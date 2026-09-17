#include <Core/Jobs/Jobs.hpp>

#include <atomic>
#include <cassert>
#include <thread>

int main()
{
    const Core::Jobs::Group models = Core::Jobs::createGroup();
    const Core::Jobs::Group other = Core::Jobs::createGroup();

    std::atomic<bool> release {false};
    std::atomic<bool> model_done {false};
    std::atomic<bool> other_done {false};

    assert(Core::Jobs::trySubmit(models, [&] {
        model_done.store(true, std::memory_order_release);
    }));
    assert(Core::Jobs::trySubmit(other, [&] {
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        other_done.store(true, std::memory_order_release);
    }));

    Core::Jobs::wait(models);
    assert(model_done.load(std::memory_order_acquire));
    assert(!other_done.load(std::memory_order_acquire));

    release.store(true, std::memory_order_release);
    Core::Jobs::wait(other);
    assert(other_done.load(std::memory_order_acquire));

    Core::Jobs::shutdown();
    return 0;
}
