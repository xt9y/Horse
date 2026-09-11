#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_FRAME_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_FRAME_HPP

#include "Models/Images/WebpVp8Bits.hpp"
#include "Models/Images/WebpVp8Tables.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images::WebpVp8Internal {

struct Segmentation {
    bool enabled = false;
    bool update_map = false;
    bool absolute = false;
    std::array<int, 4> quantizer {{0,0,0,0}};
    std::array<int, 4> filter {{0,0,0,0}};
    std::array<std::uint8_t, 3> tree_probability {{255u,255u,255u}};
};

struct LoopFilter {
    bool simple = false;
    unsigned int level = 0u;
    unsigned int sharpness = 0u;
    bool delta_enabled = false;
    std::array<int, 4> reference_delta {{0,0,0,0}};
    std::array<int, 4> mode_delta {{0,0,0,0}};
};

struct Quantizer {
    unsigned int base = 0u;
    int y1_dc = 0;
    int y2_dc = 0;
    int y2_ac = 0;
    int uv_dc = 0;
    int uv_ac = 0;
};

struct TokenPartition {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0u;
};

struct KeyFrame {
    int width = 0;
    int height = 0;
    unsigned int version = 0u;
    bool shown = false;
    Segmentation segmentation;
    LoopFilter filter;
    Quantizer quantizer;
    bool refresh_entropy = false;
    CoeffProbabilities coefficient_probabilities = DefaultCoeffProbabilities;
    bool coefficient_skip_enabled = false;
    std::uint8_t coefficient_skip_probability = 0u;
    BoolDecoder header;
    std::vector<TokenPartition> token_partitions;
};

bool parseKeyFrame(
    const std::uint8_t *data,
    std::size_t size,
    KeyFrame *frame,
    std::string *error = nullptr
);

} // namespace Models::Images::WebpVp8Internal

#endif
