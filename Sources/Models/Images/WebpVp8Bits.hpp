#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_BITS_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_BITS_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Images::WebpVp8Internal {

class BoolDecoder {
public:
    bool reset(const std::uint8_t *data, std::size_t size, std::string *error = nullptr);
    bool read(std::uint8_t probability, unsigned int *bit, std::string *error = nullptr);
    bool literal(unsigned int bits, std::uint32_t *value, std::string *error = nullptr);
    bool signedLiteral(unsigned int bits, int *value, std::string *error = nullptr);
    bool optionalSigned(unsigned int bits, int *value, std::string *error = nullptr);

    std::size_t bytesConsumed() const { return cursor_; }

private:
    std::uint8_t nextByte();

    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t cursor_ = 0u;
    std::uint32_t range_ = 255u;
    std::uint32_t value_ = 0u;
    unsigned int bit_count_ = 0u;
};

} // namespace Models::Images::WebpVp8Internal

#endif
