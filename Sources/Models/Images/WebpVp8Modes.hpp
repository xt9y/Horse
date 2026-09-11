#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_MODES_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_MODES_HPP

#include "Models/Images/WebpVp8Frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images::WebpVp8Internal {

enum class YMode : std::uint8_t {
    Dc = 0,
    Vertical = 1,
    Horizontal = 2,
    TrueMotion = 3,
    Blocks = 4,
};

enum class BMode : std::uint8_t {
    Dc = 0,
    TrueMotion = 1,
    Vertical = 2,
    Horizontal = 3,
    LeftDown = 4,
    RightDown = 5,
    VerticalRight = 6,
    VerticalLeft = 7,
    HorizontalDown = 8,
    HorizontalUp = 9,
};

struct MacroblockMode {
    std::uint8_t segment = 0u;
    bool skip_coefficients = false;
    YMode y_mode = YMode::Dc;
    YMode uv_mode = YMode::Dc;
    std::array<BMode, 16> blocks {};
};

struct MacroblockGrid {
    std::size_t columns = 0u;
    std::size_t rows = 0u;
    std::vector<MacroblockMode> macroblocks;

    const MacroblockMode *at(std::size_t x, std::size_t y) const
    {
        if (x >= columns || y >= rows) return nullptr;
        return &macroblocks[y * columns + x];
    }
};

bool decodeMacroblockModes(KeyFrame *frame, MacroblockGrid *grid, std::string *error = nullptr);

} // namespace Models::Images::WebpVp8Internal

#endif
