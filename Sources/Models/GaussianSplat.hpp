#ifndef HORSE_MODELS_GAUSSIAN_SPLAT_HPP
#define HORSE_MODELS_GAUSSIAN_SPLAT_HPP

#include "Models/Models.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::GaussianSplat {

enum class ColorSpace : std::uint8_t {
    SrgbRec709Display,
    LinearRec709Display,
};

enum class Projection : std::uint8_t {
    Perspective,
};

enum class SortingMethod : std::uint8_t {
    CameraDistance,
};

struct Splat {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale {1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    std::vector<Vec3> spherical_harmonics;
};

struct Data {
    ColorSpace color_space = ColorSpace::SrgbRec709Display;
    Projection projection = Projection::Perspective;
    SortingMethod sorting = SortingMethod::CameraDistance;
    std::uint32_t spherical_harmonic_degree = 0u;
    std::vector<Splat> splats;
};

bool isGaussianSplat(const MeshData& mesh);
bool decode(const MeshData& mesh, Data *output, std::string *error = nullptr);

} // namespace Models::GaussianSplat

#endif
