#ifndef HORSE_MODELS_COMPRESSION_ZSTD_BLOCK_HPP
#define HORSE_MODELS_COMPRESSION_ZSTD_BLOCK_HPP

#include "Models/Compression/ZstdFse.hpp"
#include "Models/Compression/ZstdHuffman.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression::ZstdInternal {

struct SequenceTable {
    FseTable fse;
    std::uint16_t repeated_symbol = 0u;
    bool rle = false;
    bool valid = false;
};

struct BlockState {
    HuffmanTable huffman;
    SequenceTable literal_lengths;
    SequenceTable offsets;
    SequenceTable match_lengths;
    std::array<std::uint32_t, 3> repeated_offsets {{1u, 4u, 8u}};
    std::size_t frame_output_begin = 0u;
    std::size_t window_size = 0u;
};

bool decodeCompressedBlock(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t block_maximum,
    std::size_t output_maximum,
    BlockState *state,
    std::vector<std::uint8_t> *output,
    std::string *error = nullptr
);

} // namespace Models::Compression::ZstdInternal

#endif
