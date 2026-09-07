#ifndef RW_ENGINE_MODELS_COMPRESSION_DEFLATE_HPP
#define RW_ENGINE_MODELS_COMPRESSION_DEFLATE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression {

struct InflateOptions {
    std::size_t max_output = 256u * 1024u * 1024u;
    bool verify_adler32 = true;
};

bool inflateZlib(
    const std::uint8_t *data,
    std::size_t size,
    std::vector<std::uint8_t> *output,
    std::string *error = nullptr,
    InflateOptions options = {}
);

} // namespace Models::Compression

#endif
