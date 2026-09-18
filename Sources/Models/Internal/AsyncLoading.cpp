#include "Models/Internal/AsyncLoading.hpp"

#include "Core/Jobs/Jobs.hpp"
#include "Models/Formats/Registry.hpp"
#include "Models/Internal/Registry.hpp"
#include "Models/Internal/StagedModel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models::Internal {
namespace {

std::size_t maximumInFlightModelJobs()
{
    const unsigned int hardware = std::thread::hardware_concurrency();
    const std::size_t available = hardware > 1u
        ? static_cast<std::size_t>(hardware - 1u)
        : 1u;
    return std::clamp<std::size_t>(available, 2u, 8u);
}

struct WorkResult
{
    bool ok = false;
    StagedModel staged;
    std::string error;
};

struct Request
{
    LoadHandle handle = INVALID_LOAD;
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
    std::unordered_map<LoadHandle, std::size_t> request_indices;
    std::unordered_map<std::string, LoadHandle> paths;
    std::uint64_t generation = 1u;
    LoadHandle next_handle = 0u;
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

Request *requestFor(Runtime& state, LoadHandle handle)
{
    const auto found = state.request_indices.find(handle);
    if (found == state.request_indices.end() || found->second >= state.requests.size())
        return nullptr;
    return &state.requests[found->second];
}

const Request *requestFor(const Runtime& state, LoadHandle handle)
{
    const auto found = state.request_indices.find(handle);
    if (found == state.request_indices.end() || found->second >= state.requests.size())
        return nullptr;
    return &state.requests[found->second];
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
    if (generation != state.generation) return;

    Request *request = requestFor(state, handle);
    if (!request || request->generation != generation ||
        request->state != LoadState::Pending || !request->submitted)
        return;

    request->submitted = false;
    if (state.in_flight != 0u) --state.in_flight;

    if (!result || !result->ok) {
        const std::string reason = result && !result->error.empty()
            ? result->error
            : std::string("asynchronous model staging failed");
        fail(*request, reason);
        return;
    }

    std::unordered_map<TextureHandle, TextureHandle> mapping;
    std::string error;
    if (!materializeStagedTextures(result->staged, &mapping, &error) ||
        !remapDocumentTextures(result->staged.document, mapping, &error))
    {
        fail(*request, std::move(error));
        return;
    }

    const ModelHandle model = publishDocument(
        request->path,
        std::move(result->staged.document),
        &error);
    if (model == INVALID_MODEL) {
        fail(*request, std::move(error));
        return;
    }

    request->model = model;
    request->error.clear();
    request->work.reset();
    request->state = LoadState::Ready;
}

bool schedule(LoadHandle handle)
{
    Runtime& state = runtime();
    if (state.in_flight >= maximumInFlightModelJobs()) return false;

    Request *request = requestFor(state, handle);
    if (!request || request->state != LoadState::Pending || request->submitted)
        return false;

    const std::uint64_t generation = request->generation;
    const std::string path = request->path;
    const auto result = std::make_shared<WorkResult>();

    const bool queued = Core::Jobs::trySubmit(
        modelJobGroup(),
        [path, result] {
            try {
                result->ok = Formats::stage(path, &result->staged, &result->error);
            } catch (const std::exception& exception) {
                result->ok = false;
                result->error = exception.what();
            } catch (...) {
                result->ok = false;
                result->error = "asynchronous model loader threw an unknown exception";
            }
        },
        [handle, generation, result] {
            complete(handle, generation, result);
        }
    );
    if (!queued) return false;

    request->work = result;
    request->submitted = true;
    ++state.in_flight;
    return true;
}

void schedulePending()
{
    Runtime& state = runtime();
    if (state.in_flight >= maximumInFlightModelJobs()) return;

    for (const Request& request : state.requests) {
        if (state.in_flight >= maximumInFlightModelJobs()) break;
        schedule(request.handle);
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
    if (state.next_handle == INVALID_LOAD) return INVALID_LOAD;

    Request request;
    request.handle = state.next_handle++;
    request.path = normalized;
    request.generation = state.generation;

    if (const ModelHandle existing = loadedModelForPath(normalized); existing != INVALID_MODEL) {
        request.state = LoadState::Ready;
        request.model = existing;
    }

    const LoadHandle handle = request.handle;
    const std::size_t index = state.requests.size();
    state.requests.push_back(std::move(request));
    state.request_indices.emplace(handle, index);
    state.paths.emplace(normalized, handle);

    if (state.requests[index].state == LoadState::Pending) schedulePending();
    return handle;
}

LoadState modelLoadState(LoadHandle handle)
{
    pumpModelLoads();

    const Runtime& state = runtime();
    const Request *request = requestFor(state, handle);
    return request ? request->state : LoadState::Failed;
}

ModelHandle modelLoadResult(LoadHandle handle, std::string *error)
{
    if (error) error->clear();
    pumpModelLoads();

    const Runtime& state = runtime();
    const Request *request = requestFor(state, handle);
    if (!request) {
        if (error) *error = "invalid model load handle";
        return INVALID_MODEL;
    }

    if (request->state == LoadState::Ready) return request->model;
    if (request->state == LoadState::Failed && error) *error = request->error;
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
    state.request_indices.clear();
    state.paths.clear();
    state.in_flight = 0u;
}

} // namespace Models::Internal
