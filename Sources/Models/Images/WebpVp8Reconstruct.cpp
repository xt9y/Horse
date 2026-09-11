#include "Models/Images/WebpVp8Reconstruct.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Models::Images::WebpVp8Internal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint8_t clip(int value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

int avg2(int a, int b)
{
    return (a + b + 1) >> 1;
}

int avg3(int a, int b, int c)
{
    return (a + 2 * b + c + 2) >> 2;
}

std::array<int, 16> inverseDct(const std::array<int, 16>& input)
{
    constexpr int CosMinusOne = 20091;
    constexpr int Sin = 35468;
    std::array<int, 16> temporary {};
    std::array<int, 16> output {};

    for (std::size_t column = 0u; column < 4u; ++column) {
        const int a = input[column] + input[8u + column];
        const int b = input[column] - input[8u + column];
        const int c = ((input[4u + column] * Sin) >> 16) -
            (input[12u + column] + ((input[12u + column] * CosMinusOne) >> 16));
        const int d = (input[4u + column] + ((input[4u + column] * CosMinusOne) >> 16)) +
            ((input[12u + column] * Sin) >> 16);
        temporary[column] = a + d;
        temporary[4u + column] = b + c;
        temporary[8u + column] = b - c;
        temporary[12u + column] = a - d;
    }

    for (std::size_t row = 0u; row < 4u; ++row) {
        const std::size_t offset = row * 4u;
        const int a = temporary[offset] + temporary[offset + 2u];
        const int b = temporary[offset] - temporary[offset + 2u];
        const int c = ((temporary[offset + 1u] * Sin) >> 16) -
            (temporary[offset + 3u] + ((temporary[offset + 3u] * CosMinusOne) >> 16));
        const int d = (temporary[offset + 1u] + ((temporary[offset + 1u] * CosMinusOne) >> 16)) +
            ((temporary[offset + 3u] * Sin) >> 16);
        output[offset] = (a + d + 4) >> 3;
        output[offset + 1u] = (b + c + 4) >> 3;
        output[offset + 2u] = (b - c + 4) >> 3;
        output[offset + 3u] = (a - d + 4) >> 3;
    }
    return output;
}

std::array<int, 16> inverseWalsh(const MacroblockResidue& residue)
{
    std::array<int, 16> input {};
    for (std::size_t index = 0u; index < input.size(); ++index) {
        const int factor = index == 0u ? residue.quant.y2_dc : residue.quant.y2_ac;
        input[index] = static_cast<int>(residue.y2.value[index]) * factor;
    }

    std::array<int, 16> temporary {};
    std::array<int, 16> output {};
    for (std::size_t column = 0u; column < 4u; ++column) {
        const int a = input[column] + input[12u + column];
        const int b = input[4u + column] + input[8u + column];
        const int c = input[4u + column] - input[8u + column];
        const int d = input[column] - input[12u + column];
        temporary[column] = a + b;
        temporary[4u + column] = c + d;
        temporary[8u + column] = a - b;
        temporary[12u + column] = d - c;
    }
    for (std::size_t row = 0u; row < 4u; ++row) {
        const std::size_t offset = row * 4u;
        const int a = temporary[offset] + temporary[offset + 3u];
        const int b = temporary[offset + 1u] + temporary[offset + 2u];
        const int c = temporary[offset + 1u] - temporary[offset + 2u];
        const int d = temporary[offset] - temporary[offset + 3u];
        output[offset] = (a + b + 3) >> 3;
        output[offset + 1u] = (c + d + 3) >> 3;
        output[offset + 2u] = (a - b + 3) >> 3;
        output[offset + 3u] = (d - c + 3) >> 3;
    }
    return output;
}

std::array<int, 16> residualBlock(
    const Coefficients& coefficients,
    int dc_factor,
    int ac_factor,
    const int *dc_override)
{
    std::array<int, 16> transformed {};
    transformed[0] = dc_override ? *dc_override : static_cast<int>(coefficients.value[0]) * dc_factor;
    for (std::size_t index = 1u; index < transformed.size(); ++index)
        transformed[index] = static_cast<int>(coefficients.value[index]) * ac_factor;
    return inverseDct(transformed);
}

int planePixel(const Plane& plane, std::size_t x, std::size_t y)
{
    return plane.at(std::min(x, plane.width - 1u), std::min(y, plane.height - 1u));
}

