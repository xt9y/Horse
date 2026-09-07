#include "Models/Images/Jpeg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Models::Images::Jpeg {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kMaximumPixels = 268435456u;
constexpr std::array<std::uint8_t, 64> kZigzag = {
    0,1,8,16,9,2,3,10,
    17,24,32,25,18,11,4,5,
    12,19,26,33,40,48,41,34,
    27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63
};

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint16_t be16(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8u) |
        static_cast<std::uint16_t>(data[1])
    );
}

struct QuantTable {
    bool valid = false;
    std::array<std::uint16_t, 64> value{};
};

struct HuffmanTable {
    bool valid = false;
    std::array<std::uint8_t, 16> counts{};
    std::vector<std::uint8_t> symbols;
    std::array<int, 17> min_code{};
    std::array<int, 17> max_code{};
    std::array<int, 17> value_index{};
};

bool buildHuffman(HuffmanTable *table, std::string *error)
{
    if (!table) return fail(error, "invalid JPEG Huffman table");
    int code = 0;
    int index = 0;
    for (int length = 1; length <= 16; ++length) {
        const int count = table->counts[static_cast<std::size_t>(length - 1)];
        table->value_index[static_cast<std::size_t>(length)] = index;
        if (count == 0) {
            table->min_code[static_cast<std::size_t>(length)] = -1;
            table->max_code[static_cast<std::size_t>(length)] = -1;
        } else {
            table->min_code[static_cast<std::size_t>(length)] = code;
            code += count - 1;
            table->max_code[static_cast<std::size_t>(length)] = code;
            ++code;
            index += count;
        }
        if (code > (1 << length)) return fail(error, "oversubscribed JPEG Huffman table");
        code <<= 1;
    }
    if (index != static_cast<int>(table->symbols.size())) return fail(error, "JPEG Huffman symbol count mismatch");
    table->valid = true;
    return true;
}

struct Component {
    std::uint8_t id = 0u;
    std::uint8_t h = 1u;
    std::uint8_t v = 1u;
    std::uint8_t quant = 0u;
    std::size_t blocks_x = 0u;
    std::size_t blocks_y = 0u;
    std::vector<std::array<std::int32_t,64>> coefficients;
    std::array<std::int8_t,64> approximation{};
    std::vector<std::uint8_t> plane;
};

struct Frame {
    bool valid = false;
    bool progressive = false;
    std::size_t width = 0u;
    std::size_t height = 0u;
    std::uint8_t max_h = 1u;
    std::uint8_t max_v = 1u;
    std::size_t mcu_x = 0u;
    std::size_t mcu_y = 0u;
    std::vector<Component> components;
};

class EntropyReader {
public:
    EntropyReader(const std::uint8_t *data, std::size_t size, std::size_t position)
        : data_(data), size_(size), position_(position)
    {
    }

    bool readBit(std::uint32_t *value, std::string *error)
    {
        if (!value) return fail(error, "invalid JPEG entropy output");
        if (bits_left_ == 0u) {
            std::uint8_t byte = 0u;
            if (!readEntropyByte(&byte, error)) return false;
            current_ = byte;
            bits_left_ = 8u;
        }
        *value = (current_ >> 7u) & 1u;
        current_ <<= 1u;
        --bits_left_;
        return true;
    }

    bool readBits(unsigned count, std::uint32_t *value, std::string *error)
    {
        if (!value || count > 16u) return fail(error, "invalid JPEG entropy bit count");
        std::uint32_t result = 0u;
        for (unsigned bit = 0u; bit < count; ++bit) {
            std::uint32_t next = 0u;
            if (!readBit(&next, error)) return false;
            result = (result << 1u) | next;
        }
        *value = result;
        return true;
    }

    void align() { bits_left_ = 0u; current_ = 0u; }

    bool takeMarker(int *marker, std::string *error)
    {
        if (!marker) return fail(error, "invalid JPEG marker output");
        align();
        if (pending_marker_ >= 0) {
            *marker = pending_marker_;
            pending_marker_ = -1;
            return true;
        }
        while (position_ < size_ && data_[position_] != 0xffu) ++position_;
        if (position_ >= size_) return fail(error, "JPEG scan has no terminating marker");
        while (position_ < size_ && data_[position_] == 0xffu) ++position_;
        if (position_ >= size_) return fail(error, "truncated JPEG marker");
        const int code = data_[position_++];
        if (code == 0x00) return fail(error, "unexpected stuffed byte outside JPEG entropy data");
        *marker = code;
        return true;
    }

