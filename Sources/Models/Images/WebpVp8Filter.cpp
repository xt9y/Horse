#include "Models/Images/WebpVp8Filter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Models::Images::WebpVp8Internal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

int signedClamp(int value)
{
    return std::clamp(value, -128, 127);
}

int signedPixel(std::uint8_t value)
{
    return static_cast<int>(static_cast<std::int8_t>(value ^ 0x80u));
}

std::uint8_t unsignedPixel(int value)
{
    const std::int8_t signed_value = static_cast<std::int8_t>(signedClamp(value));
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(signed_value) ^ 0x80u);
}

bool normalMask(
    int limit,
    int boundary_limit,
    std::uint8_t p3,
    std::uint8_t p2,
    std::uint8_t p1,
    std::uint8_t p0,
    std::uint8_t q0,
    std::uint8_t q1,
    std::uint8_t q2,
    std::uint8_t q3)
{
    return std::abs(static_cast<int>(p3) - static_cast<int>(p2)) <= limit &&
        std::abs(static_cast<int>(p2) - static_cast<int>(p1)) <= limit &&
        std::abs(static_cast<int>(p1) - static_cast<int>(p0)) <= limit &&
        std::abs(static_cast<int>(q1) - static_cast<int>(q0)) <= limit &&
        std::abs(static_cast<int>(q2) - static_cast<int>(q1)) <= limit &&
        std::abs(static_cast<int>(q3) - static_cast<int>(q2)) <= limit &&
        2 * std::abs(static_cast<int>(p0) - static_cast<int>(q0)) +
            std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 <= boundary_limit;
}

bool highEdgeVariance(int threshold, std::uint8_t p1, std::uint8_t p0, std::uint8_t q0, std::uint8_t q1)
{
    return std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > threshold ||
        std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > threshold;
}

void filterFour(
    bool mask,
    bool hev,
    std::uint8_t *p1,
    std::uint8_t *p0,
    std::uint8_t *q0,
    std::uint8_t *q1)
{
    if (!mask) return;
    int ps1 = signedPixel(*p1);
    int ps0 = signedPixel(*p0);
    int qs0 = signedPixel(*q0);
    int qs1 = signedPixel(*q1);

    int filter = hev ? signedClamp(ps1 - qs1) : 0;
    filter = signedClamp(filter + 3 * (qs0 - ps0));

    const int filter1 = signedClamp(filter + 4) >> 3;
    const int filter2 = signedClamp(filter + 3) >> 3;
    qs0 = signedClamp(qs0 - filter1);
    ps0 = signedClamp(ps0 + filter2);
    *q0 = unsignedPixel(qs0);
    *p0 = unsignedPixel(ps0);

    int outer = (filter1 + 1) >> 1;
    if (hev) outer = 0;
    qs1 = signedClamp(qs1 - outer);
    ps1 = signedClamp(ps1 + outer);
    *q1 = unsignedPixel(qs1);
    *p1 = unsignedPixel(ps1);
}

void filterSix(
    bool mask,
    bool hev,
    std::uint8_t *p2,
    std::uint8_t *p1,
    std::uint8_t *p0,
    std::uint8_t *q0,
    std::uint8_t *q1,
    std::uint8_t *q2)
{
    if (!mask) return;
    int ps2 = signedPixel(*p2);
    int ps1 = signedPixel(*p1);
    int ps0 = signedPixel(*p0);
    int qs0 = signedPixel(*q0);
    int qs1 = signedPixel(*q1);
    int qs2 = signedPixel(*q2);

    int filter = signedClamp(ps1 - qs1);
    filter = signedClamp(filter + 3 * (qs0 - ps0));

    int high = hev ? filter : 0;
    const int filter1 = signedClamp(high + 4) >> 3;
    const int filter2 = signedClamp(high + 3) >> 3;
    qs0 = signedClamp(qs0 - filter1);
    ps0 = signedClamp(ps0 + filter2);

    filter = hev ? 0 : filter;
    int adjustment = signedClamp((63 + filter * 27) >> 7);
    *q0 = unsignedPixel(signedClamp(qs0 - adjustment));
    *p0 = unsignedPixel(signedClamp(ps0 + adjustment));

    adjustment = signedClamp((63 + filter * 18) >> 7);
    *q1 = unsignedPixel(signedClamp(qs1 - adjustment));
    *p1 = unsignedPixel(signedClamp(ps1 + adjustment));

    adjustment = signedClamp((63 + filter * 9) >> 7);
    *q2 = unsignedPixel(signedClamp(qs2 - adjustment));
    *p2 = unsignedPixel(signedClamp(ps2 + adjustment));
}