int boundaryTopLeft(const Plane& plane, std::size_t x, std::size_t y)
{
    if (y == 0u) return 127;
    if (x == 0u) return 129;
    return planePixel(plane, x - 1u, y - 1u);
}

std::vector<std::uint8_t> predictFull(
    const Plane& plane,
    std::size_t x,
    std::size_t y,
    std::size_t size,
    YMode mode)
{
    std::vector<std::uint8_t> prediction(size * size, 128u);
    std::vector<int> above(size, 127);
    std::vector<int> left(size, 129);
    const bool has_above = y != 0u;
    const bool has_left = x != 0u;
    if (has_above)
        for (std::size_t index = 0u; index < size; ++index) above[index] = planePixel(plane, x + index, y - 1u);
    if (has_left)
        for (std::size_t index = 0u; index < size; ++index) left[index] = planePixel(plane, x - 1u, y + index);

    if (mode == YMode::Dc) {
        int value = 128;
        if (has_above && has_left) {
            int sum = 0;
            for (std::size_t index = 0u; index < size; ++index) sum += above[index] + left[index];
            value = (sum + static_cast<int>(size)) / static_cast<int>(size * 2u);
        } else if (has_above) {
            int sum = 0;
            for (int pixel : above) sum += pixel;
            value = (sum + static_cast<int>(size / 2u)) / static_cast<int>(size);
        } else if (has_left) {
            int sum = 0;
            for (int pixel : left) sum += pixel;
            value = (sum + static_cast<int>(size / 2u)) / static_cast<int>(size);
        }
        std::fill(prediction.begin(), prediction.end(), static_cast<std::uint8_t>(value));
    } else if (mode == YMode::Vertical) {
        for (std::size_t row = 0u; row < size; ++row)
            for (std::size_t column = 0u; column < size; ++column)
                prediction[row * size + column] = static_cast<std::uint8_t>(above[column]);
    } else if (mode == YMode::Horizontal) {
        for (std::size_t row = 0u; row < size; ++row)
            for (std::size_t column = 0u; column < size; ++column)
                prediction[row * size + column] = static_cast<std::uint8_t>(left[row]);
    } else if (mode == YMode::TrueMotion) {
        const int top_left = boundaryTopLeft(plane, x, y);
        for (std::size_t row = 0u; row < size; ++row)
            for (std::size_t column = 0u; column < size; ++column)
                prediction[row * size + column] = clip(left[row] + above[column] - top_left);
    }
    return prediction;
}

void addResidual(
    Plane *plane,
    std::size_t x,
    std::size_t y,
    const std::array<int, 16>& residue,
    const std::uint8_t *prediction,
    std::size_t prediction_stride)
{
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            const int predicted = prediction[row * prediction_stride + column];
            plane->at(x + column, y + row) = clip(predicted + residue[row * 4u + column]);
        }
    }
}