    std::size_t position() const { return position_; }

private:
    bool readEntropyByte(std::uint8_t *value, std::string *error)
    {
        if (position_ >= size_) return fail(error, "truncated JPEG entropy data");
        std::uint8_t byte = data_[position_++];
        if (byte != 0xffu) {
            *value = byte;
            return true;
        }

        while (position_ < size_ && data_[position_] == 0xffu) ++position_;
        if (position_ >= size_) return fail(error, "truncated JPEG entropy marker");
        const std::uint8_t code = data_[position_++];
        if (code == 0x00u) {
            *value = 0xffu;
            return true;
        }
        pending_marker_ = code;
        return fail(error, "unexpected JPEG marker inside entropy-coded block");
    }

    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t position_ = 0u;
    std::uint8_t current_ = 0u;
    unsigned bits_left_ = 0u;
    int pending_marker_ = -1;
};

bool decodeHuffman(
    EntropyReader *reader,
    const HuffmanTable& table,
    std::uint8_t *symbol,
    std::string *error)
{
    if (!reader || !symbol || !table.valid) return fail(error, "missing JPEG Huffman table");
    int code = 0;
    for (int length = 1; length <= 16; ++length) {
        std::uint32_t bit = 0u;
        if (!reader->readBit(&bit, error)) return false;
        code = (code << 1) | static_cast<int>(bit);
        const int maximum = table.max_code[static_cast<std::size_t>(length)];
        if (maximum < 0 || code > maximum) continue;
        const int minimum = table.min_code[static_cast<std::size_t>(length)];
        const int slot = table.value_index[static_cast<std::size_t>(length)] + code - minimum;
        if (slot < 0 || static_cast<std::size_t>(slot) >= table.symbols.size()) {
            return fail(error, "invalid JPEG Huffman symbol index");
        }
        *symbol = table.symbols[static_cast<std::size_t>(slot)];
        return true;
    }
    return fail(error, "invalid JPEG Huffman code");
}

bool receiveExtend(EntropyReader *reader, unsigned bits, std::int32_t *value, std::string *error)
{
    if (!value || bits > 16u) return fail(error, "invalid JPEG coefficient width");
    if (bits == 0u) {
        *value = 0;
        return true;
    }
    std::uint32_t raw = 0u;
    if (!reader->readBits(bits, &raw, error)) return false;
    const std::uint32_t threshold = 1u << (bits - 1u);
    if (raw < threshold) {
        *value = static_cast<std::int32_t>(raw) - static_cast<std::int32_t>((1u << bits) - 1u);
    } else {
        *value = static_cast<std::int32_t>(raw);
    }
    return true;
}

struct ScanComponent {
    std::size_t component = 0u;
    std::uint8_t dc = 0u;
    std::uint8_t ac = 0u;
};

struct Tables {
    std::array<QuantTable,4> quant{};
    std::array<std::array<HuffmanTable,4>,2> huffman{};
};

bool decodeBaselineBlock(
    EntropyReader *reader,
    const ScanComponent& scan,
    const Tables& tables,
    std::int32_t *dc_predictor,
    std::array<std::int32_t,64> *block,
    std::string *error)
{
    if (!dc_predictor || !block) return fail(error, "invalid JPEG baseline block state");
    block->fill(0);
    std::uint8_t symbol = 0u;
    if (!decodeHuffman(reader, tables.huffman[0][scan.dc], &symbol, error)) return false;
    if (symbol > 11u) return fail(error, "invalid JPEG DC coefficient width");
    std::int32_t difference = 0;
    if (!receiveExtend(reader, symbol, &difference, error)) return false;
    *dc_predictor += difference;
    (*block)[0] = *dc_predictor;

    int k = 1;
    while (k <= 63) {
        if (!decodeHuffman(reader, tables.huffman[1][scan.ac], &symbol, error)) return false;
        const int run = symbol >> 4u;
        const int width = symbol & 0x0fu;
        if (width == 0) {
            if (run == 0) break;
            if (run != 15) return fail(error, "invalid JPEG AC run symbol");
            k += 16;
            continue;
        }
        k += run;
        if (k > 63 || width > 10) return fail(error, "JPEG AC coefficient exceeds block");
        std::int32_t coefficient = 0;
        if (!receiveExtend(reader, static_cast<unsigned>(width), &coefficient, error)) return false;
        (*block)[kZigzag[static_cast<std::size_t>(k)]] = coefficient;
        ++k;
    }
    return true;
}

