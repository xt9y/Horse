#ifndef HORSE_MODELS_INTERNAL_MODEL_CACHE_BINARY_HPP
#define HORSE_MODELS_INTERNAL_MODEL_CACHE_BINARY_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace Models::Internal::ModelCacheBinary {

inline constexpr std::uint64_t MaximumElements = 64ull * 1024ull * 1024ull;
inline constexpr std::uint64_t MaximumStringBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::uint64_t MaximumBlobBytes = 2ull * 1024ull * 1024ull * 1024ull;

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void boolean(bool value) { u8(value ? 1u : 0u); }

    void u16(std::uint16_t value)
    {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8u));
    }

    void u32(std::uint32_t value)
    {
        for (unsigned int shift = 0u; shift < 32u; shift += 8u)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void u64(std::uint64_t value)
    {
        for (unsigned int shift = 0u; shift < 64u; shift += 8u)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }

    void raw(const void *data, std::size_t size)
    {
        if (!ok_ || size == 0u) return;
        if (!data || size > std::numeric_limits<std::size_t>::max() - bytes_.size()) {
            ok_ = false;
            return;
        }
        const auto *begin = static_cast<const std::uint8_t *>(data);
        bytes_.insert(bytes_.end(), begin, begin + size);
    }

    void string(const std::string& value)
    {
        if (value.size() > MaximumStringBytes) {
            ok_ = false;
            return;
        }
        u64(static_cast<std::uint64_t>(value.size()));
        raw(value.data(), value.size());
    }

    void blob(const std::vector<std::uint8_t>& value)
    {
        if (value.size() > MaximumBlobBytes) {
            ok_ = false;
            return;
        }
        u64(static_cast<std::uint64_t>(value.size()));
        raw(value.data(), value.size());
    }

    bool count(std::size_t value)
    {
        if (value > MaximumElements) {
            ok_ = false;
            return false;
        }
        u64(static_cast<std::uint64_t>(value));
        return true;
    }

    bool good() const { return ok_; }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
    bool ok_ = true;
};

class Reader {
public:
    Reader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}
    explicit Reader(const std::vector<std::uint8_t>& bytes) : Reader(bytes.data(), bytes.size()) {}

    bool u8(std::uint8_t *value)
    {
        if (!value || remaining() < 1u) return fail();
        *value = data_[offset_++];
        return true;
    }

    bool boolean(bool *value)
    {
        std::uint8_t raw_value = 0u;
        if (!u8(&raw_value) || raw_value > 1u || !value) return fail();
        *value = raw_value != 0u;
        return true;
    }

    bool u16(std::uint16_t *value)
    {
        if (!value || remaining() < 2u) return fail();
        *value = static_cast<std::uint16_t>(data_[offset_]) |
            static_cast<std::uint16_t>(data_[offset_ + 1u] << 8u);
        offset_ += 2u;
        return true;
    }

    bool u32(std::uint32_t *value)
    {
        if (!value || remaining() < 4u) return fail();
        std::uint32_t out = 0u;
        for (unsigned int index = 0u; index < 4u; ++index)
            out |= static_cast<std::uint32_t>(data_[offset_ + index]) << (index * 8u);
        offset_ += 4u;
        *value = out;
        return true;
    }

    bool u64(std::uint64_t *value)
    {
        if (!value || remaining() < 8u) return fail();
        std::uint64_t out = 0u;
        for (unsigned int index = 0u; index < 8u; ++index)
            out |= static_cast<std::uint64_t>(data_[offset_ + index]) << (index * 8u);
        offset_ += 8u;
        *value = out;
        return true;
    }

    bool i32(std::int32_t *value)
    {
        std::uint32_t raw_value = 0u;
        if (!u32(&raw_value) || !value) return fail();
        *value = static_cast<std::int32_t>(raw_value);
        return true;
    }

    bool i64(std::int64_t *value)
    {
        std::uint64_t raw_value = 0u;
        if (!u64(&raw_value) || !value) return fail();
        *value = static_cast<std::int64_t>(raw_value);
        return true;
    }

    bool f32(float *value)
    {
        std::uint32_t raw_value = 0u;
        if (!u32(&raw_value) || !value) return fail();
        *value = std::bit_cast<float>(raw_value);
        return true;
    }

    bool f64(double *value)
    {
        std::uint64_t raw_value = 0u;
        if (!u64(&raw_value) || !value) return fail();
        *value = std::bit_cast<double>(raw_value);
        return true;
    }

    bool raw(void *out, std::size_t size)
    {
        if (size > remaining() || (size != 0u && !out)) return fail();
        if (size != 0u) std::memcpy(out, data_ + offset_, size);
        offset_ += size;
        return true;
    }

    bool string(std::string *value)
    {
        std::uint64_t size = 0u;
        if (!u64(&size) || size > MaximumStringBytes || size > remaining() || !value) return fail();
        value->assign(reinterpret_cast<const char *>(data_ + offset_), static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return true;
    }

    bool blob(std::vector<std::uint8_t> *value)
    {
        std::uint64_t size = 0u;
        if (!u64(&size) || size > MaximumBlobBytes || size > remaining() || !value) return fail();
        value->assign(data_ + offset_, data_ + offset_ + static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return true;
    }

    bool count(std::size_t *value, std::uint64_t maximum = MaximumElements)
    {
        std::uint64_t count_value = 0u;
        if (!u64(&count_value) || count_value > maximum ||
            count_value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || !value)
            return fail();
        *value = static_cast<std::size_t>(count_value);
        return true;
    }

    std::size_t remaining() const { return offset_ <= size_ ? size_ - offset_ : 0u; }
    bool good() const { return ok_; }
    bool finished() const { return ok_ && offset_ == size_; }

private:
    bool fail()
    {
        ok_ = false;
        return false;
    }

    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t offset_ = 0u;
    bool ok_ = true;
};

} // namespace Models::Internal::ModelCacheBinary

#endif
