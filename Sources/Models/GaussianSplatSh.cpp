#include "Models/GaussianSplat.hpp"

#include <algorithm>
#include <cmath>

namespace Models::GaussianSplat {
namespace {

constexpr float C0 = 0.28209479177387814f;
constexpr float C1 = 0.4886025119029199f;
constexpr float C2a = 1.0925484305920792f;
constexpr float C2b = 0.31539156525252005f;
constexpr float C2c = 0.5462742152960396f;
constexpr float C3a = 0.5900435899266435f;
constexpr float C3b = 2.890611442640554f;
constexpr float C3c = 0.4570457994644657f;
constexpr float C3d = 0.3731763325901154f;
constexpr float C3e = 1.445305721320277f;

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 coefficient(const Splat& splat, std::size_t index)
{
    return index < splat.spherical_harmonics.size()
        ? splat.spherical_harmonics[index]
        : Vec3{};
}

Vec3 normalized(Vec3 value)
{
    const float length2 = value.x * value.x + value.y * value.y + value.z * value.z;
    if (length2 <= 1.0e-20f) return {0.0f, 0.0f, 1.0f};
    const float inverse = 1.0f / std::sqrt(length2);
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

} // namespace

Vec3 evaluateSphericalHarmonics(
    const Splat& splat,
    std::uint32_t degree,
    Vec3 direction)
{
    direction = normalized(direction);
    const float x = direction.x;
    const float y = direction.y;
    const float z = direction.z;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;

    Vec3 color = multiply(coefficient(splat, 0u), C0);

    if (degree >= 1u && splat.spherical_harmonics.size() >= 4u) {
        color = add(color, multiply(coefficient(splat, 1u), -C1 * y));
        color = add(color, multiply(coefficient(splat, 2u),  C1 * z));
        color = add(color, multiply(coefficient(splat, 3u), -C1 * x));
    }

    if (degree >= 2u && splat.spherical_harmonics.size() >= 9u) {
        color = add(color, multiply(coefficient(splat, 4u), C2a * x * y));
        color = add(color, multiply(coefficient(splat, 5u), -C2a * y * z));
        color = add(color, multiply(coefficient(splat, 6u), C2b * (2.0f * zz - xx - yy)));
        color = add(color, multiply(coefficient(splat, 7u), -C2a * x * z));
        color = add(color, multiply(coefficient(splat, 8u), C2c * (xx - yy)));
    }

    if (degree >= 3u && splat.spherical_harmonics.size() >= 16u) {
        color = add(color, multiply(coefficient(splat, 9u), -C3a * y * (3.0f * xx - yy)));
        color = add(color, multiply(coefficient(splat, 10u), C3b * x * y * z));
        color = add(color, multiply(coefficient(splat, 11u), -C3c * y * (4.0f * zz - xx - yy)));
        color = add(color, multiply(coefficient(splat, 12u), C3d * z * (2.0f * zz - 3.0f * xx - 3.0f * yy)));
        color = add(color, multiply(coefficient(splat, 13u), -C3c * x * (4.0f * zz - xx - yy)));
        color = add(color, multiply(coefficient(splat, 14u), C3e * z * (xx - yy)));
        color = add(color, multiply(coefficient(splat, 15u), -C3a * x * (xx - 3.0f * yy)));
    }

    color.x = std::max(color.x + 0.5f, 0.0f);
    color.y = std::max(color.y + 0.5f, 0.0f);
    color.z = std::max(color.z + 0.5f, 0.0f);
    return color;
}

} // namespace Models::GaussianSplat