bool refineNonzero(
    EntropyReader *reader,
    std::int32_t *coefficient,
    std::int32_t bit_value,
    std::string *error)
{
    std::uint32_t bit = 0u;
    if (!reader->readBit(&bit, error)) return false;
    if (bit != 0u && (std::abs(*coefficient) & bit_value) == 0) {
        *coefficient += *coefficient >= 0 ? bit_value : -bit_value;
    }
    return true;
}

bool decodeProgressiveBlock(
    EntropyReader *reader,
    const ScanComponent& scan,
    const Tables& tables,
    int ss,
    int se,
    int ah,
    int al,
    std::int32_t *dc_predictor,
    std::uint32_t *eob_run,
    std::array<std::int32_t,64> *block,
    std::string *error)
{
    if (!dc_predictor || !eob_run || !block) return fail(error, "invalid JPEG progressive block state");
    const std::int32_t p1 = 1 << al;

    if (ss == 0) {
        if (ah == 0) {
            std::uint8_t symbol = 0u;
            if (!decodeHuffman(reader, tables.huffman[0][scan.dc], &symbol, error)) return false;
            if (symbol > 11u) return fail(error, "invalid progressive JPEG DC width");
            std::int32_t difference = 0;
            if (!receiveExtend(reader, symbol, &difference, error)) return false;
            *dc_predictor += difference;
            (*block)[0] = *dc_predictor << al;
        } else {
            std::uint32_t bit = 0u;
            if (!reader->readBit(&bit, error)) return false;
            if (bit != 0u && (std::abs((*block)[0]) & p1) == 0) {
                (*block)[0] += (*block)[0] >= 0 ? p1 : -p1;
            }
        }
        return true;
    }

    if (ah == 0) {
        if (*eob_run != 0u) {
            --*eob_run;
            return true;
        }
        int k = ss;
        while (k <= se) {
            std::uint8_t symbol = 0u;
            if (!decodeHuffman(reader, tables.huffman[1][scan.ac], &symbol, error)) return false;
            int run = symbol >> 4u;
            const int width = symbol & 0x0fu;
            if (width == 0) {
                if (run == 15) {
                    k += 16;
                    continue;
                }
                std::uint32_t extra = 0u;
                if (run != 0 && !reader->readBits(static_cast<unsigned>(run), &extra, error)) return false;
                *eob_run = (1u << run) + extra;
                --*eob_run;
                break;
            }
            k += run;
            if (k > se || width > 10) return fail(error, "progressive JPEG AC coefficient exceeds scan");
            std::int32_t value = 0;
            if (!receiveExtend(reader, static_cast<unsigned>(width), &value, error)) return false;
            (*block)[kZigzag[static_cast<std::size_t>(k)]] = value << al;
            ++k;
        }
        return true;
    }

    int k = ss;
    std::int32_t new_coefficient = 0;
    int zeros = 0;
    if (*eob_run == 0u) {
        while (k <= se) {
            std::uint8_t symbol = 0u;
            if (!decodeHuffman(reader, tables.huffman[1][scan.ac], &symbol, error)) return false;
            zeros = symbol >> 4u;
            const int width = symbol & 0x0fu;
            new_coefficient = 0;
            if (width != 0) {
                if (width != 1) return fail(error, "invalid progressive JPEG refinement symbol");
                std::uint32_t sign = 0u;
                if (!reader->readBit(&sign, error)) return false;
                new_coefficient = sign != 0u ? p1 : -p1;
            } else if (zeros != 15) {
                std::uint32_t extra = 0u;
                if (zeros != 0 && !reader->readBits(static_cast<unsigned>(zeros), &extra, error)) return false;
                *eob_run = (1u << zeros) + extra;
                break;
            } else {
                zeros = 16;
            }

            bool inserted = false;
            while (k <= se) {
                std::int32_t& coefficient = (*block)[kZigzag[static_cast<std::size_t>(k)]];
                if (coefficient != 0) {
                    if (!refineNonzero(reader, &coefficient, p1, error)) return false;
                } else if (zeros == 0) {
                    if (new_coefficient != 0) {
                        coefficient = new_coefficient;
                        inserted = true;
                        ++k;
                    }
                    break;
                } else {
                    --zeros;
                }
                ++k;
            }
            if (new_coefficient != 0 && !inserted && k > se) {
                return fail(error, "progressive JPEG refinement insertion exceeds scan");
            }
        }
    }

    if (*eob_run != 0u) {
        while (k <= se) {
            std::int32_t& coefficient = (*block)[kZigzag[static_cast<std::size_t>(k)]];
            if (coefficient != 0 && !refineNonzero(reader, &coefficient, p1, error)) return false;
            ++k;
        }
        --*eob_run;
    }
    return true;
}

