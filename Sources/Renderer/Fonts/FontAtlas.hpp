#ifndef RW_ENGINE_RENDERER_FONT_ATLAS_HPP
#define RW_ENGINE_RENDERER_FONT_ATLAS_HPP

#include <cstdint>
#include <vector>

namespace Renderer::FontAtlas {

inline constexpr int FALLBACK_WIDTH = 128;
inline constexpr int FALLBACK_HEIGHT = 128;
inline constexpr int WIDTH = FALLBACK_WIDTH;
inline constexpr int HEIGHT = FALLBACK_HEIGHT;

std::vector<std::uint8_t> rgba();

} // namespace Renderer::FontAtlas

#endif
