#ifndef HORSE_CORE_JOBS_JOBS_HPP
#define HORSE_CORE_JOBS_JOBS_HPP

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Core::Jobs {

using Work = std::function<void()>;
using Completion = std::function<void()>;
using Group = std::uint64_t;

inline constexpr Group DefaultGroup = 0u;

Group createGroup();
bool trySubmit(Group group, Work work, Completion completion = {});
bool trySubmit(Work work, Completion completion = {});
std::size_t pump(Group group);
std::size_t pump();
void cancelPending(Group group);
void cancelPending();
void wait(Group group);
void wait();
void shutdown();

} // namespace Core::Jobs

#endif