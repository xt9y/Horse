#include "Models/Images/WebpVp8Residue.hpp"

#include "Models/Images/WebpVp8Bits.hpp"
#include "Models/Images/WebpVp8Tables.hpp"

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

constexpr std::array<int, 22> CoeffTree {{
    -11,2,
    0,4,
    -1,6,
    8,12,
    -2,10,
    -3,-4,
    14,16,
    -5,-6,
    18,20,
    -7,-8,
    -9,-10,
}};

bool readToken(
    BoolDecoder *decoder,
    const CoeffNode& probabilities,
    bool skip_eob,
    unsigned int *token,
    std::string *error)
{
    if (!decoder || !token) return fail(error, "invalid VP8 coefficient token output");
    std::size_t index = skip_eob ? 2u : 0u;
    for (;;) {
        if (index + 1u >= CoeffTree.size() || index / 2u >= probabilities.size())
            return fail(error, "invalid VP8 coefficient token tree");
        unsigned int bit = 0u;
        if (!decoder->read(probabilities[index / 2u], &bit, error)) return false;
        const int next = CoeffTree[index + (bit != 0u ? 1u : 0u)];
        if (next <= 0) {
            *token = static_cast<unsigned int>(-next);
            return true;
        }
        index = static_cast<std::size_t>(next);
    }
}

bool extraValue(BoolDecoder *decoder, unsigned int token, int *value, std::string *error)
{
    if (!decoder || !value) return fail(error, "invalid VP8 coefficient value output");
    if (token == 1u) {
        *value = 1;
    } else if (token >= 2u && token <= 4u) {
        *value = static_cast<int>(token);
    } else if (token >= 5u && token <= 10u) {
        const ExtraBits& extra = CoeffExtraBits[token - 5u];
        unsigned int suffix = 0u;
        for (std::size_t bit_index = 0u; bit_index < extra.count; ++bit_index) {
            unsigned int bit = 0u;
            if (!decoder->read(extra.probabilities[bit_index], &bit, error)) return false;
            suffix = (suffix << 1u) | bit;
        }
        *value = extra.minimum + static_cast<int>(suffix);
    } else {
        return fail(error, "invalid VP8 nonzero coefficient token");
    }

    unsigned int sign = 0u;
    if (!decoder->read(128u, &sign, error)) return false;
    if (sign != 0u) *value = -*value;
    return true;
}

bool decodeBlock(
    BoolDecoder *decoder,
    const CoeffProbabilities& probabilities,
    std::size_t type,
    std::size_t start,
    std::uint8_t above,
    std::uint8_t left,
    Coefficients *coefficients,
    std::uint8_t *nonzero_context,
    std::string *error)
{
    if (!decoder || !coefficients || !nonzero_context || type >= probabilities.size() || start > 15u)
        return fail(error, "invalid VP8 coefficient block request");
    coefficients->value.fill(0);
    coefficients->nonzero = false;
    unsigned int context = static_cast<unsigned int>(above != 0u) + static_cast<unsigned int>(left != 0u);
    bool after_zero = false;

    for (std::size_t coefficient = start; coefficient < 16u; ++coefficient) {
        const std::size_t band = CoeffBands[coefficient];
        if (band >= probabilities[type].size() || context >= probabilities[type][band].size())
            return fail(error, "invalid VP8 coefficient context");
        unsigned int token = 0u;
        if (!readToken(decoder, probabilities[type][band][context], after_zero, &token, error)) return false;
        if (token == 11u) break;
        if (token == 0u) {
            context = 0u;
            after_zero = true;
            continue;
        }

        int decoded = 0;
        if (!extraValue(decoder, token, &decoded, error)) return false;
        const std::size_t destination = ZigZag[coefficient];
        coefficients->value[destination] = static_cast<std::int16_t>(decoded);
        coefficients->nonzero = true;
        context = token == 1u ? 1u : 2u;
        after_zero = false;
    }
    *nonzero_context = coefficients->nonzero ? 1u : 0u;
    return true;
}

int quantIndex(const KeyFrame& frame, std::uint8_t segment)
{
    int value = static_cast<int>(frame.quantizer.base);
    if (frame.segmentation.enabled && segment < frame.segmentation.quantizer.size()) {
        const int adjustment = frame.segmentation.quantizer[segment];
        value = frame.segmentation.absolute ? adjustment : value + adjustment;
    }
    return std::clamp(value, 0, 127);
}

int dcQuant(int index, int delta)
{
    return DcQuant[static_cast<std::size_t>(std::clamp(index + delta, 0, 127))];
}

int acQuant(int index, int delta)
{
    return AcQuant[static_cast<std::size_t>(std::clamp(index + delta, 0, 127))];
}

QuantFactors factors(const KeyFrame& frame, std::uint8_t segment)
{
    const int q = quantIndex(frame, segment);
    QuantFactors result;
    result.y1_dc = dcQuant(q, frame.quantizer.y1_dc);
    result.y1_ac = acQuant(q, 0);
    result.y2_dc = dcQuant(q, frame.quantizer.y2_dc) * 2;
    result.y2_ac = std::max(8, (acQuant(q, frame.quantizer.y2_ac) * 101581) >> 16);
    result.uv_dc = std::min(132, dcQuant(q, frame.quantizer.uv_dc));
    result.uv_ac = acQuant(q, frame.quantizer.uv_ac);
    return result;
}

