#include "Models/Images/WebpVp8Bits.hpp"

#include <cstddef>
#include <cstdint>

namespace Models::Images::WebpVp8Internal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

} // namespace

bool BoolDecoder::reset(const std::uint8_t *data, std::size_t size, std::string *error)
{
    data_ = nullptr;
    size_ = 0u;
    cursor_ = 0u;
    range_ = 255u;
    value_ = 0u;
    bit_count_ = 0u;
    if (!data || size == 0u) return fail(error, "empty VP8 boolean partition");
    data_ = data;
    size_ = size;
    value_ = static_cast<std::uint32_t>(nextByte()) << 8u;
    value_ |= nextByte();
    return true;
}

std::uint8_t BoolDecoder::nextByte()
{
    if (!data_ || cursor_ >= size_) return 0u;
    return data_[cursor_++];
}

bool BoolDecoder::read(std::uint8_t probability, unsigned int *bit, std::string *error)
{
    if (!bit || !data_ || range_ < 128u || range_ > 255u)
        return fail(error, "invalid VP8 boolean decoder state");
    const std::uint32_t split = 1u + (((range_ - 1u) * probability) >> 8u);
    const std::uint32_t scaled_split = split << 8u;
    if (value_ >= scaled_split) {
        *bit = 1u;
        range_ -= split;
        value_ -= scaled_split;
    } else {
        *bit = 0u;
        range_ = split;
    }

    while (range_ < 128u) {
        range_ <<= 1u;
        value_ <<= 1u;
        ++bit_count_;
        if (bit_count_ == 8u) {
            bit_count_ = 0u;
            value_ |= nextByte();
        }
    }
    return true;
}

bool BoolDecoder::literal(unsigned int bits, std::uint32_t *value, std::string *error)
{
    if (!value || bits > 32u) return fail(error, "invalid VP8 literal width");
    std::uint32_t result = 0u;
    for (unsigned int index = 0u; index < bits; ++index) {
        unsigned int bit = 0u;
        if (!read(128u, &bit, error)) return false;
        result = (result << 1u) | bit;
    }
    *value = result;
    return true;
}

bool BoolDecoder::signedLiteral(unsigned int bits, int *value, std::string *error)
{
    if (!value || bits > 30u) return fail(error, "invalid VP8 signed literal width");
    std::uint32_t magnitude = 0u;
    if (!literal(bits, &magnitude, error)) return false;
    unsigned int sign = 0u;
    if (!read(128u, &sign, error)) return false;
    *value = sign != 0u ? -static_cast<int>(magnitude) : static_cast<int>(magnitude);
    return true;
}

bool BoolDecoder::optionalSigned(unsigned int bits, int *value, std::string *error)
{
    if (!value) return fail(error, "null VP8 optional signed output");
    unsigned int present = 0u;
    if (!read(128u, &present, error)) return false;
    if (present == 0u) {
        *value = 0;
        return true;
    }
    return signedLiteral(bits, value, error);
}

} // namespace Models::Images::WebpVp8Internal
