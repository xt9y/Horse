#include "Models/Compression/ZstdBlock.hpp"

#include "Models/Compression/ZstdBits.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace Models::Compression::ZstdInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint16_t u16le(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u)
    );
}

std::uint32_t u24le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u);
}

std::uint32_t u32le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

bool appendLiteralRange(
    const std::vector<std::uint8_t>& literals,
    std::size_t *literal_cursor,
    std::size_t count,
    std::size_t block_begin,
    std::size_t block_maximum,
    std::size_t output_maximum,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!literal_cursor || !output || *literal_cursor > literals.size() || count > literals.size() - *literal_cursor)
        return fail(error, "Zstd sequence consumes more literals than available");
    if (count > output_maximum - std::min(output->size(), output_maximum))
        return fail(error, "Zstd output exceeds configured limit");
    if (output->size() < block_begin || count > block_maximum - std::min(output->size() - block_begin, block_maximum))
        return fail(error, "Zstd compressed block exceeds block maximum");
    output->insert(
        output->end(),
        literals.begin() + static_cast<std::ptrdiff_t>(*literal_cursor),
        literals.begin() + static_cast<std::ptrdiff_t>(*literal_cursor + count)
    );
    *literal_cursor += count;
    return true;
}

bool copyMatch(
    std::uint32_t offset,
    std::size_t length,
    std::size_t block_begin,
    std::size_t block_maximum,
    std::size_t output_maximum,
    const BlockState& state,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!output || offset == 0u) return fail(error, "Zstd match offset is zero");
    if (offset > state.window_size) return fail(error, "Zstd match offset exceeds frame window");
    if (output->size() < state.frame_output_begin ||
        static_cast<std::size_t>(offset) > output->size() - state.frame_output_begin)
        return fail(error, "Zstd match offset precedes frame history");
    if (length > output_maximum - std::min(output->size(), output_maximum))
        return fail(error, "Zstd output exceeds configured limit");
    if (output->size() < block_begin || length > block_maximum - std::min(output->size() - block_begin, block_maximum))
        return fail(error, "Zstd compressed block exceeds block maximum");

    for (std::size_t index = 0u; index < length; ++index) {
        const std::size_t source = output->size() - static_cast<std::size_t>(offset);
        output->push_back((*output)[source]);
    }
    return true;
}

struct LiteralSection {
    std::vector<std::uint8_t> bytes;
    std::size_t consumed = 0u;
};

bool decodeFourHuffmanStreams(
    const HuffmanTable& table,
    const std::uint8_t *data,
    std::size_t size,
    std::size_t regenerated,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!data || !output || size < 6u) return fail(error, "truncated four-stream Zstd Huffman literals");
    const std::size_t size1 = u16le(data + 0u);
    const std::size_t size2 = u16le(data + 2u);
    const std::size_t size3 = u16le(data + 4u);
    if (size1 > size - 6u || size2 > size - 6u - size1 || size3 > size - 6u - size1 - size2)
        return fail(error, "Zstd Huffman jump table exceeds literals payload");
    const std::size_t size4 = size - 6u - size1 - size2 - size3;
    if (size1 == 0u || size2 == 0u || size3 == 0u || size4 == 0u)
        return fail(error, "Zstd four-stream Huffman stream is empty");

    const std::size_t segment = (regenerated + 3u) / 4u;
    if (segment > regenerated || segment * 3u > regenerated)
        return fail(error, "invalid Zstd four-stream regenerated size");
    const std::array<std::size_t, 4> regenerated_sizes {{
        segment,
        segment,
        segment,
        regenerated - segment * 3u,
    }};
    const std::array<std::size_t, 4> compressed_sizes {{size1, size2, size3, size4}};

    output->clear();
    output->reserve(regenerated);
    std::size_t offset = 6u;
    for (std::size_t stream = 0u; stream < 4u; ++stream) {
        std::vector<std::uint8_t> decoded;
        if (!decodeHuffmanStream(
                table,
                data + offset,
                compressed_sizes[stream],
                regenerated_sizes[stream],
                &decoded,
                error))
            return false;
        output->insert(output->end(), decoded.begin(), decoded.end());
        offset += compressed_sizes[stream];
    }
    return output->size() == regenerated;
}

