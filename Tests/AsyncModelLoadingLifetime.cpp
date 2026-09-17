#include "Models/Formats/Registry.hpp"
#include "Models/Models.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace {

std::atomic<int> calls{0};
std::atomic<bool> started{false};

bool lifetimeLoader(
    const std::string&,
    Models::Formats::Document *document,
    std::string *error)
{
    ++calls;
    started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (error) error->clear();
    if (!document) return false;

    Models::Formats::Part part;
    part.mesh.vertices.resize(3u);
    part.mesh.indices = {0u, 1u, 2u};
    document->parts.push_back(std::move(part));
    return true;
}

Models::LoadState waitFor(Models::LoadHandle handle)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
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
    Models::clearCache();
    assert(Models::Formats::registerLoader(".asynclife", lifetimeLoader));

    const Models::LoadHandle stale = Models::loadAsync("generation.asynclife");
    assert(stale != Models::INVALID_LOAD);

    const auto started_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!started.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < started_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(started.load(std::memory_order_acquire));

    Models::clearCache();

    std::string error;
    assert(Models::loadResult(stale, &error) == Models::INVALID_MODEL);
    assert(!error.empty());

    started.store(false, std::memory_order_release);
    const Models::LoadHandle current = Models::loadAsync("generation.asynclife");
    assert(current != Models::INVALID_LOAD);
    assert(current != stale);

    error.clear();
    assert(Models::loadResult(stale, &error) == Models::INVALID_MODEL);
    assert(!error.empty());

    assert(waitFor(current) == Models::LoadState::Ready);
    const Models::ModelHandle model = Models::loadResult(current, &error);
    assert(model != Models::INVALID_MODEL);
    assert(Models::partCount(model) == 1u);
    assert(calls.load() >= 2);

    Models::clearCache();
    return 0;
}