std::array<std::uint8_t, 16> predict4(
    const Plane& plane,
    std::size_t block_x,
    std::size_t block_y,
    std::size_t macro_x,
    std::size_t macro_y,
    std::size_t macro_columns,
    BMode mode)
{
    std::array<int, 8> a {};
    std::array<int, 4> l {};
    const std::size_t macro_origin_x = macro_x * 16u;
    const std::size_t macro_origin_y = macro_y * 16u;
    const bool top = block_y == 0u;
    const bool left_edge = block_x == 0u;
    const int p = boundaryTopLeft(plane, block_x, block_y);

    for (std::size_t index = 0u; index < 4u; ++index) {
        l[index] = left_edge ? 129 : planePixel(plane, block_x - 1u, block_y + index);
        a[index] = top ? 127 : planePixel(plane, block_x + index, block_y - 1u);
    }
    for (std::size_t index = 4u; index < 8u; ++index) {
        if (top) {
            a[index] = 127;
        } else if ((block_x - macro_origin_x) == 12u && block_y > macro_origin_y) {
            const std::size_t source_x = macro_x + 1u == macro_columns
                ? macro_origin_x + 15u
                : macro_origin_x + index + 12u;
            a[index] = planePixel(plane, source_x, macro_origin_y - 1u);
        } else {
            a[index] = planePixel(plane, block_x + index, block_y - 1u);
        }
    }

    std::array<int, 9> e {{l[3],l[2],l[1],l[0],p,a[0],a[1],a[2],a[3]}};
    std::array<int, 16> b {};
    const auto set = [&](std::size_t row, std::size_t column, int value) { b[row * 4u + column] = value; };
    const auto A2 = [](int x, int y) { return avg2(x, y); };
    const auto A3 = [](int x, int y, int z) { return avg3(x, y, z); };

    switch (mode) {
        case BMode::Dc: {
            int sum = 4;
            for (std::size_t index = 0u; index < 4u; ++index) sum += a[index] + l[index];
            b.fill(sum >> 3);
            break;
        }
        case BMode::TrueMotion:
            for (std::size_t row = 0u; row < 4u; ++row)
                for (std::size_t column = 0u; column < 4u; ++column)
                    set(row, column, clip(l[row] + a[column] - p));
            break;
        case BMode::Vertical:
            for (std::size_t column = 0u; column < 4u; ++column) {
                const int before = column == 0u ? p : a[column - 1u];
                const int after = a[column + 1u];
                const int value = A3(before, a[column], after);
                for (std::size_t row = 0u; row < 4u; ++row) set(row, column, value);
            }
            break;
        case BMode::Horizontal:
            for (std::size_t row = 0u; row < 4u; ++row) {
                const int before = row == 0u ? p : l[row - 1u];
                const int after = row == 3u ? l[3] : l[row + 1u];
                const int value = A3(before, l[row], after);
                for (std::size_t column = 0u; column < 4u; ++column) set(row, column, value);
            }
            break;
        case BMode::LeftDown:
            set(0,0,A3(a[0],a[1],a[2]));
            set(0,1,A3(a[1],a[2],a[3])); set(1,0,b[1]);
            set(0,2,A3(a[2],a[3],a[4])); set(1,1,b[2]); set(2,0,b[2]);
            set(0,3,A3(a[3],a[4],a[5])); set(1,2,b[3]); set(2,1,b[3]); set(3,0,b[3]);
            set(1,3,A3(a[4],a[5],a[6])); set(2,2,b[7]); set(3,1,b[7]);
            set(2,3,A3(a[5],a[6],a[7])); set(3,2,b[11]);
            set(3,3,A3(a[6],a[7],a[7]));
            break;
        case BMode::RightDown:
            set(3,0,A3(e[0],e[1],e[2]));
            set(3,1,A3(e[1],e[2],e[3])); set(2,0,b[13]);
            set(3,2,A3(e[2],e[3],e[4])); set(2,1,b[14]); set(1,0,b[14]);
            set(3,3,A3(e[3],e[4],e[5])); set(2,2,b[15]); set(1,1,b[15]); set(0,0,b[15]);
            set(2,3,A3(e[4],e[5],e[6])); set(1,2,b[11]); set(0,1,b[11]);
            set(1,3,A3(e[5],e[6],e[7])); set(0,2,b[7]);
            set(0,3,A3(e[6],e[7],e[8]));
            break;
        case BMode::VerticalRight:
            set(3,0,A3(e[1],e[2],e[3]));
            set(2,0,A3(e[2],e[3],e[4]));
            set(3,1,A3(e[3],e[4],e[5])); set(1,0,b[13]);
            set(2,1,A2(e[4],e[5])); set(0,0,b[9]);
            set(3,2,A3(e[4],e[5],e[6])); set(1,1,b[14]);
            set(2,2,A2(e[5],e[6])); set(0,1,b[10]);
            set(3,3,A3(e[5],e[6],e[7])); set(1,2,b[15]);
            set(2,3,A2(e[6],e[7])); set(0,2,b[11]);
            set(1,3,A3(e[6],e[7],e[8]));
            set(0,3,A2(e[7],e[8]));
            break;
        case BMode::VerticalLeft:
            set(0,0,A2(a[0],a[1]));
            set(1,0,A3(a[0],a[1],a[2]));
            set(2,0,A2(a[1],a[2])); set(0,1,b[8]);
            set(1,1,A3(a[1],a[2],a[3])); set(3,0,b[5]);
            set(2,1,A2(a[2],a[3])); set(0,2,b[9]);
            set(3,1,A3(a[2],a[3],a[4])); set(1,2,b[7]);
            set(2,2,A2(a[3],a[4])); set(0,3,b[10]);
            set(3,2,A3(a[3],a[4],a[5])); set(1,3,b[11]);
            set(2,3,A3(a[4],a[5],a[6]));
            set(3,3,A3(a[5],a[6],a[7]));
            break;
        case BMode::HorizontalDown:
            set(3,0,A2(e[0],e[1]));
            set(3,1,A3(e[0],e[1],e[2]));
            set(2,0,A2(e[1],e[2])); set(3,2,b[12]);
            set(2,1,A3(e[1],e[2],e[3])); set(3,3,b[9]);
            set(2,2,A2(e[2],e[3])); set(1,0,b[10]);
            set(2,3,A3(e[2],e[3],e[4])); set(1,1,b[11]);
            set(1,2,A2(e[3],e[4])); set(0,0,b[6]);
            set(1,3,A3(e[3],e[4],e[5])); set(0,1,b[7]);
            set(0,2,A3(e[4],e[5],e[6]));
            set(0,3,A3(e[5],e[6],e[7]));
            break;
        case BMode::HorizontalUp:
            set(0,0,A2(l[0],l[1]));
            set(0,1,A3(l[0],l[1],l[2]));
            set(0,2,A2(l[1],l[2])); set(1,0,b[2]);
            set(0,3,A3(l[1],l[2],l[3])); set(1,1,b[3]);
            set(1,2,A2(l[2],l[3])); set(2,0,b[6]);
            set(1,3,A3(l[2],l[3],l[3])); set(2,1,b[7]);
            set(2,2,l[3]); set(2,3,l[3]); set(3,0,l[3]); set(3,1,l[3]); set(3,2,l[3]); set(3,3,l[3]);
            break;
    }

    std::array<std::uint8_t, 16> result {};
    for (std::size_t index = 0u; index < result.size(); ++index) result[index] = clip(b[index]);
    return result;
}

