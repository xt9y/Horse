#ifndef HORSE_INTERACTIVITY_MATH_HPP
#define HORSE_INTERACTIVITY_MATH_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace Interactivity::InternalMath {

inline std::size_t matrixDimension(std::string_view type)
{
    if (type == "float2x2") return 2u;
    if (type == "float3x3") return 3u;
    if (type == "float4x4") return 4u;
    return 0u;
}

inline std::string matrixType(std::size_t dimension)
{
    if (dimension == 2u) return "float2x2";
    if (dimension == 3u) return "float3x3";
    return "float4x4";
}

inline std::vector<double> transpose(
    std::string_view type,
    const std::vector<double>& source)
{
    const std::size_t n = matrixDimension(type);
    if (n == 0u || source.size() < n * n) return {};
    std::vector<double> result(n * n, 0.0);
    for (std::size_t column = 0u; column < n; ++column)
        for (std::size_t row = 0u; row < n; ++row)
            result[column * n + row] = source[row * n + column];
    return result;
}

inline double determinant(
    std::string_view type,
    const std::vector<double>& source)
{
    const std::size_t n = matrixDimension(type);
    if (n == 0u || source.size() < n * n) return 0.0;

    double rows[4][4]{};
    for (std::size_t row = 0u; row < n; ++row)
        for (std::size_t column = 0u; column < n; ++column)
            rows[row][column] = source[column * n + row];

    double result = 1.0;
    int sign = 1;
    for (std::size_t column = 0u; column < n; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1u; row < n; ++row)
            if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
        if (std::abs(rows[pivot][column]) <= 1.0e-14) return 0.0;
        if (pivot != column) {
            for (std::size_t i = 0u; i < n; ++i) std::swap(rows[pivot][i], rows[column][i]);
            sign = -sign;
        }
        const double diagonal = rows[column][column];
        result *= diagonal;
        for (std::size_t row = column + 1u; row < n; ++row) {
            const double factor = rows[row][column] / diagonal;
            for (std::size_t i = column + 1u; i < n; ++i)
                rows[row][i] -= factor * rows[column][i];
        }
    }
    return static_cast<double>(sign) * result;
}

inline bool inverse(
    std::string_view type,
    const std::vector<double>& source,
    std::vector<double> *output)
{
    if (!output) return false;
    const std::size_t n = matrixDimension(type);
    if (n == 0u || source.size() < n * n) return false;

    double augmented[4][8]{};
    for (std::size_t row = 0u; row < n; ++row) {
        for (std::size_t column = 0u; column < n; ++column)
            augmented[row][column] = source[column * n + row];
        augmented[row][row + n] = 1.0;
    }

    for (std::size_t column = 0u; column < n; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1u; row < n; ++row)
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        if (std::abs(augmented[pivot][column]) <= 1.0e-14) return false;
        if (pivot != column)
            for (std::size_t i = 0u; i < 2u * n; ++i)
                std::swap(augmented[pivot][i], augmented[column][i]);

        const double divisor = augmented[column][column];
        for (std::size_t i = 0u; i < 2u * n; ++i) augmented[column][i] /= divisor;
        for (std::size_t row = 0u; row < n; ++row) {
            if (row == column) continue;
            const double factor = augmented[row][column];
            for (std::size_t i = 0u; i < 2u * n; ++i)
                augmented[row][i] -= factor * augmented[column][i];
        }
    }

    output->assign(n * n, 0.0);
    for (std::size_t row = 0u; row < n; ++row)
        for (std::size_t column = 0u; column < n; ++column)
            (*output)[column * n + row] = augmented[row][column + n];
    return true;
}

inline std::vector<double> multiply(
    std::string_view a_type,
    const std::vector<double>& a,
    std::string_view b_type,
    const std::vector<double>& b)
{
    const std::size_t n = matrixDimension(a_type);
    if (n == 0u || matrixDimension(b_type) != n || a.size() < n * n || b.size() < n * n) return {};

    std::vector<double> result(n * n, 0.0);
    for (std::size_t column = 0u; column < n; ++column)
        for (std::size_t row = 0u; row < n; ++row)
            for (std::size_t k = 0u; k < n; ++k)
                result[column * n + row] += a[k * n + row] * b[column * n + k];
    return result;
}

using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>;

inline double dot3(const Vec3& a, const Vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vec3 cross3(const Vec3& a, const Vec3& b)
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
}

inline Vec3 normalize3(Vec3 value, bool *valid = nullptr)
{
    const double length2 = dot3(value, value);
    if (!(length2 > 0.0) || !std::isfinite(length2)) {
        if (valid) *valid = false;
        return {0.0, 0.0, 0.0};
    }
    const double inverse = 1.0 / std::sqrt(length2);
    for (double& item : value) item *= inverse;
    if (valid) *valid = true;
    return value;
}

inline Quat normalizeQuat(Quat value)
{
    const double length2 = value[0] * value[0] + value[1] * value[1] + value[2] * value[2] + value[3] * value[3];
    if (!(length2 > 0.0) || !std::isfinite(length2)) return {0.0, 0.0, 0.0, 1.0};
    const double inverse = 1.0 / std::sqrt(length2);
    for (double& item : value) item *= inverse;
    return value;
}

inline Quat quatMultiply(const Quat& a, const Quat& b)
{
    return {
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    };
}

inline Quat axisQuaternion(char axis, double angle)
{
    const double sine = std::sin(angle * 0.5);
    const double cosine = std::cos(angle * 0.5);
    if (axis == 'x') return {sine, 0.0, 0.0, cosine};
    if (axis == 'y') return {0.0, sine, 0.0, cosine};
    return {0.0, 0.0, sine, cosine};
}