bool decodeLiterals(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t block_maximum,
    BlockState *state,
    LiteralSection *section,
    std::string *error)
{
    if (!data || !state || !section || size == 0u) return fail(error, "truncated Zstd literals section");
    const unsigned int type = data[0] & 3u;
    const unsigned int size_format = (data[0] >> 2u) & 3u;

    std::size_t header_size = 0u;
    std::size_t regenerated = 0u;
    std::size_t compressed = 0u;
    bool single_stream = true;

    if (type <= 1u) {
        if (size_format == 0u || size_format == 2u) {
            header_size = 1u;
            regenerated = data[0] >> 3u;
        } else if (size_format == 1u) {
            if (size < 2u) return fail(error, "truncated Zstd literals header");
            header_size = 2u;
            regenerated = u16le(data) >> 4u;
        } else {
            if (size < 3u) return fail(error, "truncated Zstd literals header");
            header_size = 3u;
            regenerated = u24le(data) >> 4u;
        }
        if (regenerated > block_maximum) return fail(error, "Zstd literals exceed block maximum");
        if (type == 0u) {
            if (regenerated > size - header_size) return fail(error, "truncated raw Zstd literals");
            section->bytes.assign(data + header_size, data + header_size + regenerated);
            section->consumed = header_size + regenerated;
            return true;
        }
        if (header_size >= size) return fail(error, "truncated RLE Zstd literal byte");
        section->bytes.assign(regenerated, data[header_size]);
        section->consumed = header_size + 1u;
        return true;
    }

    if (size < 3u) return fail(error, "truncated compressed Zstd literals header");
    const std::uint32_t low = u32le(data);
    if (size_format == 0u || size_format == 1u) {
        header_size = 3u;
        single_stream = size_format == 0u;
        regenerated = (low >> 4u) & 0x3ffu;
        compressed = (low >> 14u) & 0x3ffu;
    } else if (size_format == 2u) {
        if (size < 4u) return fail(error, "truncated compressed Zstd literals header");
        header_size = 4u;
        single_stream = false;
        regenerated = (low >> 4u) & 0x3fffu;
        compressed = low >> 18u;
    } else {
        if (size < 5u) return fail(error, "truncated compressed Zstd literals header");
        header_size = 5u;
        single_stream = false;
        regenerated = (low >> 4u) & 0x3ffffu;
        compressed = (low >> 22u) + (static_cast<std::size_t>(data[4]) << 10u);
    }
    if (regenerated > block_maximum) return fail(error, "Zstd compressed literals exceed block maximum");
    if (compressed == 0u || compressed > size - header_size)
        return fail(error, "Zstd compressed literals payload exceeds block");

    const std::uint8_t *payload = data + header_size;
    std::size_t stream_offset = 0u;
    if (type == 2u) {
        std::size_t tree_bytes = 0u;
        if (!parseHuffmanTable(payload, compressed, &state->huffman, &tree_bytes, error)) return false;
        if (tree_bytes >= compressed) return fail(error, "Zstd Huffman tree consumes complete literal payload");
        stream_offset = tree_bytes;
    } else {
        if (!state->huffman.valid()) return fail(error, "Zstd treeless literals have no previous Huffman table");
    }

    const std::uint8_t *stream_data = payload + stream_offset;
    const std::size_t stream_size = compressed - stream_offset;
    if (single_stream) {
        if (!decodeHuffmanStream(state->huffman, stream_data, stream_size, regenerated, &section->bytes, error))
            return false;
    } else {
        if (!decodeFourHuffmanStreams(state->huffman, stream_data, stream_size, regenerated, &section->bytes, error))
            return false;
    }
    section->consumed = header_size + compressed;
    return true;
}

const std::vector<std::int16_t>& defaultLiteralLengths()
{
    static const std::vector<std::int16_t> value {
        4,3,2,2,2,2,2,2,2,2,2,2,2,1,1,1,
        2,2,2,2,2,2,2,2,2,3,2,1,1,1,1,1,
        -1,-1,-1,-1,
    };
    return value;
}