bool reconstructLuma(
    const MacroblockMode& mode,
    const MacroblockResidue& residue,
    std::size_t macro_x,
    std::size_t macro_y,
    std::size_t macro_columns,
    Plane *plane)
{
    const std::size_t x0 = macro_x * 16u;
    const std::size_t y0 = macro_y * 16u;
    std::array<int, 16> y2 {};
    if (residue.has_y2) y2 = inverseWalsh(residue);

    if (mode.y_mode == YMode::Blocks) {
        for (std::size_t block = 0u; block < 16u; ++block) {
            const std::size_t bx = x0 + (block & 3u) * 4u;
            const std::size_t by = y0 + (block >> 2u) * 4u;
            const auto prediction = predict4(*plane, bx, by, macro_x, macro_y, macro_columns, mode.blocks[block]);
            const auto transformed = residualBlock(
                residue.y[block],
                residue.quant.y1_dc,
                residue.quant.y1_ac,
                nullptr
            );
            addResidual(plane, bx, by, transformed, prediction.data(), 4u);
        }
    } else {
        const std::vector<std::uint8_t> prediction = predictFull(*plane, x0, y0, 16u, mode.y_mode);
        for (std::size_t block = 0u; block < 16u; ++block) {
            const std::size_t bx = x0 + (block & 3u) * 4u;
            const std::size_t by = y0 + (block >> 2u) * 4u;
            const int dc = y2[block];
            const auto transformed = residualBlock(
                residue.y[block],
                residue.quant.y1_dc,
                residue.quant.y1_ac,
                &dc
            );
            const std::size_t px = (block & 3u) * 4u;
            const std::size_t py = (block >> 2u) * 4u;
            addResidual(plane, bx, by, transformed, prediction.data() + py * 16u + px, 16u);
        }
    }
    return true;
}

void reconstructChromaPlane(
    const MacroblockMode& mode,
    const std::array<Coefficients,4>& coefficients,
    const QuantFactors& quant,
    std::size_t macro_x,
    std::size_t macro_y,
    Plane *plane)
{
    const std::size_t x0 = macro_x * 8u;
    const std::size_t y0 = macro_y * 8u;
    const std::vector<std::uint8_t> prediction = predictFull(*plane, x0, y0, 8u, mode.uv_mode);
    for (std::size_t block = 0u; block < 4u; ++block) {
        const std::size_t bx = x0 + (block & 1u) * 4u;
        const std::size_t by = y0 + (block >> 1u) * 4u;
        const auto transformed = residualBlock(coefficients[block], quant.uv_dc, quant.uv_ac, nullptr);
        const std::size_t px = (block & 1u) * 4u;
        const std::size_t py = (block >> 1u) * 4u;
        addResidual(plane, bx, by, transformed, prediction.data() + py * 8u + px, 8u);
    }
}

} // namespace

