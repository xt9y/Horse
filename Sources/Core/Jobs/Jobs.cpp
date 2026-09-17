#include "Core/Jobs/Jobs.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Core::Jobs {
namespace {

struct Job
{
    Work work;
    Completion completion;
};

struct Runtime
{
    std::mutex mutex;
    std::condition_variable work_ready;
    std::condition_variable idle;
    std::deque<Job> work;
    std::deque<Completion> completions;
    std::vector<std::jthread> workers;
    std::size_t running = 0u;
    std::size_t capacity = 0u;
    bool started = false;
    bool stopping = false;

    ~Runtime()
    {
        shutdown();
    }

    void startLocked()
    {
        if (started) return;

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
                    }

                    try {
                        if (job.work) job.work();
                    } catch (...) {
                    }

                    {
                        std::lock_guard lock(mutex);
                        if (job.completion) completions.push_back(std::move(job.completion));
                        if (running != 0u) --running;
                        if (work.empty() && running == 0u) idle.notify_all();
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
            if (!started) {
                work.clear();
                completions.clear();
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

} // namespace

bool trySubmit(Work work, Completion completion)
{
    if (!work) return false;

    Runtime& state = runtime();
    {
        std::lock_guard lock(state.mutex);
        state.startLocked();
        if (state.stopping || state.work.size() >= state.capacity) return false;
        state.work.push_back({std::move(work), std::move(completion)});
    }
    state.work_ready.notify_one();
    return true;
}

std::size_t pump()
{
    Runtime& state = runtime();
    std::deque<Completion> completions;
    {
        std::lock_guard lock(state.mutex);
        completions.swap(state.completions);
    }

    const std::size_t count = completions.size();
    while (!completions.empty()) {
        Completion completion = std::move(completions.front());
        completions.pop_front();
        if (completion) completion();
    }
    return count;
}

void cancelPending()
{
    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);
    state.work.clear();
    if (state.running == 0u) state.idle.notify_all();
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
