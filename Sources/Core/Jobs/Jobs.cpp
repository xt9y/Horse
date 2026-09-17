#include "Core/Jobs/Jobs.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core::Jobs {
namespace {

struct Job
{
    Group group = DefaultGroup;
    Work work;
    Completion completion;
};

struct Completed
{
    Group group = DefaultGroup;
    Completion completion;
};

struct Runtime
{
    std::mutex mutex;
    std::condition_variable work_ready;
    std::condition_variable idle;
    std::deque<Job> work;
    std::deque<Completed> completions;
    std::vector<std::jthread> workers;
    std::unordered_map<Group, std::size_t> running_by_group;
    std::size_t running = 0u;
    std::size_t capacity = 0u;
    bool started = false;
    bool stopping = false;
    bool closed = false;

    ~Runtime()
    {
        shutdown();
    }

    void startLocked()
    {
        if (started || closed) return;

        const unsigned int hardware = std::thread::hardware_concurrency();
        const std::size_t count = std::max<std::size_t>(
            1u,
            hardware > 1u ? static_cast<std::size_t>(hardware - 1u) : 1u
        );
        capacity = std::max<std::size_t>(64u, count * 32u);
        workers.reserve(count);
        started = true;

        for (std::size_t index = 0u; index < count; ++index) {
            (void)index;
            workers.emplace_back([this](std::stop_token) {
                for (;;) {
                    Job job;
                    {
                        std::unique_lock lock(mutex);
                        work_ready.wait(lock, [&] {
                            return stopping || !work.empty();
                        });

                        if (stopping && work.empty()) return;

                        job = std::move(work.front());
                        work.pop_front();
                        ++running;
                        ++running_by_group[job.group];
                    }

                    try {
                        if (job.work) job.work();
                    } catch (...) {
                    }

                    {
                        std::lock_guard lock(mutex);
                        if (job.completion)
                            completions.push_back({job.group, std::move(job.completion)});
                        if (running != 0u) --running;

                        const auto group = running_by_group.find(job.group);
                        if (group != running_by_group.end()) {
                            if (group->second != 0u) --group->second;
                            if (group->second == 0u) running_by_group.erase(group);
                        }
                        idle.notify_all();
                    }
                }
            });
        }
    }

    void shutdown()
    {
        std::vector<std::jthread> joined;
        {
            std::lock_guard lock(mutex);
            closed = true;
            if (!started) {
                work.clear();
                completions.clear();
                running_by_group.clear();
                return;
            }
            stopping = true;
            work.clear();
            joined.swap(workers);
        }

        work_ready.notify_all();
        joined.clear();

        {
            std::lock_guard lock(mutex);
            completions.clear();
            running_by_group.clear();
            running = 0u;
            capacity = 0u;
            stopping = false;
            started = false;
            idle.notify_all();
        }
    }
};

Runtime& runtime()
{
    static Runtime value;
    return value;
}

bool queuedFor(const Runtime& state, Group group)
{
    return std::any_of(
        state.work.begin(),
        state.work.end(),
        [group](const Job& job) { return job.group == group; }
    );
}

std::size_t runningFor(const Runtime& state, Group group)
{
    const auto found = state.running_by_group.find(group);
    return found == state.running_by_group.end() ? 0u : found->second;
}

std::atomic<Group>& nextGroup()
{
    static std::atomic<Group> value {1u};
    return value;
}

} // namespace

Group createGroup()
{
    Group group = nextGroup().fetch_add(1u, std::memory_order_relaxed);
    if (group == DefaultGroup)
        group = nextGroup().fetch_add(1u, std::memory_order_relaxed);
    return group;
}

bool trySubmit(Group group, Work work, Completion completion)
{
    if (!work) return false;

    Runtime& state = runtime();
    {
        std::lock_guard lock(state.mutex);
        if (state.closed) return false;
        state.startLocked();
        if (state.stopping || state.work.size() >= state.capacity) return false;

        std::size_t remaining = state.capacity - state.work.size();
        if (state.running >= remaining) return false;
        remaining -= state.running;
        if (state.completions.size() >= remaining) return false;

        state.work.push_back({group, std::move(work), std::move(completion)});
    }
    state.work_ready.notify_one();
    return true;
}

bool trySubmit(Work work, Completion completion)
{
    return trySubmit(DefaultGroup, std::move(work), std::move(completion));
}

std::size_t pump(Group group)
{
    Runtime& state = runtime();
    std::deque<Completed> selected;
    {
        std::lock_guard lock(state.mutex);
        for (auto iterator = state.completions.begin(); iterator != state.completions.end();) {
            if (iterator->group == group) {
                selected.push_back(std::move(*iterator));
                iterator = state.completions.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    const std::size_t count = selected.size();
    while (!selected.empty()) {
        Completion completion = std::move(selected.front().completion);
        selected.pop_front();
        if (completion) completion();
    }
    return count;
}

std::size_t pump()
{
    Runtime& state = runtime();
    std::deque<Completed> completions;
    {
        std::lock_guard lock(state.mutex);
        completions.swap(state.completions);
    }

    const std::size_t count = completions.size();
    while (!completions.empty()) {
        Completion completion = std::move(completions.front().completion);
        completions.pop_front();
        if (completion) completion();
    }
    return count;
}

void cancelPending(Group group)
{
    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);
    std::erase_if(state.work, [group](const Job& job) {
        return job.group == group;
    });
    state.idle.notify_all();
}

void cancelPending()
{
    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);
    state.work.clear();
    state.idle.notify_all();
}

void wait(Group group)
{
    Runtime& state = runtime();
    std::unique_lock lock(state.mutex);
    if (!state.started) return;
    state.idle.wait(lock, [&] {
        return !queuedFor(state, group) && runningFor(state, group) == 0u;
    });
}

void wait()
{
    Runtime& state = runtime();
    std::unique_lock lock(state.mutex);
    if (!state.started) return;
    state.idle.wait(lock, [&] {
        return state.work.empty() && state.running == 0u;
    });
}

void shutdown()
{
    runtime().shutdown();
}

} // namespace Core::Jobs