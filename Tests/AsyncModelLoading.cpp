#include "Models/Formats/Registry.hpp"
#include "Models/Models.hpp"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

namespace {

bool slowLoader(
    const std::string&,
    Models::Formats::Document *document,
    std::string *error)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (error) error->clear();
    if (!document) return false;

    Models::Formats::Part part;
    part.mesh.vertices.resize(3u);
    part.mesh.vertices[0].position = {0.0f, 0.0f, 0.0f};
    part.mesh.vertices[1].position = {1.0f, 0.0f, 0.0f};
    part.mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
    part.mesh.indices = {0u, 1u, 2u};
    document->parts.push_back(std::move(part));
    return true;
}

bool failingLoader(
    const std::string&,
    Models::Formats::Document *,
    std::string *error)
{
    if (error) *error = "intentional async loader failure";
    return false;
}

Models::LoadState waitFor(Models::LoadHandle handle)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    Models::LoadState state = Models::LoadState::Pending;
    while (std::chrono::steady_clock::now() < deadline) {
        state = Models::loadState(handle);
        if (state != Models::LoadState::Pending) return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return state;
}

} // namespace

int main()
{
    using Clock = std::chrono::steady_clock;

    Models::clearCache();
    assert(Models::Formats::registerLoader(".asyncslow", slowLoader));
    assert(Models::Formats::registerLoader(".asyncfail", failingLoader));

    const auto begin = Clock::now();
    const Models::LoadHandle first = Models::loadAsync("async-model.asyncslow");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin);

    assert(first != Models::INVALID_LOAD);
    assert(elapsed < std::chrono::milliseconds(100));
    assert(Models::loadState(first) == Models::LoadState::Pending);

    const Models::LoadHandle duplicate = Models::loadAsync("./async-model.asyncslow");
    assert(duplicate == first);

    assert(waitFor(first) == Models::LoadState::Ready);
    std::string error;
    const Models::ModelHandle model = Models::loadResult(first, &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(model) == 1u);

    const Models::LoadHandle already_loaded = Models::loadAsync("async-model.asyncslow");
    assert(already_loaded == first);
    assert(Models::loadState(already_loaded) == Models::LoadState::Ready);
    assert(Models::loadResult(already_loaded) == model);

    const Models::LoadHandle failed = Models::loadAsync("broken.asyncfail");
    assert(failed != Models::INVALID_LOAD);
    assert(waitFor(failed) == Models::LoadState::Failed);
    error.clear();
    assert(Models::loadResult(failed, &error) == Models::INVALID_MODEL);
    assert(error == "intentional async loader failure");

    Models::clearCache();
    return 0;
}
