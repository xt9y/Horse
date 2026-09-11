#ifndef HORSE_MODELS_COMPRESSION_XXHASH_HPP
#define HORSE_MODELS_COMPRESSION_XXHASH_HPP

#include <cstddef>
#include <cstdint>

namespace Models::Compression {

std::uint64_t xxHash64(const void *data, std::size_t size, std::uint64_t seed = 0u);

} // namespace Models::Compression

#endif
