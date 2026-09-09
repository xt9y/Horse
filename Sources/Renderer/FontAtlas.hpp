#ifndef RW_ENGINE_RENDERER_FONT_ATLAS_HPP
#define RW_ENGINE_RENDERER_FONT_ATLAS_HPP

#include <cstdint>
#include <vector>

namespace Renderer::FontAtlas {

inline constexpr int WIDTH = 128;
inline constexpr int HEIGHT = 128;
inline constexpr const char *ASSET_PATH = "Assests/Font/font.png";

std::vector<std::uint8_t> rgba();

} // namespace Renderer::FontAtlas

#endif
