#include "Models/Images/WebpVp8Modes.hpp"

#include "Models/Images/WebpVp8Tables.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Models::Images::WebpVp8Internal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

template <std::size_t N, std::size_t P>
bool treeRead(
    BoolDecoder *decoder,
    const std::array<int, N>& tree,
    const std::array<std::uint8_t, P>& probabilities,
    unsigned int *value,
    std::string *error)
{
    if (!decoder || !value) return fail(error, "invalid VP8 tree decode output");
    std::size_t index = 0u;
    for (;;) {
        if (index + 1u >= tree.size() || index / 2u >= probabilities.size())
            return fail(error, "invalid VP8 probability tree");
        unsigned int bit = 0u;
        if (!decoder->read(probabilities[index / 2u], &bit, error)) return false;
        const int next = tree[index + (bit != 0u ? 1u : 0u)];
        if (next <= 0) {
            *value = static_cast<unsigned int>(-next);
            return true;
        }
        index = static_cast<std::size_t>(next);
    }
}

std::uint8_t segmentId(KeyFrame *frame, std::string *error)
{
    if (!frame || !frame->segmentation.enabled || !frame->segmentation.update_map) return 0u;
    unsigned int first = 0u;
    if (!frame->header.read(frame->segmentation.tree_probability[0], &first, error)) return 0xffu;
    if (first == 0u) {
        unsigned int low = 0u;
        if (!frame->header.read(frame->segmentation.tree_probability[1], &low, error)) return 0xffu;
        return static_cast<std::uint8_t>(low);
    }
    unsigned int high = 0u;
    if (!frame->header.read(frame->segmentation.tree_probability[2], &high, error)) return 0xffu;
    return static_cast<std::uint8_t>(2u + high);
}

BMode macroContext(YMode mode)
{
    switch (mode) {
        case YMode::Vertical: return BMode::Vertical;
        case YMode::Horizontal: return BMode::Horizontal;
        case YMode::TrueMotion: return BMode::TrueMotion;
        case YMode::Dc:
        case YMode::Blocks:
            return BMode::Dc;
    }
    return BMode::Dc;
}

BMode aboveContext(
    const MacroblockGrid& grid,
    const MacroblockMode& current,
    std::size_t macro_x,
    std::size_t macro_y,
    std::size_t block)
{
    const std::size_t column = block & 3u;
    const std::size_t row = block >> 2u;
    if (row != 0u) return current.blocks[block - 4u];
    if (macro_y == 0u) return BMode::Dc;
    const MacroblockMode *above = grid.at(macro_x, macro_y - 1u);
    if (!above) return BMode::Dc;
    return above->y_mode == YMode::Blocks ? above->blocks[12u + column] : macroContext(above->y_mode);
}

BMode leftContext(
    const MacroblockGrid& grid,
    const MacroblockMode& current,
    std::size_t macro_x,
    std::size_t macro_y,
    std::size_t block)
{
    const std::size_t column = block & 3u;
    const std::size_t row = block >> 2u;
    if (column != 0u) return current.blocks[block - 1u];
    if (macro_x == 0u) return BMode::Dc;
    const MacroblockMode *left = grid.at(macro_x - 1u, macro_y);
    if (!left) return BMode::Dc;
    return left->y_mode == YMode::Blocks ? left->blocks[row * 4u + 3u] : macroContext(left->y_mode);
}

bool decodeYMode(KeyFrame *frame, YMode *mode, std::string *error)
{
    static constexpr std::array<int, 8> tree {{-4,2,4,6,0,-1,-2,-3}};
    unsigned int value = 0u;
    if (!treeRead(&frame->header, tree, KeyFrameYModeProbabilities, &value, error)) return false;
    if (value > static_cast<unsigned int>(YMode::Blocks)) return fail(error, "invalid VP8 luma mode");
    *mode = static_cast<YMode>(value);
    return true;
}

bool decodeUvMode(KeyFrame *frame, YMode *mode, std::string *error)
{
    static constexpr std::array<int, 6> tree {{0,2,-1,4,-2,-3}};
    unsigned int value = 0u;
    if (!treeRead(&frame->header, tree, KeyFrameUvModeProbabilities, &value, error)) return false;
    if (value > static_cast<unsigned int>(YMode::TrueMotion)) return fail(error, "invalid VP8 chroma mode");
    *mode = static_cast<YMode>(value);
    return true;
}

bool decodeBMode(
    KeyFrame *frame,
    BMode above,
    BMode left,
    BMode *mode,
    std::string *error)
{
    static constexpr std::array<int, 18> tree {{
        0,2,-1,4,-2,6,8,12,-3,10,-5,-6,-4,14,-7,16,-8,-9,
    }};
    const std::size_t a = static_cast<std::size_t>(above);
    const std::size_t l = static_cast<std::size_t>(left);
    if (a >= KeyFrameBModeProbabilities.size() || l >= KeyFrameBModeProbabilities[a].size())
        return fail(error, "invalid VP8 subblock mode context");
    unsigned int value = 0u;
    if (!treeRead(&frame->header, tree, KeyFrameBModeProbabilities[a][l], &value, error)) return false;
    if (value > static_cast<unsigned int>(BMode::HorizontalUp)) return fail(error, "invalid VP8 subblock mode");
    *mode = static_cast<BMode>(value);
    return true;
}

} // namespace

bool decodeMacroblockModes(KeyFrame *frame, MacroblockGrid *grid, std::string *error)
{
    if (error) error->clear();
    if (!frame || !grid || frame->width <= 0 || frame->height <= 0)
        return fail(error, "invalid VP8 macroblock grid output");
    const std::size_t width = static_cast<std::size_t>(frame->width);
    const std::size_t height = static_cast<std::size_t>(frame->height);
    const std::size_t columns = (width + 15u) / 16u;
    const std::size_t rows = (height + 15u) / 16u;
    if (columns != 0u && rows > std::numeric_limits<std::size_t>::max() / columns)
        return fail(error, "VP8 macroblock grid dimensions overflow");

    grid->columns = columns;
    grid->rows = rows;
    grid->macroblocks.assign(columns * rows, {});

    for (std::size_t y = 0u; y < rows; ++y) {
        for (std::size_t x = 0u; x < columns; ++x) {
            MacroblockMode& macro = grid->macroblocks[y * columns + x];
            if (frame->segmentation.enabled && frame->segmentation.update_map) {
                const std::uint8_t decoded = segmentId(frame, error);
                if (decoded == 0xffu) return false;
                macro.segment = decoded;
            }
            if (frame->coefficient_skip_enabled) {
                unsigned int skip = 0u;
                if (!frame->header.read(frame->coefficient_skip_probability, &skip, error)) return false;
                macro.skip_coefficients = skip != 0u;
            }
            if (!decodeYMode(frame, &macro.y_mode, error)) return false;
            if (macro.y_mode == YMode::Blocks) {
                for (std::size_t block = 0u; block < macro.blocks.size(); ++block) {
                    const BMode above = aboveContext(*grid, macro, x, y, block);
                    const BMode left = leftContext(*grid, macro, x, y, block);
                    if (!decodeBMode(frame, above, left, &macro.blocks[block], error)) return false;
                }
            } else {
                macro.blocks.fill(macroContext(macro.y_mode));
            }
            if (!decodeUvMode(frame, &macro.uv_mode, error)) return false;
        }
    }
    return true;
}

} // namespace Models::Images::WebpVp8Internal
