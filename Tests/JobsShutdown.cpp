#include <Core/Jobs/Jobs.hpp>

#include <atomic>
#include <cassert>
#include <cstddef>

int main()
{
    std::size_t accepted = 0u;
    for (std::size_t index = 0u; index < 512u; ++index) {
        if (!Core::Jobs::trySubmit([] {}, [] {})) break;
        ++accepted;
        Core::Jobs::wait();
    }
    assert(accepted < 512u);
    assert(Core::Jobs::pump() == accepted);

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
