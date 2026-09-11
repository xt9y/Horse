#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_TABLES_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_TABLES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace Models::Images::WebpVp8Internal {

using CoeffNode = std::array<std::uint8_t, 11>;
using CoeffContext = std::array<CoeffNode, 3>;
using CoeffBandSet = std::array<CoeffContext, 8>;
using CoeffProbabilities = std::array<CoeffBandSet, 4>;
using BModeProbabilities = std::array<std::array<std::array<std::uint8_t, 9>, 10>, 10>;

extern const CoeffProbabilities DefaultCoeffProbabilities;
extern const CoeffProbabilities CoeffUpdateProbabilities;
extern const BModeProbabilities KeyFrameBModeProbabilities;

extern const std::array<std::uint8_t, 4> KeyFrameYModeProbabilities;
extern const std::array<std::uint8_t, 3> KeyFrameUvModeProbabilities;
extern const std::array<std::uint8_t, 16> CoeffBands;
extern const std::array<std::uint8_t, 16> ZigZag;
extern const std::array<int, 128> DcQuant;
extern const std::array<int, 128> AcQuant;

struct ExtraBits {
    int minimum = 0;
    std::uint8_t count = 0u;
    std::array<std::uint8_t, 11> probabilities {};
};

extern const std::array<ExtraBits, 6> CoeffExtraBits;

} // namespace Models::Images::WebpVp8Internal

#endif
