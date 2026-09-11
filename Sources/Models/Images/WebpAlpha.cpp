#include "Models/Images/WebpAlpha.hpp"

#include "Models/Images/WebpLossless.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Models::Images::WebpAlpha {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint8_t clippedGradient(std::uint8_t left, std::uint8_t above, std::uint8_t upper_left)
{
    const int value = static_cast<int>(left) + static_cast<int>(above) - static_cast<int>(upper_left);
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    int width,
    int height,
    std::vector<std::uint8_t> *alpha,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !alpha || size < 1u || width <= 0 || height <= 0)
        return fail(error, "invalid WebP ALPH input");
    if (static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height))
        return fail(error, "WebP ALPH dimensions overflow");
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    const std::uint8_t header = data[0];
    const unsigned int compression = header & 3u;
    const unsigned int filtering = (header >> 2u) & 3u;
    const unsigned int preprocessing = (header >> 4u) & 3u;
    const unsigned int reserved = header >> 6u;
    if (reserved != 0u) return fail(error, "WebP ALPH reserved bits are nonzero");
    if (compression > 1u) return fail(error, "unsupported WebP ALPH compression method");
    if (preprocessing > 1u) return fail(error, "reserved WebP ALPH preprocessing method");
    (void)preprocessing;

    std::vector<std::uint8_t> residual;
    if (compression == 0u) {
        if (size - 1u != pixels) return fail(error, "raw WebP ALPH payload size does not match image dimensions");
        residual.assign(data + 1u, data + size);
    } else {
        std::vector<std::uint32_t> argb;
        if (!WebpLossless::decodeStream(data + 1u, size - 1u, width, height, &argb, error)) return false;
        if (argb.size() != pixels) return fail(error, "compressed WebP ALPH output size mismatch");
        residual.resize(pixels);
        for (std::size_t index = 0u; index < pixels; ++index)
            residual[index] = static_cast<std::uint8_t>((argb[index] >> 8u) & 0xffu);
    }

    alpha->assign(pixels, 0u);
    const std::size_t stride = static_cast<std::size_t>(width);
    for (std::size_t y = 0u; y < static_cast<std::size_t>(height); ++y) {
        for (std::size_t x = 0u; x < stride; ++x) {
            const std::size_t index = y * stride + x;
            std::uint8_t predictor = 0u;
            if (filtering == 1u) {
                if (x != 0u) predictor = (*alpha)[index - 1u];
                else if (y != 0u) predictor = (*alpha)[index - stride];
            } else if (filtering == 2u) {
                if (y != 0u) predictor = (*alpha)[index - stride];
                else if (x != 0u) predictor = (*alpha)[index - 1u];
            } else if (filtering == 3u) {
                if (x != 0u && y != 0u) {
                    predictor = clippedGradient(
                        (*alpha)[index - 1u],
                        (*alpha)[index - stride],
                        (*alpha)[index - stride - 1u]
                    );
                } else if (x != 0u) {
                    predictor = (*alpha)[index - 1u];
                } else if (y != 0u) {
                    predictor = (*alpha)[index - stride];
                }
            }
            (*alpha)[index] = static_cast<std::uint8_t>(
                static_cast<unsigned int>(predictor) + static_cast<unsigned int>(residual[index])
            );
        }
    }
    return true;
}

} // namespace Models::Images::WebpAlpha
