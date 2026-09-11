#ifndef HORSE_MODELS_COMPRESSION_ZSTD_BITS_HPP
#define HORSE_MODELS_COMPRESSION_ZSTD_BITS_HPP

#include "Models/Compression/ZstdFse.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Compression::ZstdInternal {

class ReverseBits {
public:
    bool reset(const std::uint8_t *data, std::size_t size, std::string *error = nullptr);
    bool read(unsigned int count, std::uint32_t *out, std::string *error = nullptr);
    bool readPadded(
        unsigned int count,
        std::uint32_t *out,
        bool *overflow,
        std::string *error = nullptr
    );
    bool empty() const { return bit_position_ == 0u; }
    std::size_t remaining() const { return bit_position_; }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t bit_position_ = 0u;
};

struct FseState {
    const FseTable *table = nullptr;
    std::uint32_t state = 0u;
};

bool initializeFseState(
    const FseTable& table,
    ReverseBits *bits,
    FseState *state,
    std::string *error = nullptr
);

bool fseSymbol(const FseState& state, std::uint16_t *symbol, std::string *error = nullptr);
bool updateFseState(FseState *state, ReverseBits *bits, std::string *error = nullptr);
bool updateFseStatePadded(
    FseState *state,
    ReverseBits *bits,
    bool *overflow,
    std::string *error = nullptr
);

} // namespace Models::Compression::ZstdInternal

#endif
