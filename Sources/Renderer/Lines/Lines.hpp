#ifndef HORSE_RENDERER_LINES_LINES_HPP
#define HORSE_RENDERER_LINES_LINES_HPP

#include "Renderer/Components.hpp"

#include <cstddef>

namespace Renderer::Lines {

void clear();
void reserve(std::size_t segments);
void add(Vec3 from, Vec3 to, Vec4 color);
std::size_t count();

} // namespace Renderer::Lines

#endif
