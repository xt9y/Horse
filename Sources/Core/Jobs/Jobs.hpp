#ifndef HORSE_CORE_JOBS_JOBS_HPP
#define HORSE_CORE_JOBS_JOBS_HPP

#include <cstddef>
#include <functional>

namespace Core::Jobs {

using Work = std::function<void()>;
using Completion = std::function<void()>;

bool trySubmit(Work work, Completion completion = {});
std::size_t pump();
void cancelPending();
void wait();
void shutdown();

} // namespace Core::Jobs

#endif
