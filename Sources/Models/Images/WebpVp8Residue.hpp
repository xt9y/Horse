#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_RESIDUE_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_RESIDUE_HPP

#include "Models/Images/WebpVp8Frame.hpp"
#include "Models/Images/WebpVp8Modes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images::WebpVp8Internal {

struct QuantFactors {
    int y1_dc = 0;
    int y1_ac = 0;
    int y2_dc = 0;
    int y2_ac = 0;
    int uv_dc = 0;
    int uv_ac = 0;
};

struct Coefficients {
    std::array<std::int16_t, 16> value {};
    bool nonzero = false;
};

struct MacroblockResidue {
    QuantFactors quant;
    Coefficients y2;
    std::array<Coefficients, 16> y;
    std::array<Coefficients, 4> u;
    std::array<Coefficients, 4> v;
    bool has_y2 = false;
};

struct ResidueGrid {
    std::size_t columns = 0u;
    std::size_t rows = 0u;
    std::vector<MacroblockResidue> macroblocks;

    const MacroblockResidue *at(std::size_t x, std::size_t y) const
    {
        if (x >= columns || y >= rows) return nullptr;
        return &macroblocks[y * columns + x];
    }
};

bool decodeResidue(
    const KeyFrame& frame,
    const MacroblockGrid& modes,
    ResidueGrid *residue,
    std::string *error = nullptr
);

} // namespace Models::Images::WebpVp8Internal

#endif