const std::vector<std::int16_t>& defaultMatchLengths()
{
    static const std::vector<std::int16_t> value {
        1,4,3,2,2,2,2,2,2,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,-1,-1,
        -1,-1,-1,-1,-1,
    };
    return value;
}

const std::vector<std::int16_t>& defaultOffsets()
{
    static const std::vector<std::int16_t> value {
        1,1,1,1,1,1,2,2,2,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,-1,-1,-1,-1,-1,
    };
    return value;
}

bool sequenceTable(
    unsigned int mode,
    const std::vector<std::int16_t>& predefined,
    std::uint8_t predefined_log,
    std::uint16_t maximum_symbol,
    std::uint8_t maximum_log,
    const std::uint8_t *data,
    std::size_t size,
    SequenceTable *table,
    std::size_t *consumed,
    std::string *error)
{
    if (!table || !consumed) return fail(error, "invalid Zstd sequence table output");
    *consumed = 0u;
    if (mode == 0u) {
        if (!buildFseTable(predefined, predefined_log, &table->fse, error)) return false;
        table->rle = false;
        table->valid = true;
        return true;
    }
    if (mode == 1u) {
        if (size == 0u) return fail(error, "truncated Zstd sequence RLE table");
        if (data[0] > maximum_symbol) return fail(error, "Zstd sequence RLE symbol exceeds allowed maximum");
        table->repeated_symbol = data[0];
        table->rle = true;
        table->valid = true;
        *consumed = 1u;
        return true;
    }
    if (mode == 2u) {
        if (!parseFseTable(data, size, maximum_symbol, maximum_log, &table->fse, consumed, error)) return false;
        table->rle = false;
        table->valid = true;
        return true;
    }
    if (!table->valid) return fail(error, "Zstd Repeat_Mode sequence table has no previous table");
    return true;
}

bool sequenceCount(const std::uint8_t *data, std::size_t size, std::size_t *count, std::size_t *consumed, std::string *error)
{
    if (!data || !count || !consumed || size == 0u) return fail(error, "truncated Zstd sequence count");
    const std::uint8_t first = data[0];
    if (first < 128u) {
        *count = first;
        *consumed = 1u;
        return true;
    }
    if (first < 255u) {
        if (size < 2u) return fail(error, "truncated two-byte Zstd sequence count");
        *count = (static_cast<std::size_t>(first - 0x80u) << 8u) + data[1];
        *consumed = 2u;
        return true;
    }
    if (size < 3u) return fail(error, "truncated three-byte Zstd sequence count");
    *count = static_cast<std::size_t>(data[1]) +
        (static_cast<std::size_t>(data[2]) << 8u) + 0x7f00u;
    *consumed = 3u;
    return true;
}

bool codeValue(
    std::uint16_t code,
    bool match,
    std::uint32_t *baseline,
    unsigned int *bits,
    std::string *error)
{
    if (!baseline || !bits) return fail(error, "invalid Zstd length code output");
    if (!match) {
        if (code <= 15u) { *baseline = code; *bits = 0u; return true; }
        static constexpr std::array<std::uint32_t, 20> bases {{
            16,18,20,22,24,28,32,40,48,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,
        }};
        static constexpr std::array<unsigned int, 20> widths {{
            1,1,1,1,2,2,3,3,4,6,7,8,9,10,11,12,13,14,15,16,
        }};
        if (code < 16u || code > 35u) return fail(error, "invalid Zstd literals length code");
        *baseline = bases[code - 16u];
        *bits = widths[code - 16u];
        return true;
    }

    if (code <= 31u) { *baseline = static_cast<std::uint32_t>(code) + 3u; *bits = 0u; return true; }
    static constexpr std::array<std::uint32_t, 21> bases {{
        35,37,39,41,43,47,51,59,67,83,99,131,259,515,1027,2051,4099,8195,16387,32771,65539,
    }};
    static constexpr std::array<unsigned int, 21> widths {{
        1,1,1,1,2,2,3,3,4,4,5,7,8,9,10,11,12,13,14,15,16,
    }};
    if (code < 32u || code > 52u) return fail(error, "invalid Zstd match length code");
    *baseline = bases[code - 32u];
    *bits = widths[code - 32u];
    return true;
}

