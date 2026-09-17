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
};

struct Runtime
{
    std::vector<Request> requests;
    std::unordered_map<std::string, LoadHandle> paths;
    std::uint64_t generation = 1u;
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
    if (request.generation != generation || request.state != LoadState::Pending) return;
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

    Request& stored = state.requests[handle];
    if (stored.state == LoadState::Ready) return handle;

    const std::uint64_t generation = stored.generation;
    const auto result = std::make_shared<WorkResult>();
    stored.work = result;

    const bool queued = Core::Jobs::trySubmit(
        modelJobGroup(),
        [normalized, result] {
            result->ok = Formats::stage(normalized, &result->staged, &result->error);
        },
        [handle, generation, result] {
            complete(handle, generation, result);
        }
    );

    if (!queued) fail(stored, "model load queue is full");
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
    return Core::Jobs::pump(modelJobGroup());
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
}

} // namespace Models::Internal