bool validateProgression(Component& component, int ss, int se, int ah, int al, std::string *error)
{
    if (ah != 0 && ah != al + 1) return fail(error, "invalid progressive JPEG successive approximation");
    for (int k = ss; k <= se; ++k) {
        std::int8_t& current = component.approximation[kZigzag[static_cast<std::size_t>(k)]];
        if (ah == 0) {
            if (current != -1) return fail(error, "duplicate progressive JPEG first scan");
        } else if (current != ah) {
            return fail(error, "invalid progressive JPEG refinement order");
        }
    }
    return true;
}

void updateProgression(Component& component, int ss, int se, int al)
{
    for (int k = ss; k <= se; ++k) {
        component.approximation[kZigzag[static_cast<std::size_t>(k)]] = static_cast<std::int8_t>(al);
    }
}

bool decodeScan(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t *position,
    Frame *frame,
    const Tables& tables,
    std::uint16_t restart_interval,
    const std::vector<ScanComponent>& scan_components,
    int ss,
    int se,
    int ah,
    int al,
    int *next_marker,
    std::string *error)
{
    if (!position || !frame || !next_marker || scan_components.empty()) return fail(error, "invalid JPEG scan state");
    if (ss < 0 || se < ss || se > 63 || ah < 0 || ah > 13 || al < 0 || al > 13) {
        return fail(error, "invalid JPEG scan spectral selection");
    }

    if (!frame->progressive) {
        if (ss != 0 || se != 63 || ah != 0 || al != 0) return fail(error, "invalid baseline JPEG scan parameters");
    } else {
        if (ss == 0 && se != 0) return fail(error, "invalid progressive JPEG DC scan range");
        if (ss != 0 && scan_components.size() != 1u) return fail(error, "progressive JPEG AC scan must have one component");
        for (const ScanComponent& scan : scan_components) {
            if (!validateProgression(frame->components[scan.component], ss, se, ah, al, error)) return false;
        }
    }

    const bool interleaved = scan_components.size() > 1u;
    std::size_t mcu_count = 0u;
    if (interleaved) {
        mcu_count = frame->mcu_x * frame->mcu_y;
    } else {
        const Component& component = frame->components[scan_components[0].component];
        mcu_count = component.blocks_x * component.blocks_y;
    }

    EntropyReader reader(data, size, *position);
    std::array<std::int32_t,4> dc_predictors{};
    std::uint32_t eob_run = 0u;
    int expected_restart = 0;

    for (std::size_t mcu = 0u; mcu < mcu_count; ++mcu) {
        for (const ScanComponent& scan : scan_components) {
            Component& component = frame->components[scan.component];
            const std::size_t repeat_x = interleaved ? component.h : 1u;
            const std::size_t repeat_y = interleaved ? component.v : 1u;
            for (std::size_t by = 0u; by < repeat_y; ++by) {
                for (std::size_t bx = 0u; bx < repeat_x; ++bx) {
                    std::size_t block_x = 0u;
                    std::size_t block_y = 0u;
                    if (interleaved) {
                        const std::size_t mcu_x = mcu % frame->mcu_x;
                        const std::size_t mcu_y = mcu / frame->mcu_x;
                        block_x = mcu_x * component.h + bx;
                        block_y = mcu_y * component.v + by;
                    } else {
                        block_x = mcu % component.blocks_x;
                        block_y = mcu / component.blocks_x;
                    }
                    const std::size_t block_index = block_y * component.blocks_x + block_x;
                    if (block_index >= component.coefficients.size()) return fail(error, "JPEG block index out of range");

                    if (frame->progressive) {
                        if (!decodeProgressiveBlock(
                            &reader, scan, tables, ss, se, ah, al,
                            &dc_predictors[scan.component], &eob_run,
                            &component.coefficients[block_index], error
                        )) return false;
                    } else {
                        if (!decodeBaselineBlock(
                            &reader, scan, tables,
                            &dc_predictors[scan.component],
                            &component.coefficients[block_index], error
                        )) return false;
                    }
                }
            }
        }

        if (restart_interval != 0u && (mcu + 1u) % restart_interval == 0u && mcu + 1u < mcu_count) {
            int marker = 0;
            if (!reader.takeMarker(&marker, error)) return false;
            if (marker != 0xd0 + expected_restart) return fail(error, "unexpected JPEG restart marker");
            expected_restart = (expected_restart + 1) & 7;
            dc_predictors.fill(0);
            eob_run = 0u;
        }
    }

    if (frame->progressive) {
        for (const ScanComponent& scan : scan_components) updateProgression(frame->components[scan.component], ss, se, al);
    }

    if (!reader.takeMarker(next_marker, error)) return false;
    *position = reader.position();
    return true;
}

