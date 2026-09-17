#include <Core/Jobs/Jobs.hpp>

#include <atomic>
#include <cassert>

int main()
{
    std::atomic<bool> ran {false};
    Core::Jobs::shutdown();

    const bool submitted = Core::Jobs::trySubmit([&] {
        ran.store(true, std::memory_order_release);
    });

    assert(!submitted);
    Core::Jobs::wait();
    assert(!ran.load(std::memory_order_acquire));
    return 0;
}