bool currentSymbol(const SequenceTable& table, const FseState& state, std::uint16_t *symbol, std::string *error)
{
    if (!symbol || !table.valid) return fail(error, "invalid Zstd sequence table state");
    if (table.rle) {
        *symbol = table.repeated_symbol;
        return true;
    }
    return fseSymbol(state, symbol, error);
}

bool initializeSequenceState(const SequenceTable& table, ReverseBits *bits, FseState *state, std::string *error)
{
    if (!table.valid) return fail(error, "invalid Zstd sequence table");
    if (table.rle) {
        state->table = nullptr;
        state->state = 0u;
        return true;
    }
    return initializeFseState(table.fse, bits, state, error);
}

bool updateSequenceState(const SequenceTable& table, FseState *state, ReverseBits *bits, std::string *error)
{
    return table.rle ? true : updateFseState(state, bits, error);
}

bool resolveOffset(
    std::uint32_t offset_value,
    std::size_t literal_length,
    BlockState *state,
    std::uint32_t *offset,
    std::string *error)
{
    if (!state || !offset || offset_value == 0u) return fail(error, "invalid Zstd offset value");
    auto& repeat = state->repeated_offsets;
    if (offset_value > 3u) {
        *offset = offset_value - 3u;
        repeat = {*offset, repeat[0], repeat[1]};
        return *offset != 0u;
    }

    if (literal_length == 0u) {
        if (offset_value == 1u) {
            *offset = repeat[1];
            std::swap(repeat[0], repeat[1]);
            return true;
        }
        if (offset_value == 2u) {
            *offset = repeat[2];
            repeat = {*offset, repeat[0], repeat[1]};
            return true;
        }
        if (repeat[0] <= 1u) return fail(error, "Zstd repeat offset minus one is zero");
        *offset = repeat[0] - 1u;
        repeat = {*offset, repeat[0], repeat[1]};
        return true;
    }

    if (offset_value == 1u) {
        *offset = repeat[0];
        return true;
    }
    if (offset_value == 2u) {
        *offset = repeat[1];
        std::swap(repeat[0], repeat[1]);
        return true;
    }
    *offset = repeat[2];
    repeat = {*offset, repeat[0], repeat[1]};
    return true;
}

} // namespace

