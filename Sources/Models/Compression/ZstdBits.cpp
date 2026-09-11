#include "Models/Compression/ZstdBits.hpp"

#include <cstddef>
#include <cstdint>

namespace Models::Compression::ZstdInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

unsigned int highestSetBit(std::uint8_t value)
{
    unsigned int result = 0u;
    while (value > 1u) {
        value >>= 1u;
        ++result;
    }
    return result;
}

} // namespace

bool ReverseBits::reset(const std::uint8_t *data, std::size_t size, std::string *error)
{
    data_ = nullptr;
    size_ = 0u;
    bit_position_ = 0u;
    if (!data || size == 0u) return fail(error, "empty Zstd reverse bitstream");
    const std::uint8_t last = data[size - 1u];
    if (last == 0u) return fail(error, "Zstd reverse bitstream has no end marker");

    data_ = data;
    size_ = size;
    bit_position_ = (size - 1u) * 8u + highestSetBit(last);
    return true;
}

bool ReverseBits::read(unsigned int count, std::uint32_t *out, std::string *error)
{
    if (!out || count > 32u) return fail(error, "invalid Zstd reverse bit read");
    if (count > bit_position_) return fail(error, "Zstd reverse bitstream is exhausted");
    if (count == 0u) {
        *out = 0u;
        return true;
    }

    const std::size_t start = bit_position_ - count;
    std::uint32_t value = 0u;
    for (unsigned int bit = 0u; bit < count; ++bit) {
        const std::size_t position = start + bit;
        const std::uint8_t source = data_[position >> 3u];
        const unsigned int source_bit = static_cast<unsigned int>(position & 7u);
        value |= static_cast<std::uint32_t>((source >> source_bit) & 1u) << bit;
    }
    bit_position_ = start;
    *out = value;
    return true;
}

bool initializeFseState(
    const FseTable& table,
    ReverseBits *bits,
    FseState *state,
    std::string *error)
{
    if (!bits || !state || table.rows.empty()) return fail(error, "invalid Zstd FSE state initialization");
    std::uint32_t value = 0u;
    if (!bits->read(table.accuracy_log, &value, error)) return false;
    if (value >= table.rows.size()) return fail(error, "Zstd FSE initial state exceeds table");
    state->table = &table;
    state->state = value;
    return true;
}

bool fseSymbol(const FseState& state, std::uint16_t *symbol, std::string *error)
{
    if (!symbol || !state.table || state.state >= state.table->rows.size())
        return fail(error, "invalid Zstd FSE state");
    *symbol = state.table->rows[state.state].symbol;
    return true;
}

bool updateFseState(FseState *state, ReverseBits *bits, std::string *error)
{
    if (!state || !bits || !state->table || state->state >= state->table->rows.size())
        return fail(error, "invalid Zstd FSE state update");
    const FseEntry& row = state->table->rows[state->state];
    std::uint32_t extra = 0u;
    if (!bits->read(row.bits, &extra, error)) return false;
    const std::uint32_t next = static_cast<std::uint32_t>(row.baseline) + extra;
    if (next >= state->table->rows.size()) return fail(error, "Zstd FSE next state exceeds table");
    state->state = next;
    return true;
}

} // namespace Models::Compression::ZstdInternal