struct NeighborContexts {
    std::array<std::uint8_t, 4> y {{0,0,0,0}};
    std::array<std::uint8_t, 2> u {{0,0}};
    std::array<std::uint8_t, 2> v {{0,0}};
    std::uint8_t y2 = 0u;
};

void clearSkipped(NeighborContexts *above, NeighborContexts *left)
{
    if (above) *above = {};
    if (left) *left = {};
}

bool decodePlane2x2(
    BoolDecoder *decoder,
    const CoeffProbabilities& probabilities,
    std::array<std::uint8_t, 2> *above,
    std::array<std::uint8_t, 2> *left,
    std::array<Coefficients, 4> *blocks,
    std::string *error)
{
    if (!decoder || !above || !left || !blocks) return fail(error, "invalid VP8 chroma residue request");
    std::array<std::uint8_t, 4> contexts {{0,0,0,0}};
    for (std::size_t block = 0u; block < 4u; ++block) {
        const std::size_t row = block >> 1u;
        const std::size_t column = block & 1u;
        const std::uint8_t a = row == 0u ? (*above)[column] : contexts[block - 2u];
        const std::uint8_t l = column == 0u ? (*left)[row] : contexts[block - 1u];
        if (!decodeBlock(decoder, probabilities, 2u, 0u, a, l, &(*blocks)[block], &contexts[block], error))
            return false;
    }
    (*above)[0] = contexts[2];
    (*above)[1] = contexts[3];
    (*left)[0] = contexts[1];
    (*left)[1] = contexts[3];
    return true;
}

} // namespace

bool decodeResidue(
    const KeyFrame& frame,
    const MacroblockGrid& modes,
    ResidueGrid *residue,
    std::string *error)
{
    if (error) error->clear();
    if (!residue || modes.columns == 0u || modes.rows == 0u ||
        modes.macroblocks.size() != modes.columns * modes.rows || frame.token_partitions.empty())
        return fail(error, "invalid VP8 residue grid input");

    std::vector<BoolDecoder> token_decoders(frame.token_partitions.size());
    for (std::size_t index = 0u; index < frame.token_partitions.size(); ++index) {
        const TokenPartition& partition = frame.token_partitions[index];
        if (!token_decoders[index].reset(partition.data, partition.size, error)) return false;
    }

    residue->columns = modes.columns;
    residue->rows = modes.rows;
    residue->macroblocks.assign(modes.macroblocks.size(), {});
    std::vector<NeighborContexts> above(modes.columns);

    for (std::size_t y = 0u; y < modes.rows; ++y) {
        NeighborContexts left;
        BoolDecoder& decoder = token_decoders[y % token_decoders.size()];
        for (std::size_t x = 0u; x < modes.columns; ++x) {
            const MacroblockMode& mode = modes.macroblocks[y * modes.columns + x];
            MacroblockResidue& output = residue->macroblocks[y * modes.columns + x];
            output.quant = factors(frame, mode.segment);
            output.has_y2 = mode.y_mode != YMode::Blocks;

            if (mode.skip_coefficients) {
                clearSkipped(&above[x], &left);
                continue;
            }

            if (output.has_y2) {
                std::uint8_t context = 0u;
                if (!decodeBlock(
                        &decoder,
                        frame.coefficient_probabilities,
                        1u,
                        0u,
                        above[x].y2,
                        left.y2,
                        &output.y2,
                        &context,
                        error))
                    return false;
                above[x].y2 = context;
                left.y2 = context;
            } else {
                above[x].y2 = 0u;
                left.y2 = 0u;
            }

            std::array<std::uint8_t, 16> y_contexts {};
            for (std::size_t block = 0u; block < output.y.size(); ++block) {
                const std::size_t row = block >> 2u;
                const std::size_t column = block & 3u;
                const std::uint8_t a = row == 0u ? above[x].y[column] : y_contexts[block - 4u];
                const std::uint8_t l = column == 0u ? left.y[row] : y_contexts[block - 1u];
                const std::size_t type = output.has_y2 ? 0u : 3u;
                const std::size_t start = output.has_y2 ? 1u : 0u;
                if (!decodeBlock(
                        &decoder,
                        frame.coefficient_probabilities,
                        type,
                        start,
                        a,
                        l,
                        &output.y[block],
                        &y_contexts[block],
                        error))
                    return false;
            }
            for (std::size_t column = 0u; column < 4u; ++column) above[x].y[column] = y_contexts[12u + column];
            for (std::size_t row = 0u; row < 4u; ++row) left.y[row] = y_contexts[row * 4u + 3u];

            if (!decodePlane2x2(
                    &decoder,
                    frame.coefficient_probabilities,
                    &above[x].u,
                    &left.u,
                    &output.u,
                    error) ||
                !decodePlane2x2(
                    &decoder,
                    frame.coefficient_probabilities,
                    &above[x].v,
                    &left.v,
                    &output.v,
                    error))
                return false;
        }
    }
    return true;
}

} // namespace Models::Images::WebpVp8Internal