bool decodeCompressedBlock(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t block_maximum,
    std::size_t output_maximum,
    BlockState *state,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!data || !state || !output || size == 0u) return fail(error, "invalid Zstd compressed block input");
    const std::size_t block_begin = output->size();
    LiteralSection literals;
    if (!decodeLiterals(data, size, block_maximum, state, &literals, error)) return false;
    if (literals.consumed >= size) return fail(error, "Zstd compressed block has no sequence section");

    const std::uint8_t *sequence_data = data + literals.consumed;
    const std::size_t sequence_size = size - literals.consumed;
    std::size_t number_sequences = 0u;
    std::size_t cursor = 0u;
    if (!sequenceCount(sequence_data, sequence_size, &number_sequences, &cursor, error)) return false;
    if (number_sequences == 0u) {
        if (cursor != sequence_size) return fail(error, "Zstd zero-sequence block has trailing sequence data");
        std::size_t literal_cursor = 0u;
        return appendLiteralRange(
            literals.bytes,
            &literal_cursor,
            literals.bytes.size(),
            block_begin,
            block_maximum,
            output_maximum,
            output,
            error
        );
    }

    if (cursor >= sequence_size) return fail(error, "truncated Zstd sequence mode byte");
    const std::uint8_t modes = sequence_data[cursor++];
    if ((modes & 3u) != 0u) return fail(error, "reserved Zstd sequence mode bits are nonzero");
    const unsigned int literal_mode = modes >> 6u;
    const unsigned int offset_mode = (modes >> 4u) & 3u;
    const unsigned int match_mode = (modes >> 2u) & 3u;

    std::size_t table_bytes = 0u;
    if (!sequenceTable(
            literal_mode,
            defaultLiteralLengths(),
            6u,
            35u,
            9u,
            sequence_data + cursor,
            sequence_size - cursor,
            &state->literal_lengths,
            &table_bytes,
            error))
        return false;
    cursor += table_bytes;

    if (!sequenceTable(
            offset_mode,
            defaultOffsets(),
            5u,
            31u,
            8u,
            sequence_data + cursor,
            sequence_size - cursor,
            &state->offsets,
            &table_bytes,
            error))
        return false;
    cursor += table_bytes;

    if (!sequenceTable(
            match_mode,
            defaultMatchLengths(),
            6u,
            52u,
            9u,
            sequence_data + cursor,
            sequence_size - cursor,
            &state->match_lengths,
            &table_bytes,
            error))
        return false;
    cursor += table_bytes;
    if (cursor >= sequence_size) return fail(error, "Zstd sequence bitstream is empty");

    ReverseBits bits;
    if (!bits.reset(sequence_data + cursor, sequence_size - cursor, error)) return false;
    FseState literal_state;
    FseState offset_state;
    FseState match_state;
    if (!initializeSequenceState(state->literal_lengths, &bits, &literal_state, error) ||
        !initializeSequenceState(state->offsets, &bits, &offset_state, error) ||
        !initializeSequenceState(state->match_lengths, &bits, &match_state, error))
        return false;

    std::size_t literal_cursor = 0u;
    for (std::size_t sequence = 0u; sequence < number_sequences; ++sequence) {
        std::uint16_t literal_code = 0u;
        std::uint16_t offset_code = 0u;
        std::uint16_t match_code = 0u;
        if (!currentSymbol(state->literal_lengths, literal_state, &literal_code, error) ||
            !currentSymbol(state->offsets, offset_state, &offset_code, error) ||
            !currentSymbol(state->match_lengths, match_state, &match_code, error))
            return false;
        if (offset_code > 31u) return fail(error, "Zstd offset code exceeds 31 bits");

        std::uint32_t literal_base = 0u;
        std::uint32_t match_base = 0u;
        unsigned int literal_bits = 0u;
        unsigned int match_bits = 0u;
        if (!codeValue(literal_code, false, &literal_base, &literal_bits, error) ||
            !codeValue(match_code, true, &match_base, &match_bits, error))
            return false;

        std::uint32_t offset_extra = 0u;
        std::uint32_t match_extra = 0u;
        std::uint32_t literal_extra = 0u;
        if (!bits.read(offset_code, &offset_extra, error) ||
            !bits.read(match_bits, &match_extra, error) ||
            !bits.read(literal_bits, &literal_extra, error))
            return false;

        const std::uint64_t offset_value_64 = (std::uint64_t{1u} << offset_code) + offset_extra;
        if (offset_value_64 > std::numeric_limits<std::uint32_t>::max())
            return fail(error, "Zstd offset value exceeds 32 bits");
        const std::size_t literal_length = static_cast<std::size_t>(literal_base) + literal_extra;
        const std::size_t match_length = static_cast<std::size_t>(match_base) + match_extra;

        if (!appendLiteralRange(
                literals.bytes,
                &literal_cursor,
                literal_length,
                block_begin,
                block_maximum,
                output_maximum,
                output,
                error))
            return false;
        std::uint32_t offset = 0u;
        if (!resolveOffset(static_cast<std::uint32_t>(offset_value_64), literal_length, state, &offset, error)) return false;
        if (!copyMatch(
                offset,
                match_length,
                block_begin,
                block_maximum,
                output_maximum,
                *state,
                output,
                error))
            return false;

        if (sequence + 1u != number_sequences) {
            if (!updateSequenceState(state->literal_lengths, &literal_state, &bits, error) ||
                !updateSequenceState(state->match_lengths, &match_state, &bits, error) ||
                !updateSequenceState(state->offsets, &offset_state, &bits, error))
                return false;
        }
    }

    if (!bits.empty()) return fail(error, "Zstd sequence bitstream contains trailing bits");
    if (!appendLiteralRange(
            literals.bytes,
            &literal_cursor,
            literals.bytes.size() - literal_cursor,
            block_begin,
            block_maximum,
            output_maximum,
            output,
            error))
        return false;
    if (literal_cursor != literals.bytes.size()) return fail(error, "Zstd literal stream was not fully consumed");
    return output->size() - block_begin <= block_maximum;
}

} // namespace Models::Compression::ZstdInternal
