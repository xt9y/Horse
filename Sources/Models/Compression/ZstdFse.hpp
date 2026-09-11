#ifndef HORSE_MODELS_COMPRESSION_ZSTD_FSE_HPP
#define HORSE_MODELS_COMPRESSION_ZSTD_FSE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression::ZstdInternal {

struct FseEntry {
    std::uint16_t baseline = 0u;
    std::uint8_t bits = 0u;
    std::uint16_t symbol = 0u;
};

struct FseTable {
    std::uint8_t accuracy_log = 0u;
    std::vector<FseEntry> rows;
};

bool buildFseTable(
    const std::vector<std::int16_t>& normalized,
    std::uint8_t accuracy_log,
    FseTable *table,
    std::string *error = nullptr
);

bool parseFseTable(
    const std::uint8_t *data,
    std::size_t size,
    std::uint16_t maximum_symbol,
    std::uint8_t maximum_accuracy_log,
    FseTable *table,
    std::size_t *consumed,
    std::string *error = nullptr
);

} // namespace Models::Compression::ZstdInternal

#endif