bool parseDqt(const std::uint8_t *data, std::size_t size, Tables *tables, std::string *error)
{
    std::size_t offset = 0u;
    while (offset < size) {
        const std::uint8_t info = data[offset++];
        const unsigned precision = info >> 4u;
        const unsigned index = info & 0x0fu;
        if (precision > 1u || index >= tables->quant.size()) return fail(error, "invalid JPEG quantization table selector");
        const std::size_t bytes_per_value = precision == 0u ? 1u : 2u;
        if (64u * bytes_per_value > size - offset) return fail(error, "truncated JPEG quantization table");
        QuantTable& table = tables->quant[index];
        for (std::size_t i = 0u; i < 64u; ++i) {
            std::uint16_t value = 0u;
            if (precision == 0u) {
                value = data[offset++];
            } else {
                value = be16(data + offset);
                offset += 2u;
            }
            if (value == 0u) return fail(error, "JPEG quantization table contains zero");
            table.value[kZigzag[i]] = value;
        }
        table.valid = true;
    }
    return true;
}

bool parseDht(const std::uint8_t *data, std::size_t size, Tables *tables, std::string *error)
{
    std::size_t offset = 0u;
    while (offset < size) {
        const std::uint8_t info = data[offset++];
        const unsigned table_class = info >> 4u;
        const unsigned index = info & 0x0fu;
        if (table_class > 1u || index >= 4u) return fail(error, "invalid JPEG Huffman table selector");
        if (16u > size - offset) return fail(error, "truncated JPEG Huffman lengths");
        HuffmanTable table;
        std::size_t symbol_count = 0u;
        for (std::size_t i = 0u; i < 16u; ++i) {
            table.counts[i] = data[offset++];
            symbol_count += table.counts[i];
        }
        if (symbol_count == 0u || symbol_count > 256u || symbol_count > size - offset) {
            return fail(error, "invalid JPEG Huffman symbol count");
        }
        table.symbols.assign(data + offset, data + offset + symbol_count);
        offset += symbol_count;
        if (!buildHuffman(&table, error)) return false;
        tables->huffman[table_class][index] = std::move(table);
    }
    return true;
}