void simpleFilter(
    std::uint8_t *p1,
    std::uint8_t *p0,
    std::uint8_t *q0,
    std::uint8_t *q1,
    int boundary_limit)
{
    const bool mask = 2 * std::abs(static_cast<int>(*p0) - static_cast<int>(*q0)) +
        std::abs(static_cast<int>(*p1) - static_cast<int>(*q1)) / 2 <= boundary_limit;
    if (!mask) return;

    const int ps1 = signedPixel(*p1);
    int ps0 = signedPixel(*p0);
    int qs0 = signedPixel(*q0);
    const int qs1 = signedPixel(*q1);
    int filter = signedClamp(ps1 - qs1);
    filter = signedClamp(filter + 3 * (qs0 - ps0));
    const int filter1 = signedClamp(filter + 4) >> 3;
    const int filter2 = signedClamp(filter + 3) >> 3;
    qs0 = signedClamp(qs0 - filter1);
    ps0 = signedClamp(ps0 + filter2);
    *q0 = unsignedPixel(qs0);
    *p0 = unsignedPixel(ps0);
}

struct Limits {
    int level = 0;
    int limit = 0;
    int block_limit = 0;
    int macroblock_limit = 0;
    int hev = 0;
};

Limits limitsFor(const KeyFrame& frame, const MacroblockMode& mode)
{
    int level = static_cast<int>(frame.filter.level);
    if (frame.segmentation.enabled && mode.segment < frame.segmentation.filter.size()) {
        const int adjustment = frame.segmentation.filter[mode.segment];
        level = frame.segmentation.absolute ? adjustment : level + adjustment;
    }
    if (frame.filter.delta_enabled) {
        level += frame.filter.reference_delta[0];
        if (mode.y_mode == YMode::Blocks) level += frame.filter.mode_delta[0];
    }
    level = std::clamp(level, 0, 63);
    if (level == 0) return {};

    int inside = level >> (frame.filter.sharpness > 0u ? 1 : 0);
    inside >>= (frame.filter.sharpness > 4u ? 1 : 0);
    if (frame.filter.sharpness > 0u)
        inside = std::min(inside, 9 - static_cast<int>(frame.filter.sharpness));
    inside = std::max(inside, 1);

    Limits result;
    result.level = level;
    result.limit = inside;
    result.block_limit = 2 * level + inside;
    result.macroblock_limit = 2 * (level + 2) + inside;
    result.hev = level >= 40 ? 2 : (level >= 15 ? 1 : 0);
    return result;
}

void verticalNormal(Plane *plane, std::size_t x, std::size_t y, std::size_t count, const Limits& limits, bool macroblock)
{
    if (!plane || x < 4u || x + 3u >= plane->width || y + count > plane->height) return;
    const int boundary = macroblock ? limits.macroblock_limit : limits.block_limit;
    for (std::size_t row = 0u; row < count; ++row) {
        std::uint8_t *p3 = &plane->at(x - 4u, y + row);
        std::uint8_t *p2 = &plane->at(x - 3u, y + row);
        std::uint8_t *p1 = &plane->at(x - 2u, y + row);
        std::uint8_t *p0 = &plane->at(x - 1u, y + row);
        std::uint8_t *q0 = &plane->at(x + 0u, y + row);
        std::uint8_t *q1 = &plane->at(x + 1u, y + row);
        std::uint8_t *q2 = &plane->at(x + 2u, y + row);
        std::uint8_t *q3 = &plane->at(x + 3u, y + row);
        const bool mask = normalMask(limits.limit, boundary, *p3,*p2,*p1,*p0,*q0,*q1,*q2,*q3);
        const bool hev = highEdgeVariance(limits.hev, *p1,*p0,*q0,*q1);
        if (macroblock) filterSix(mask, hev, p2,p1,p0,q0,q1,q2);
        else filterFour(mask, hev, p1,p0,q0,q1);
    }
}

void horizontalNormal(Plane *plane, std::size_t x, std::size_t y, std::size_t count, const Limits& limits, bool macroblock)
{
    if (!plane || y < 4u || y + 3u >= plane->height || x + count > plane->width) return;
    const int boundary = macroblock ? limits.macroblock_limit : limits.block_limit;
    for (std::size_t column = 0u; column < count; ++column) {
        std::uint8_t *p3 = &plane->at(x + column, y - 4u);
        std::uint8_t *p2 = &plane->at(x + column, y - 3u);
        std::uint8_t *p1 = &plane->at(x + column, y - 2u);
        std::uint8_t *p0 = &plane->at(x + column, y - 1u);
        std::uint8_t *q0 = &plane->at(x + column, y + 0u);
        std::uint8_t *q1 = &plane->at(x + column, y + 1u);
        std::uint8_t *q2 = &plane->at(x + column, y + 2u);
        std::uint8_t *q3 = &plane->at(x + column, y + 3u);
        const bool mask = normalMask(limits.limit, boundary, *p3,*p2,*p1,*p0,*q0,*q1,*q2,*q3);
        const bool hev = highEdgeVariance(limits.hev, *p1,*p0,*q0,*q1);
        if (macroblock) filterSix(mask, hev, p2,p1,p0,q0,q1,q2);
        else filterFour(mask, hev, p1,p0,q0,q1);
    }
}

