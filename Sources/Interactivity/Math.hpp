#ifndef HORSE_INTERACTIVITY_MATH_HPP
#define HORSE_INTERACTIVITY_MATH_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

} // namespace Interactivity::InternalMath

#endif