bool parseSof(const std::uint8_t *data, std::size_t size, bool progressive, Frame *frame, std::string *error)
{
    if (size < 6u) return fail(error, "truncated JPEG frame header");
    if (data[0] != 8u) return fail(error, "only 8-bit JPEG precision is supported");
    const std::size_t height = be16(data + 1u);
    const std::size_t width = be16(data + 3u);
    const std::size_t component_count = data[5];
    if (width == 0u || height == 0u || width > kMaximumPixels || height > kMaximumPixels || height > kMaximumPixels / width) {
        return fail(error, "JPEG dimensions exceed image limit");
    }
    if (component_count != 1u && component_count != 3u) return fail(error, "unsupported JPEG component count");
    if (size != 6u + component_count * 3u) return fail(error, "invalid JPEG frame component table length");

    Frame result;
    result.valid = true;
    result.progressive = progressive;
    result.width = width;
    result.height = height;
    result.components.resize(component_count);
    result.max_h = 1u;
    result.max_v = 1u;

    std::size_t offset = 6u;
    for (std::size_t i = 0u; i < component_count; ++i) {
        Component& component = result.components[i];
        component.id = data[offset++];
        const std::uint8_t sampling = data[offset++];
        component.h = sampling >> 4u;
        component.v = sampling & 0x0fu;
        component.quant = data[offset++];
        if (component.h == 0u || component.v == 0u || component.h > 4u || component.v > 4u || component.quant >= 4u) {
            return fail(error, "invalid JPEG sampling or quantization selector");
        }
        for (std::size_t previous = 0u; previous < i; ++previous) {
            if (result.components[previous].id == component.id) return fail(error, "duplicate JPEG component id");
        }
        result.max_h = std::max(result.max_h, component.h);
        result.max_v = std::max(result.max_v, component.v);
        component.approximation.fill(-1);
    }

    result.mcu_x = (width + static_cast<std::size_t>(result.max_h) * 8u - 1u) / (static_cast<std::size_t>(result.max_h) * 8u);
    result.mcu_y = (height + static_cast<std::size_t>(result.max_v) * 8u - 1u) / (static_cast<std::size_t>(result.max_v) * 8u);
    for (Component& component : result.components) {
        component.blocks_x = result.mcu_x * component.h;
        component.blocks_y = result.mcu_y * component.v;
        if (component.blocks_x != 0u && component.blocks_y > std::numeric_limits<std::size_t>::max() / component.blocks_x) {
            return fail(error, "JPEG coefficient allocation overflow");
        }
        component.coefficients.resize(component.blocks_x * component.blocks_y);
        for (auto& block : component.coefficients) block.fill(0);
    }
    *frame = std::move(result);
    return true;
}