void verticalSimple(Plane *plane, std::size_t x, std::size_t y, std::size_t count, int boundary)
{
    if (!plane || x < 2u || x + 1u >= plane->width || y + count > plane->height) return;
    for (std::size_t row = 0u; row < count; ++row)
        simpleFilter(
            &plane->at(x - 2u, y + row),
            &plane->at(x - 1u, y + row),
            &plane->at(x + 0u, y + row),
            &plane->at(x + 1u, y + row),
            boundary
        );
}

void horizontalSimple(Plane *plane, std::size_t x, std::size_t y, std::size_t count, int boundary)
{
    if (!plane || y < 2u || y + 1u >= plane->height || x + count > plane->width) return;
    for (std::size_t column = 0u; column < count; ++column)
        simpleFilter(
            &plane->at(x + column, y - 2u),
            &plane->at(x + column, y - 1u),
            &plane->at(x + column, y + 0u),
            &plane->at(x + column, y + 1u),
            boundary
        );
}

void normalMacroblock(YuvFrame *image, const MacroblockMode& mode, std::size_t mx, std::size_t my, const Limits& limits)
{
    const std::size_t yx = mx * 16u;
    const std::size_t yy = my * 16u;
    const std::size_t ux = mx * 8u;
    const std::size_t uy = my * 8u;
    const bool skip_internal = mode.y_mode != YMode::Blocks && mode.skip_coefficients;

    if (mx != 0u) {
        verticalNormal(&image->y, yx, yy, 16u, limits, true);
        verticalNormal(&image->u, ux, uy, 8u, limits, true);
        verticalNormal(&image->v, ux, uy, 8u, limits, true);
    }
    if (!skip_internal) {
        verticalNormal(&image->y, yx + 4u, yy, 16u, limits, false);
        verticalNormal(&image->y, yx + 8u, yy, 16u, limits, false);
        verticalNormal(&image->y, yx + 12u, yy, 16u, limits, false);
        verticalNormal(&image->u, ux + 4u, uy, 8u, limits, false);
        verticalNormal(&image->v, ux + 4u, uy, 8u, limits, false);
    }
    if (my != 0u) {
        horizontalNormal(&image->y, yx, yy, 16u, limits, true);
        horizontalNormal(&image->u, ux, uy, 8u, limits, true);
        horizontalNormal(&image->v, ux, uy, 8u, limits, true);
    }
    if (!skip_internal) {
        horizontalNormal(&image->y, yx, yy + 4u, 16u, limits, false);
        horizontalNormal(&image->y, yx, yy + 8u, 16u, limits, false);
        horizontalNormal(&image->y, yx, yy + 12u, 16u, limits, false);
        horizontalNormal(&image->u, ux, uy + 4u, 8u, limits, false);
        horizontalNormal(&image->v, ux, uy + 4u, 8u, limits, false);
    }
}

void simpleMacroblock(YuvFrame *image, const MacroblockMode& mode, std::size_t mx, std::size_t my, const Limits& limits)
{
    const std::size_t x = mx * 16u;
    const std::size_t y = my * 16u;
    const bool skip_internal = mode.y_mode != YMode::Blocks && mode.skip_coefficients;
    if (mx != 0u) verticalSimple(&image->y, x, y, 16u, limits.macroblock_limit);
    if (!skip_internal) {
        verticalSimple(&image->y, x + 4u, y, 16u, limits.block_limit);
        verticalSimple(&image->y, x + 8u, y, 16u, limits.block_limit);
        verticalSimple(&image->y, x + 12u, y, 16u, limits.block_limit);
    }
    if (my != 0u) horizontalSimple(&image->y, x, y, 16u, limits.macroblock_limit);
    if (!skip_internal) {
        horizontalSimple(&image->y, x, y + 4u, 16u, limits.block_limit);
        horizontalSimple(&image->y, x, y + 8u, 16u, limits.block_limit);
        horizontalSimple(&image->y, x, y + 12u, 16u, limits.block_limit);
    }
}

} // namespace

bool filterFrame(
    const KeyFrame& frame,
    const MacroblockGrid& modes,
    YuvFrame *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image || modes.columns == 0u || modes.rows == 0u ||
        modes.macroblocks.size() != modes.columns * modes.rows ||
        image->y.width < modes.columns * 16u || image->y.height < modes.rows * 16u ||
        image->u.width < modes.columns * 8u || image->u.height < modes.rows * 8u ||
        image->v.width < modes.columns * 8u || image->v.height < modes.rows * 8u)
        return fail(error, "invalid VP8 loop-filter input");

    if (frame.filter.level == 0u && !frame.segmentation.enabled) return true;
    for (std::size_t my = 0u; my < modes.rows; ++my) {
        for (std::size_t mx = 0u; mx < modes.columns; ++mx) {
            const MacroblockMode& mode = modes.macroblocks[my * modes.columns + mx];
            const Limits limits = limitsFor(frame, mode);
            if (limits.level == 0) continue;
            if (frame.filter.simple) simpleMacroblock(image, mode, mx, my, limits);
            else normalMacroblock(image, mode, mx, my, limits);
        }
    }
    return true;
}

} // namespace Models::Images::WebpVp8Internal
