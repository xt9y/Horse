#ifndef HORSE_MODELS_COMPRESSION_ZSTD_HUFFMAN_HPP
#define HORSE_MODELS_COMPRESSION_ZSTD_HUFFMAN_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression::ZstdInternal {

struct HuffmanNode {
    std::array<std::int16_t, 2> child {{-1, -1}};
    std::int16_t symbol = -1;
};

struct HuffmanTable {
    std::uint8_t maximum_bits = 0u;
    std::vector<HuffmanNode> nodes;

    bool valid() const { return maximum_bits != 0u && !nodes.empty(); }
    void clear() { maximum_bits = 0u; nodes.clear(); }
};

bool parseHuffmanTable(
    const std::uint8_t *data,
    std::size_t size,
    HuffmanTable *table,
    std::size_t *consumed,
    std::string *error = nullptr
);

bool decodeHuffmanStream(
    const HuffmanTable& table,
    const std::uint8_t *data,
    std::size_t size,
    std::size_t regenerated_size,
    std::vector<std::uint8_t> *output,
    std::string *error = nullptr
);

} // namespace Models::Compression::ZstdInternal

#endif
