#ifndef HORSE_MODELS_COMPRESSION_ZSTD_HPP
#define HORSE_MODELS_COMPRESSION_ZSTD_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression {

struct ZstdOptions {
    std::size_t max_output = 256u * 1024u * 1024u;
    std::size_t max_window = 256u * 1024u * 1024u;
};

bool decompressZstd(
    const std::uint8_t *data,
    std::size_t size,
    std::vector<std::uint8_t> *output,
    std::string *error = nullptr,
    ZstdOptions options = {}
);

} // namespace Models::Compression

#endif
