#include "Models/Internal/AsyncLoading.hpp"

#include "Core/Jobs/Jobs.hpp"
#include "Models/Formats/Registry.hpp"
#include "Models/Internal/Registry.hpp"
#include "Models/Internal/StagedModel.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models::Internal {
namespace {

constexpr std::size_t MaximumInFlightModelJobs = 2u;

struct WorkResult
{
    bool ok = false;
    StagedModel staged;
    std::string error;
};

struct Request
{
    std::string path;
    LoadState state = LoadState::Pending;
    ModelHandle model = INVALID_MODEL;
    std::string error;
    std::uint64_t generation = 0u;
    std::shared_ptr<WorkResult> work;
    bool submitted = false;
};

struct Runtime
{
    std::vector<Request> requests;
    std::unordered_map<std::string, LoadHandle> paths;
    std::uint64_t generation = 1u;
    std::size_t in_flight = 0u;
};

Runtime& runtime()
{
    static Runtime value;
    return value;
}

Core::Jobs::Group modelJobGroup()
{
    static const Core::Jobs::Group value = Core::Jobs::createGroup();
    return value;
}

void fail(Request& request, std::string error)
{
    request.model = INVALID_MODEL;
    request.error = std::move(error);
    request.work.reset();
    request.submitted = false;
    request.state = LoadState::Failed;
}

void complete(
    LoadHandle handle,
    std::uint64_t generation,
    const std::shared_ptr<WorkResult>& result)
{
    Runtime& state = runtime();
    if (generation != state.generation || handle >= state.requests.size()) return;

    Request& request = state.requests[handle];
    if (request.generation != generation || request.state != LoadState::Pending || !request.submitted)
        return;

    request.submitted = false;
    if (state.in_flight != 0u) --state.in_flight;

    if (!result || !result->ok) {
        fail(request, result ? result->error : std::string("model load produced no result"));
        return;
    }

    std::unordered_map<TextureHandle, TextureHandle> mapping;
    std::string error;
    if (!materializeStagedTextures(result->staged, &mapping, &error) ||
        !remapDocumentTextures(result->staged.document, mapping, &error))
    {
        fail(request, std::move(error));
        return;
    }

    const ModelHandle model = publishDocument(
        request.path,
        std::move(result->staged.document),
        &error);
    if (model == INVALID_MODEL) {
        fail(request, std::move(error));
        return;
    }

    request.model = model;
    request.error.clear();
    request.work.reset();
    request.state = LoadState::Ready;
}

bool schedule(LoadHandle handle)
{
    Runtime& state = runtime();
    if (handle >= state.requests.size() || state.in_flight >= MaximumInFlightModelJobs)
        return false;

    Request& request = state.requests[handle];
    if (request.state != LoadState::Pending || request.submitted) return false;

    const std::uint64_t generation = request.generation;
    const std::string path = request.path;
    const auto result = std::make_shared<WorkResult>();

    const bool queued = Core::Jobs::trySubmit(
        modelJobGroup(),
        [path, result] {
            result->ok = Formats::stage(path, &result->staged, &result->error);
        },
        [handle, generation, result] {
            complete(handle, generation, result);
        }
    );
    if (!queued) return false;

    request.work = result;
    request.submitted = true;
    ++state.in_flight;
    return true;
}

void schedulePending()
{
    Runtime& state = runtime();
    if (state.in_flight >= MaximumInFlightModelJobs) return;
    for (std::size_t index = 0u;
         index < state.requests.size() && state.in_flight < MaximumInFlightModelJobs;
         ++index)
    {
        schedule(static_cast<LoadHandle>(index));
    }
}

} // namespace

LoadHandle requestModelLoad(const std::string& path)
{
    Runtime& state = runtime();
    const std::string normalized = normalizeModelPath(path);
    if (normalized.empty()) return INVALID_LOAD;

    if (const auto found = state.paths.find(normalized); found != state.paths.end())
        return found->second;
    if (state.requests.size() >= static_cast<std::size_t>(INVALID_LOAD))
        return INVALID_LOAD;

    Request request;
    request.path = normalized;
    request.generation = state.generation;

    if (const ModelHandle existing = loadedModelForPath(normalized); existing != INVALID_MODEL) {
        request.state = LoadState::Ready;
        request.model = existing;
    }

    const LoadHandle handle = static_cast<LoadHandle>(state.requests.size());
    state.requests.push_back(std::move(request));
    state.paths.emplace(normalized, handle);

    if (state.requests[handle].state == LoadState::Pending) schedulePending();
    return handle;
}

LoadState modelLoadState(LoadHandle handle)
{
    pumpModelLoads();
    Runtime& state = runtime();
    if (handle >= state.requests.size()) return LoadState::Failed;
    return state.requests[handle].state;
}

ModelHandle modelLoadResult(LoadHandle handle, std::string *error)
{
    if (error) error->clear();
    pumpModelLoads();

    Runtime& state = runtime();
    if (handle >= state.requests.size()) {
        if (error) *error = "invalid model load handle";
        return INVALID_MODEL;
    }

    const Request& request = state.requests[handle];
    if (request.state == LoadState::Ready) return request.model;
    if (request.state == LoadState::Failed && error) *error = request.error;
    return INVALID_MODEL;
}

std::size_t pumpModelLoads()
{
    const std::size_t completed = Core::Jobs::pump(modelJobGroup());
    schedulePending();
    return completed;
}

void clearModelLoads()
{
    Runtime& state = runtime();
    ++state.generation;
    if (state.generation == 0u) state.generation = 1u;

    Core::Jobs::cancelPending(modelJobGroup());
    Core::Jobs::wait(modelJobGroup());
    Core::Jobs::pump(modelJobGroup());

    state.requests.clear();
    state.paths.clear();
    state.in_flight = 0u;
}

} // namespace Models::Internal
