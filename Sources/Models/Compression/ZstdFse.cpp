#include "Models/Compression/ZstdFse.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Models::Compression::ZstdInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

class ForwardBits {
public:
    ForwardBits(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    bool peek(unsigned int count, std::uint32_t *out) const
    {
        if (!out || count > 24u || bit_ > size_ * 8u || count > size_ * 8u - bit_) return false;
        std::uint64_t value = 0u;
        const std::size_t byte = bit_ >> 3u;
        const unsigned int shift = static_cast<unsigned int>(bit_ & 7u);
        const std::size_t available = std::min<std::size_t>(5u, size_ - byte);
        for (std::size_t index = 0u; index < available; ++index)
            value |= static_cast<std::uint64_t>(data_[byte + index]) << static_cast<unsigned int>(index * 8u);
        value >>= shift;
        *out = count == 0u ? 0u : static_cast<std::uint32_t>(value & ((std::uint64_t{1u} << count) - 1u));
        return true;
    }

    bool read(unsigned int count, std::uint32_t *out)
    {
        if (!peek(count, out)) return false;
        bit_ += count;
        return true;
    }

    std::size_t bytesConsumed() const { return (bit_ + 7u) >> 3u; }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t bit_ = 0u;
};

unsigned int highBit(std::uint32_t value)
{
    unsigned int result = 0u;
    while (value > 1u) {
        value >>= 1u;
        ++result;
    }
    return result;
}

} // namespace

bool buildFseTable(
    const std::vector<std::int16_t>& normalized,
    std::uint8_t accuracy_log,
    FseTable *table,
    std::string *error)
{
    if (!table) return fail(error, "null Zstd FSE table output");
    if (accuracy_log < 5u || accuracy_log > 15u) return fail(error, "invalid Zstd FSE accuracy log");
    const std::size_t table_size = std::size_t{1u} << accuracy_log;
    if (normalized.empty()) return fail(error, "empty Zstd FSE distribution");

    std::size_t total = 0u;
    std::size_t nonzero = 0u;
    for (const std::int16_t probability : normalized) {
        if (probability < -1) return fail(error, "invalid Zstd FSE normalized probability");
        if (probability != 0) ++nonzero;
        const std::size_t points = probability == -1 ? 1u : static_cast<std::size_t>(probability);
        if (points > table_size - std::min(total, table_size)) return fail(error, "Zstd FSE distribution exceeds table size");
        total += points;
    }
    if (total != table_size || nonzero < 2u) return fail(error, "invalid Zstd FSE normalized distribution total");

    std::vector<std::uint16_t> symbols(table_size, std::numeric_limits<std::uint16_t>::max());
    std::size_t high = table_size - 1u;
    for (std::size_t symbol = 0u; symbol < normalized.size(); ++symbol) {
        if (normalized[symbol] != -1) continue;
        symbols[high--] = static_cast<std::uint16_t>(symbol);
    }

    const std::size_t mask = table_size - 1u;
    const std::size_t step = (table_size >> 1u) + (table_size >> 3u) + 3u;
    std::size_t position = 0u;
    for (std::size_t symbol = 0u; symbol < normalized.size(); ++symbol) {
        if (normalized[symbol] <= 0) continue;
        for (std::int16_t occurrence = 0; occurrence < normalized[symbol]; ++occurrence) {
            if (position > high) return fail(error, "invalid Zstd FSE table spread");
            symbols[position] = static_cast<std::uint16_t>(symbol);
            position = (position + step) & mask;
            while (position > high) position = (position + step) & mask;
        }
    }
    if (position != 0u) return fail(error, "Zstd FSE table spread did not close");
    for (const std::uint16_t symbol : symbols)
        if (symbol == std::numeric_limits<std::uint16_t>::max()) return fail(error, "Zstd FSE table contains an unassigned state");

    std::vector<std::uint32_t> next(normalized.size(), 0u);
    for (std::size_t symbol = 0u; symbol < normalized.size(); ++symbol)
        next[symbol] = normalized[symbol] == -1 ? 1u : static_cast<std::uint32_t>(std::max<std::int16_t>(normalized[symbol], 0));

    table->accuracy_log = accuracy_log;
    table->rows.assign(table_size, {});
    for (std::size_t state = 0u; state < table_size; ++state) {
        const std::uint16_t symbol = symbols[state];
        const std::uint32_t next_state = next[symbol]++;
        if (next_state == 0u) return fail(error, "invalid zero Zstd FSE next state");
        const unsigned int bits = static_cast<unsigned int>(accuracy_log) - highBit(next_state);
        const std::uint32_t baseline = (next_state << bits) - static_cast<std::uint32_t>(table_size);
        if (baseline >= table_size || bits > accuracy_log)
            return fail(error, "invalid Zstd FSE decode row");
        table->rows[state] = {
            static_cast<std::uint16_t>(baseline),
            static_cast<std::uint8_t>(bits),
            symbol,
        };
    }
    return true;
}

