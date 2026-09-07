#ifndef RW_ENGINE_MODELS_COMPRESSION_CHECKSUMS_HPP
#define RW_ENGINE_MODELS_COMPRESSION_CHECKSUMS_HPP

#include <cstddef>
#include <cstdint>

namespace Models::Compression {

std::uint32_t adler32(const std::uint8_t *data, std::size_t size);
std::uint32_t crc32(const std::uint8_t *data, std::size_t size);

} // namespace Models::Compression

#endif
