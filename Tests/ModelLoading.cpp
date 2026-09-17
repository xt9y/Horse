#include <Core/Jobs/Jobs.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

namespace {

void testJobsRunWorkOffThreadAndCompletionOnCaller()
{
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id worker{};
    std::thread::id completion{};
    std::atomic<int> value{0};

    const bool submitted = Core::Jobs::trySubmit(
        [&] {
            worker = std::this_thread::get_id();
            value.store(1, std::memory_order_release);
        },
        [&] {
            completion = std::this_thread::get_id();
            assert(value.load(std::memory_order_acquire) == 1);
            value.store(2, std::memory_order_release);
        }
    );

    assert(submitted);
    Core::Jobs::wait();
    assert(value.load(std::memory_order_acquire) == 1);
    assert(Core::Jobs::pump() == 1u);
    assert(value.load(std::memory_order_acquire) == 2);
    assert(worker != caller);
    assert(completion == caller);
}

} // namespace

int main()
{
    testJobsRunWorkOffThreadAndCompletionOnCaller();
    Core::Jobs::shutdown();
    return 0;
}