bool parseFseTable(
    const std::uint8_t *data,
    std::size_t size,
    std::uint16_t maximum_symbol,
    std::uint8_t maximum_accuracy_log,
    FseTable *table,
    std::size_t *consumed,
    std::string *error)
{
    if (!data || !table || !consumed || size == 0u) return fail(error, "truncated Zstd FSE table description");
    ForwardBits bits(data, size);
    std::uint32_t low4 = 0u;
    if (!bits.read(4u, &low4)) return fail(error, "truncated Zstd FSE accuracy log");
    const std::uint8_t accuracy_log = static_cast<std::uint8_t>(low4 + 5u);
    if (accuracy_log > maximum_accuracy_log)
        return fail(error, "Zstd FSE accuracy log exceeds symbol-table limit");

    std::uint32_t threshold = std::uint32_t{1u} << accuracy_log;
    std::uint32_t remaining = threshold + 1u;
    unsigned int probability_bits = static_cast<unsigned int>(accuracy_log) + 1u;
    std::vector<std::int16_t> normalized;
    normalized.reserve(static_cast<std::size_t>(maximum_symbol) + 1u);
    bool previous_zero = false;

    while (remaining > 1u) {
        if (previous_zero) {
            std::size_t next_symbol = normalized.size();
            for (;;) {
                std::uint32_t repeat = 0u;
                if (!bits.read(2u, &repeat)) return fail(error, "truncated Zstd FSE zero run");
                if (repeat > static_cast<std::uint32_t>(std::numeric_limits<std::size_t>::max() - next_symbol))
                    return fail(error, "Zstd FSE zero run overflows");
                next_symbol += repeat;
                if (repeat != 3u) break;
            }
            if (next_symbol > maximum_symbol)
                return fail(error, "Zstd FSE zero run exceeds allowed symbol range");
            normalized.resize(next_symbol, 0);
        }

        if (normalized.size() > maximum_symbol)
            return fail(error, "Zstd FSE distribution exceeds allowed symbol range");

        const std::uint32_t maximum_short = (threshold << 1u) - 1u;
        const std::uint32_t short_limit = maximum_short - remaining;
        std::uint32_t low = 0u;
        if (!bits.peek(probability_bits - 1u, &low)) return fail(error, "truncated Zstd FSE probability");

        std::uint32_t encoded = 0u;
        if (low < short_limit) {
            if (!bits.read(probability_bits - 1u, &encoded)) return fail(error, "truncated Zstd FSE short probability");
        } else {
            if (!bits.read(probability_bits, &encoded)) return fail(error, "truncated Zstd FSE probability");
            if (encoded >= threshold) encoded -= short_limit;
        }

        const std::int32_t probability = static_cast<std::int32_t>(encoded) - 1;
        if (probability < -1) return fail(error, "invalid Zstd FSE probability");
        const std::uint32_t points = probability == -1
            ? 1u
            : static_cast<std::uint32_t>(probability);
        if (points >= remaining) return fail(error, "Zstd FSE probability exhausts distribution early");
        remaining -= points;
        normalized.push_back(static_cast<std::int16_t>(probability));
        previous_zero = probability == 0;

        while (remaining < threshold) {
            threshold >>= 1u;
            if (probability_bits == 0u) return fail(error, "invalid Zstd FSE probability bit width");
            --probability_bits;
        }
    }

    if (remaining != 1u) return fail(error, "Zstd FSE normalized distribution is incomplete");
    *consumed = bits.bytesConsumed();
    if (*consumed == 0u || *consumed > size) return fail(error, "invalid Zstd FSE table description length");
    return buildFseTable(normalized, accuracy_log, table, error);
}

} // namespace Models::Compression::ZstdInternal