bool reconstruct(
    const KeyFrame& frame,
    const MacroblockGrid& modes,
    const ResidueGrid& residue,
    YuvFrame *output,
    std::string *error)
{
    if (error) error->clear();
    if (!output || frame.width <= 0 || frame.height <= 0 ||
        modes.columns == 0u || modes.rows == 0u ||
        residue.columns != modes.columns || residue.rows != modes.rows ||
        residue.macroblocks.size() != modes.macroblocks.size())
        return fail(error, "invalid VP8 reconstruction input");

    if (modes.columns > std::numeric_limits<std::size_t>::max() / 16u ||
        modes.rows > std::numeric_limits<std::size_t>::max() / 16u)
        return fail(error, "VP8 padded dimensions overflow");
    const std::size_t y_width = modes.columns * 16u;
    const std::size_t y_height = modes.rows * 16u;
    const std::size_t uv_width = modes.columns * 8u;
    const std::size_t uv_height = modes.rows * 8u;
    if (y_width != 0u && y_height > std::numeric_limits<std::size_t>::max() / y_width)
        return fail(error, "VP8 luma allocation overflow");
    if (uv_width != 0u && uv_height > std::numeric_limits<std::size_t>::max() / uv_width)
        return fail(error, "VP8 chroma allocation overflow");

    output->width = frame.width;
    output->height = frame.height;
    output->y = {y_width, y_height, std::vector<std::uint8_t>(y_width * y_height, 127u)};
    output->u = {uv_width, uv_height, std::vector<std::uint8_t>(uv_width * uv_height, 128u)};
    output->v = {uv_width, uv_height, std::vector<std::uint8_t>(uv_width * uv_height, 128u)};

    for (std::size_t macro_y = 0u; macro_y < modes.rows; ++macro_y) {
        for (std::size_t macro_x = 0u; macro_x < modes.columns; ++macro_x) {
            const std::size_t index = macro_y * modes.columns + macro_x;
            const MacroblockMode& mode = modes.macroblocks[index];
            const MacroblockResidue& macro_residue = residue.macroblocks[index];
            if (!reconstructLuma(mode, macro_residue, macro_x, macro_y, modes.columns, &output->y))
                return fail(error, "failed to reconstruct VP8 luma macroblock");
            reconstructChromaPlane(mode, macro_residue.u, macro_residue.quant, macro_x, macro_y, &output->u);
            reconstructChromaPlane(mode, macro_residue.v, macro_residue.quant, macro_x, macro_y, &output->v);
        }
    }
    return true;
}

bool toRgba(const YuvFrame& frame, Image *image, std::string *error)
{
    if (error) error->clear();
    if (!image || frame.width <= 0 || frame.height <= 0 || frame.y.width == 0u || frame.u.width == 0u || frame.v.width == 0u)
        return fail(error, "invalid VP8 YUV frame");
    const std::size_t width = static_cast<std::size_t>(frame.width);
    const std::size_t height = static_cast<std::size_t>(frame.height);
    if (width > std::numeric_limits<std::size_t>::max() / height || width * height > std::numeric_limits<std::size_t>::max() / 4u)
        return fail(error, "VP8 RGBA dimensions overflow");

    image->width = frame.width;
    image->height = frame.height;
    image->rgba.resize(width * height * 4u);
    image->meaningful_alpha = false;
    for (std::size_t y = 0u; y < height; ++y) {
        for (std::size_t x = 0u; x < width; ++x) {
            const int luma = std::max(0, static_cast<int>(frame.y.at(x, y)) - 16);
            const int u = static_cast<int>(frame.u.at(x >> 1u, y >> 1u)) - 128;
            const int v = static_cast<int>(frame.v.at(x >> 1u, y >> 1u)) - 128;
            const int c = 298 * luma;
            const std::size_t offset = (y * width + x) * 4u;
            image->rgba[offset + 0u] = clip((c + 409 * v + 128) >> 8);
            image->rgba[offset + 1u] = clip((c - 100 * u - 208 * v + 128) >> 8);
            image->rgba[offset + 2u] = clip((c + 516 * u + 128) >> 8);
            image->rgba[offset + 3u] = 255u;
        }
    }
    return true;
}

} // namespace Models::Images::WebpVp8Internal