int componentIndex(const Frame& frame, std::uint8_t id)
{
    for (std::size_t i = 0u; i < frame.components.size(); ++i) {
        if (frame.components[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

bool parseSos(
    const std::uint8_t *data,
    std::size_t size,
    const Frame& frame,
    std::vector<ScanComponent> *components,
    int *ss,
    int *se,
    int *ah,
    int *al,
    std::string *error)
{
    if (size < 4u || !components || !ss || !se || !ah || !al) return fail(error, "truncated JPEG scan header");
    const std::size_t count = data[0];
    if (count == 0u || count > frame.components.size() || size != 1u + count * 2u + 3u) {
        return fail(error, "invalid JPEG scan component count");
    }
    components->clear();
    std::size_t offset = 1u;
    for (std::size_t i = 0u; i < count; ++i) {
        const int index = componentIndex(frame, data[offset++]);
        if (index < 0) return fail(error, "JPEG scan references unknown component");
        const std::uint8_t selectors = data[offset++];
        ScanComponent scan;
        scan.component = static_cast<std::size_t>(index);
        scan.dc = selectors >> 4u;
        scan.ac = selectors & 0x0fu;
        if (scan.dc >= 4u || scan.ac >= 4u) return fail(error, "invalid JPEG scan Huffman selector");
        for (const ScanComponent& previous : *components) {
            if (previous.component == scan.component) return fail(error, "duplicate component in JPEG scan");
        }
        components->push_back(scan);
    }
    *ss = data[offset++];
    *se = data[offset++];
    const std::uint8_t approximation = data[offset];
    *ah = approximation >> 4u;
    *al = approximation & 0x0fu;
    return true;
}

bool readMarker(const std::uint8_t *data, std::size_t size, std::size_t *position, int *marker, std::string *error)
{
    if (!position || !marker) return fail(error, "invalid JPEG marker state");
    while (*position < size && data[*position] != 0xffu) ++*position;
    if (*position >= size) return fail(error, "JPEG marker not found");
    while (*position < size && data[*position] == 0xffu) ++*position;
    if (*position >= size) return fail(error, "truncated JPEG marker");
    const int value = data[(*position)++];
    if (value == 0x00) return fail(error, "unexpected JPEG stuffed byte outside scan");
    *marker = value;
    return true;
}

std::array<std::array<float,8>,8> makeBasis()
{
    std::array<std::array<float,8>,8> basis{};
    for (int x = 0; x < 8; ++x) {
        for (int u = 0; u < 8; ++u) {
            const float normalization = u == 0 ? 0.7071067811865475f : 1.0f;
            basis[static_cast<std::size_t>(x)][static_cast<std::size_t>(u)] =
                0.5f * normalization * std::cos((2.0f * x + 1.0f) * u * kPi / 16.0f);
        }
    }
    return basis;
}

bool renderComponent(Component *component, const QuantTable& quant, std::string *error)
{
    if (!component || !quant.valid) return fail(error, "missing JPEG quantization table");
    const std::size_t plane_width = component->blocks_x * 8u;
    const std::size_t plane_height = component->blocks_y * 8u;
    if (plane_width != 0u && plane_height > std::numeric_limits<std::size_t>::max() / plane_width) {
        return fail(error, "JPEG plane allocation overflow");
    }
    component->plane.assign(plane_width * plane_height, 0u);
    static const auto basis = makeBasis();

    for (std::size_t block_y = 0u; block_y < component->blocks_y; ++block_y) {
        for (std::size_t block_x = 0u; block_x < component->blocks_x; ++block_x) {
            const auto& coefficients = component->coefficients[block_y * component->blocks_x + block_x];
            std::array<std::array<float,8>,8> temporary{};
            for (int v = 0; v < 8; ++v) {
                for (int x = 0; x < 8; ++x) {
                    float sum = 0.0f;
                    for (int u = 0; u < 8; ++u) {
                        const std::size_t index = static_cast<std::size_t>(v * 8 + u);
                        sum += static_cast<float>(coefficients[index]) * quant.value[index] * basis[static_cast<std::size_t>(x)][static_cast<std::size_t>(u)];
                    }
                    temporary[static_cast<std::size_t>(v)][static_cast<std::size_t>(x)] = sum;
                }
            }
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    float sum = 128.0f;
                    for (int v = 0; v < 8; ++v) {
                        sum += temporary[static_cast<std::size_t>(v)][static_cast<std::size_t>(x)] * basis[static_cast<std::size_t>(y)][static_cast<std::size_t>(v)];
                    }
                    const int rounded = static_cast<int>(std::lround(sum));
                    component->plane[(block_y * 8u + static_cast<std::size_t>(y)) * plane_width + block_x * 8u + static_cast<std::size_t>(x)] =
                        static_cast<std::uint8_t>(std::clamp(rounded, 0, 255));
                }
            }
        }
    }
    return true;
}

std::uint8_t componentSample(const Frame& frame, const Component& component, std::size_t x, std::size_t y)
{
    const std::size_t sample_x = x * component.h / frame.max_h;
    const std::size_t sample_y = y * component.v / frame.max_v;
    const std::size_t plane_width = component.blocks_x * 8u;
    return component.plane[sample_y * plane_width + sample_x];
}

std::uint8_t clampByte(float value)
{
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

bool composeImage(Frame *frame, const Tables& tables, int adobe_transform, Image *image, std::string *error)
{
    if (!frame || !image) return fail(error, "invalid JPEG output state");
    for (Component& component : frame->components) {
        if (component.quant >= tables.quant.size() || !renderComponent(&component, tables.quant[component.quant], error)) return false;
    }

    image->width = static_cast<int>(frame->width);
    image->height = static_cast<int>(frame->height);
    image->rgba.assign(frame->width * frame->height * 4u, 255u);
    image->meaningful_alpha = false;

    const bool explicit_rgb = frame->components.size() == 3u &&
        frame->components[0].id == static_cast<std::uint8_t>('R') &&
        frame->components[1].id == static_cast<std::uint8_t>('G') &&
        frame->components[2].id == static_cast<std::uint8_t>('B');
    const bool direct_rgb = explicit_rgb || adobe_transform == 0;

    for (std::size_t y = 0u; y < frame->height; ++y) {
        for (std::size_t x = 0u; x < frame->width; ++x) {
            const std::size_t destination = (y * frame->width + x) * 4u;
            if (frame->components.size() == 1u) {
                const std::uint8_t gray = componentSample(*frame, frame->components[0], x, y);
                image->rgba[destination + 0u] = gray;
                image->rgba[destination + 1u] = gray;
                image->rgba[destination + 2u] = gray;
                continue;
            }

            const float c0 = componentSample(*frame, frame->components[0], x, y);
            const float c1 = componentSample(*frame, frame->components[1], x, y);
            const float c2 = componentSample(*frame, frame->components[2], x, y);
            if (direct_rgb) {
                image->rgba[destination + 0u] = clampByte(c0);
                image->rgba[destination + 1u] = clampByte(c1);
                image->rgba[destination + 2u] = clampByte(c2);
            } else {
                const float cb = c1 - 128.0f;
                const float cr = c2 - 128.0f;
                image->rgba[destination + 0u] = clampByte(c0 + 1.40200f * cr);
                image->rgba[destination + 1u] = clampByte(c0 - 0.344136f * cb - 0.714136f * cr);
                image->rgba[destination + 2u] = clampByte(c0 + 1.77200f * cb);
            }
        }
    }
    return true;
}

} // namespace

bool matches(const std::uint8_t *data, std::size_t size)
{
    return data && size >= 2u && data[0] == 0xffu && data[1] == 0xd8u;
}

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null JPEG image output");
    *image = {};
    if (!matches(data, size)) return fail(error, "invalid JPEG SOI marker");

    Tables tables;
    Frame frame;
    std::uint16_t restart_interval = 0u;
    int adobe_transform = -1;
    std::size_t position = 2u;
    int pending_marker = -1;
    bool saw_scan = false;
    bool saw_eoi = false;

    while (position <= size) {
        int marker = 0;
        if (pending_marker >= 0) {
            marker = pending_marker;
            pending_marker = -1;
        } else if (!readMarker(data, size, &position, &marker, error)) {
            return false;
        }

        if (marker == 0xd9) {
            saw_eoi = true;
            break;
        }
        if (marker == 0xd8) continue;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            return fail(error, "unexpected standalone JPEG marker");
        }

        if (position + 2u > size) return fail(error, "truncated JPEG segment length");
        const std::uint16_t segment_length = be16(data + position);
        position += 2u;
        if (segment_length < 2u) return fail(error, "invalid JPEG segment length");
        const std::size_t payload_size = static_cast<std::size_t>(segment_length - 2u);
        if (payload_size > size - position) return fail(error, "JPEG segment exceeds input");
        const std::uint8_t *payload = data + position;
        position += payload_size;

        if (marker == 0xdb) {
            if (!parseDqt(payload, payload_size, &tables, error)) return false;
        } else if (marker == 0xc4) {
            if (!parseDht(payload, payload_size, &tables, error)) return false;
        } else if (marker == 0xc0 || marker == 0xc2) {
            if (frame.valid) return fail(error, "multiple JPEG frame headers are unsupported");
            if (!parseSof(payload, payload_size, marker == 0xc2, &frame, error)) return false;
        } else if (marker == 0xdd) {
            if (payload_size != 2u) return fail(error, "invalid JPEG DRI segment");
            restart_interval = be16(payload);
        } else if (marker == 0xee) {
            if (payload_size >= 12u && std::memcmp(payload, "Adobe", 5u) == 0) adobe_transform = payload[11];
        } else if (marker == 0xda) {
            if (!frame.valid) return fail(error, "JPEG scan appears before frame header");
            std::vector<ScanComponent> scan_components;
            int ss = 0;
            int se = 0;
            int ah = 0;
            int al = 0;
            if (!parseSos(payload, payload_size, frame, &scan_components, &ss, &se, &ah, &al, error)) return false;
            for (const ScanComponent& scan : scan_components) {
                if (!tables.huffman[0][scan.dc].valid && ss == 0) return fail(error, "JPEG scan references missing DC Huffman table");
                if (!tables.huffman[1][scan.ac].valid && se != 0) return fail(error, "JPEG scan references missing AC Huffman table");
            }
            if (!decodeScan(
                data, size, &position, &frame, tables, restart_interval,
                scan_components, ss, se, ah, al, &pending_marker, error
            )) return false;
            saw_scan = true;
        } else if (
            marker == 0xc1 || marker == 0xc3 || marker == 0xc5 || marker == 0xc6 ||
            marker == 0xc7 || marker == 0xc9 || marker == 0xca || marker == 0xcb ||
            marker == 0xcd || marker == 0xce || marker == 0xcf)
        {
            return fail(error, "unsupported JPEG frame coding mode");
        }
    }

    if (!frame.valid || !saw_scan || !saw_eoi) return fail(error, "incomplete JPEG stream");
    return composeImage(&frame, tables, adobe_transform, image, error);
}

} // namespace Models::Images::Jpeg
