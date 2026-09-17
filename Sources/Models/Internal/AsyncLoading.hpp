#ifndef HORSE_MODELS_INTERNAL_ASYNC_LOADING_HPP
#define HORSE_MODELS_INTERNAL_ASYNC_LOADING_HPP

#include "Models/Models.hpp"

#include <cstddef>
#include <string>

namespace Models::Internal {

LoadHandle requestModelLoad(const std::string& path);
LoadState modelLoadState(LoadHandle handle);
ModelHandle modelLoadResult(LoadHandle handle, std::string *error = nullptr);
std::size_t pumpModelLoads();
void clearModelLoads();

} // namespace Models::Internal

#endif