inline Quat quatFromAngles(double x, double y, double z, std::string_view order)
{
    if (order != "xyz" && order != "xzy" && order != "yxz" &&
        order != "yzx" && order != "zxy" && order != "zyx")
        order = "yxz";
    const auto angle = [=](char axis) { return axis == 'x' ? x : axis == 'y' ? y : z; };
    Quat result{0.0, 0.0, 0.0, 1.0};
    for (const char axis : order) result = quatMultiply(result, axisQuaternion(axis, angle(axis)));
    return normalizeQuat(result);
}

inline Quat quatFromDirections(Vec3 from, Vec3 to)
{
    bool from_valid = false;
    bool to_valid = false;
    from = normalize3(from, &from_valid);
    to = normalize3(to, &to_valid);
    if (!from_valid || !to_valid) return {0.0, 0.0, 0.0, 1.0};

    const double cosine = std::clamp(dot3(from, to), -1.0, 1.0);
    if (cosine > 1.0 - 1.0e-12) return {0.0, 0.0, 0.0, 1.0};
    if (cosine < -1.0 + 1.0e-12) {
        const Vec3 basis = std::abs(from[0]) <= std::abs(from[1]) && std::abs(from[0]) <= std::abs(from[2])
            ? Vec3{1.0, 0.0, 0.0}
            : std::abs(from[1]) <= std::abs(from[2]) ? Vec3{0.0, 1.0, 0.0} : Vec3{0.0, 0.0, 1.0};
        const Vec3 axis = normalize3(cross3(from, basis));
        return {axis[0], axis[1], axis[2], 0.0};
    }

    const Vec3 axis = cross3(from, to);
    return normalizeQuat({axis[0], axis[1], axis[2], 1.0 + cosine});
}

inline Quat quaternionFromBasis(const Vec3& right, const Vec3& up, const Vec3& forward)
{
    const double m00 = right[0], m01 = up[0], m02 = forward[0];
    const double m10 = right[1], m11 = up[1], m12 = forward[1];
    const double m20 = right[2], m21 = up[2], m22 = forward[2];
    const double trace = m00 + m11 + m22;
    Quat q{};
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q = {0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q = {(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s};
    }
    return normalizeQuat(q);
}

inline Quat quatFromUpForward(Vec3 up, Vec3 forward)
{
    bool forward_valid = false;
    forward = normalize3(forward, &forward_valid);
    if (!forward_valid) return {0.0, 0.0, 0.0, 1.0};

    Vec3 right = cross3(up, forward);
    bool right_valid = false;
    right = normalize3(right, &right_valid);
    if (!right_valid) {
        const Vec3 basis = std::abs(forward[1]) < 0.999 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};
        right = normalize3(cross3(basis, forward));
    }
    const Vec3 corrected_up = normalize3(cross3(forward, right));
    return quaternionFromBasis(right, corrected_up, forward);
}

struct DecomposedTransform {
    Vec3 translation{};
    Quat rotation{0.0, 0.0, 0.0, 1.0};
    Vec3 scale{1.0, 1.0, 1.0};
    bool valid = false;
};

inline DecomposedTransform decomposeTransform(const std::vector<double>& matrix)
{
    DecomposedTransform result;
    if (matrix.size() < 16u || std::abs(matrix[3]) > 1.0e-12 || std::abs(matrix[7]) > 1.0e-12 ||
        std::abs(matrix[11]) > 1.0e-12 || std::abs(matrix[15] - 1.0) > 1.0e-12)
        return result;

    result.translation = {matrix[12], matrix[13], matrix[14]};
    Vec3 x{matrix[0], matrix[1], matrix[2]};
    Vec3 y{matrix[4], matrix[5], matrix[6]};
    Vec3 z{matrix[8], matrix[9], matrix[10]};
    result.scale = {
        std::sqrt(dot3(x, x)),
        std::sqrt(dot3(y, y)),
        std::sqrt(dot3(z, z)),
    };
    if (!(result.scale[0] > 0.0) || !(result.scale[1] > 0.0) || !(result.scale[2] > 0.0)) return result;

    for (double& item : x) item /= result.scale[0];
    for (double& item : y) item /= result.scale[1];
    for (double& item : z) item /= result.scale[2];
    const double orientation = dot3(x, cross3(y, z));
    if (orientation < 0.0) {
        result.scale[0] = -result.scale[0];
        for (double& item : x) item = -item;
    }
    result.rotation = quaternionFromBasis(x, y, z);
    result.valid = true;
    return result;
}

inline Vec3 rgbToOkLCh(double r, double g, double b)
{
    const double lp = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    const double mp = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    const double sp = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;
    const double l = std::cbrt(lp);
    const double m = std::cbrt(mp);
    const double s = std::cbrt(sp);
    const double lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return {lightness, std::sqrt(a * a + bb * bb), std::atan2(bb, a)};
}

inline Vec3 rgbFromOkLCh(double lightness, double chroma, double hue)
{
    const double a = chroma * std::cos(hue);
    const double b = chroma * std::sin(hue);
    const double l = lightness + 0.3963377774 * a + 0.2158037573 * b;
    const double m = lightness - 0.1055613458 * a - 0.0638541728 * b;
    const double s = lightness - 0.0894841775 * a - 1.2914855480 * b;
    const double lp = l * l * l;
    const double mp = m * m * m;
    const double sp = s * s * s;
    return {
        4.0767416621 * lp - 3.3077115913 * mp + 0.2309699292 * sp,
       -1.2684380046 * lp + 2.6097574011 * mp - 0.3413193965 * sp,
       -0.0041960863 * lp - 0.7034186147 * mp + 1.7076147010 * sp,
    };
}

} // namespace Interactivity::InternalMath

#endif
