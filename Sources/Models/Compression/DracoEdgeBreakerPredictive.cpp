#include "DracoEdgeBreakerPredictive.hpp"
#include "DracoEdgeBreakerAttributes.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Models::Compression {
namespace {

#include "DracoSequentialCore.inl"

[[maybe_unused]] void retainSequentialCoreHelpers()
{
    (void)&readPredictionBits;
    (void)&zigzag;
    (void)&littleUnsigned;
    (void)&dataTypeSize;
    (void)&rawNumber;
}

#include "DracoEdgeBreakerValenceCore.inl"

class BitBlock {
public:
    BitBlock() = default;
    BitBlock(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    bool read(unsigned int count, std::uint32_t *out)
    {
        if (!out || count > 32u || count > available()) return false;
        std::uint32_t value = 0u;
        for (unsigned int index = 0u; index < count; ++index) {
            value |= static_cast<std::uint32_t>((data_[bit_ >> 3u] >> (bit_ & 7u)) & 1u) << index;
            ++bit_;
        }
        *out = value;
        return true;
    }

    std::size_t bytesUsed() const { return (bit_ + 7u) / 8u; }
    std::size_t available() const { return size_ * 8u - bit_; }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t bit_ = 0u;
};

bool fixedU64(Reader& reader, std::uint64_t *out)
{
    if (!out) return false;
    std::uint32_t low = 0u;
    std::uint32_t high = 0u;
    if (!reader.u32(&low) || !reader.u32(&high)) return false;
    *out = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32u);
    return true;
}

struct PredictiveTraversal {
    BitBlock symbols;
    BitBlock legacy_start_faces;
    RansBit start_faces;
    std::vector<RansBit> seams;
    RansBit predictions;
    std::vector<int> valence;
    int last_symbol = -1;
    int predicted_symbol = -1;
    bool legacy_start_face_stream = false;
    bool legacy_attribute_connectivity = false;

    bool explicitSymbol(std::uint32_t *out)
    {
        std::uint32_t first = 0u;
        if (!symbols.read(1u, &first)) return false;
        if (first == 0u) {
            *out = 0u;
            return true;
        }
        std::uint32_t suffix = 0u;
        if (!symbols.read(2u, &suffix)) return false;
        *out = 1u | (suffix << 1u);
        return true;
    }

    bool next(std::uint32_t *out)
    {
        if (!out) return false;
        if (predicted_symbol >= 0) {
            bool prediction_correct = false;
            if (!predictions.bit(&prediction_correct)) return false;
            if (prediction_correct) {
                last_symbol = predicted_symbol;
                *out = static_cast<std::uint32_t>(predicted_symbol);
                return true;
            }
        }
        std::uint32_t symbol = 0u;
        if (!explicitSymbol(&symbol)) return false;
        last_symbol = static_cast<int>(symbol);
        *out = symbol;
        return true;
    }

    bool merge(int destination, int source)
    {
        if (destination < 0 || source < 0 ||
            static_cast<std::size_t>(destination) >= valence.size() ||
            static_cast<std::size_t>(source) >= valence.size())
            return false;
        valence[static_cast<std::size_t>(destination)] += valence[static_cast<std::size_t>(source)];
        return true;
    }

    bool reached(const CornerTable& table, int corner)
    {
        const int next = table.vertex(table.next(corner));
        const int previous = table.vertex(table.previous(corner));
        const int vertex = table.vertex(corner);
        if (next < 0 || previous < 0 || vertex < 0) return false;
        if (static_cast<std::size_t>(std::max({next, previous, vertex})) >= valence.size()) return false;

        switch (last_symbol) {
            case 0:
            case 1:
                ++valence[static_cast<std::size_t>(next)];
                ++valence[static_cast<std::size_t>(previous)];
                break;
            case 5:
                ++valence[static_cast<std::size_t>(vertex)];
                ++valence[static_cast<std::size_t>(next)];
                valence[static_cast<std::size_t>(previous)] += 2;
                break;
            case 3:
                ++valence[static_cast<std::size_t>(vertex)];
                valence[static_cast<std::size_t>(next)] += 2;
                ++valence[static_cast<std::size_t>(previous)];
                break;
            case 7:
                valence[static_cast<std::size_t>(vertex)] += 2;
                valence[static_cast<std::size_t>(next)] += 2;
                valence[static_cast<std::size_t>(previous)] += 2;
                break;
            default:
                return false;
        }

        if (last_symbol == 0 || last_symbol == 5)
            predicted_symbol = valence[static_cast<std::size_t>(next)] < 6 ? 5 : 0;
        else
            predicted_symbol = -1;
        return true;
    }

    bool startFace(bool *out)
    {
        if (!out) return false;
        if (!legacy_start_face_stream) return start_faces.bit(out);
        std::uint32_t value = 0u;
        if (!legacy_start_faces.read(1u, &value)) return false;
        *out = value != 0u;
        return true;
    }
};

#include "DracoEdgeBreakerPredictiveTopology.inl"

bool readBitBlock(Reader& reader, bool legacy_size, BitBlock *out, std::string *error)
{
    if (!out) return false;
    std::uint64_t size = 0u;
    if (!(legacy_size ? fixedU64(reader, &size) : reader.var64(&size)) || size > reader.remaining())
        return fail(error, "invalid Draco predictive bitstream size");
    const std::uint8_t *data = nullptr;
    if (!reader.bytes(static_cast<std::size_t>(size), &data))
        return fail(error, "truncated Draco predictive bitstream");
    *out = BitBlock(data, static_cast<std::size_t>(size));
    return true;
}

bool startPredictiveTraversal(
    Reader& reader,
    std::uint8_t minor,
    std::uint8_t attribute_data,
    std::uint32_t maximum_vertices,
    PredictiveTraversal *traversal,
    std::string *error)
{
    if (!traversal) return false;
    const bool legacy = minor < 2u;
    if (!readBitBlock(reader, legacy, &traversal->symbols, error)) return false;

    traversal->legacy_start_face_stream = legacy;
    traversal->legacy_attribute_connectivity = minor < 1u;
    if (legacy) {
        if (!readBitBlock(reader, true, &traversal->legacy_start_faces, error)) return false;
    } else if (!traversal->start_faces.start(reader)) {
        return fail(error, "invalid Draco predictive start-face stream");
    }

    traversal->seams.resize(attribute_data);
    for (RansBit& seam : traversal->seams)
        if (!seam.start(reader)) return fail(error, "invalid Draco predictive seam stream");

    std::int32_t predictive_split_count = 0;
    if (!reader.i32(&predictive_split_count) || predictive_split_count < 0 ||
        static_cast<std::uint32_t>(predictive_split_count) >= maximum_vertices)
        return fail(error, "invalid Draco predictive split-symbol count");
    if (!traversal->predictions.start(reader))
        return fail(error, "invalid Draco predictive correction stream");
    return true;
}

bool parseLegacyEvents(
    const std::uint8_t *data,
    std::size_t size,
    std::uint8_t minor,
    std::uint32_t face_count,
    std::vector<SplitEvent> *splits,
    std::size_t *bytes_used,
    std::string *error)
{
    if (!data || !splits || !bytes_used) return false;
    Reader reader(data, size);
    std::uint32_t count = 0u;
    if (!reader.var32(&count) || count > face_count)
        return fail(error, "invalid legacy Draco topology split count");

    splits->clear();
    splits->reserve(count);
    int last_source = 0;
    for (std::uint32_t index = 0u; index < count; ++index) {
        std::uint32_t source_delta = 0u;
        std::uint32_t split_delta = 0u;
        if (!reader.var32(&source_delta) || !reader.var32(&split_delta) ||
            source_delta > static_cast<std::uint32_t>(INT_MAX - last_source))
            return fail(error, "truncated legacy Draco topology split");
        const int source = last_source + static_cast<int>(source_delta);
        if (split_delta > static_cast<std::uint32_t>(source))
            return fail(error, "invalid legacy Draco split delta");
        splits->push_back({source, source - static_cast<int>(split_delta), false});
        last_source = source;
    }

    if (!splits->empty()) {
        BitBlock edges(reader.current(), reader.remaining());
        for (SplitEvent& split : *splits) {
            std::uint32_t edge = 0u;
            if (!edges.read(2u, &edge)) return fail(error, "truncated legacy Draco split edge data");
            split.right = (edge & 1u) != 0u;
        }
        if (!reader.skip(edges.bytesUsed())) return fail(error, "truncated legacy Draco split edge data");
    }

    if (minor < 1u) {
        std::uint32_t hole_count = 0u;
        if (!reader.var32(&hole_count) || hole_count > face_count)
            return fail(error, "invalid legacy Draco hole event count");
        int last_hole = 0;
        for (std::uint32_t index = 0u; index < hole_count; ++index) {
            std::uint32_t delta = 0u;
            if (!reader.var32(&delta) || delta > static_cast<std::uint32_t>(INT_MAX - last_hole))
                return fail(error, "truncated legacy Draco hole events");
            last_hole += static_cast<int>(delta);
        }
    }

    *bytes_used = size - reader.remaining();
    return true;
}

bool buildTopologyOutput(
    const CornerTable& table,
    PredictiveTraversal& traversal,
    DracoEdgeBreakerTopology *topology,
    std::string *error)
{
    if (!topology) return false;
    topology->position_vertex_count = static_cast<std::uint32_t>(table.vertices);
    topology->corner_vertices.resize(table.corner_vertex.size());
    topology->opposite.resize(table.opposite.size());
    for (std::size_t index = 0u; index < table.corner_vertex.size(); ++index) {
        if (table.corner_vertex[index] < 0 ||
            static_cast<std::uint32_t>(table.corner_vertex[index]) >= topology->position_vertex_count)
            return fail(error, "invalid Draco predictive reconstructed vertex");
        topology->corner_vertices[index] = static_cast<std::uint32_t>(table.corner_vertex[index]);
        topology->opposite[index] = static_cast<std::int32_t>(table.opposite[index]);
    }
    return decodePredictiveSeams(table, traversal, topology, error);
}

} // namespace

bool decodeDracoEdgeBreakerPredictive(
    const std::uint8_t *data,
    std::size_t size,
    DracoMesh *mesh,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !mesh || size < 12u) return fail(error, "truncated Draco stream");
    *mesh = {};

    Reader reader(data, size);
    if (reader.remaining() < 5u || std::memcmp(reader.current(), "DRACO", 5u) != 0)
        return fail(error, "invalid Draco magic");
    reader.skip(5u);

    std::uint8_t major = 0u;
    std::uint8_t minor = 0u;
    std::uint8_t geometry_type = 0u;
    std::uint8_t method = 0u;
    std::uint16_t flags = 0u;
    if (!reader.u8(&major) || !reader.u8(&minor) || !reader.u8(&geometry_type) ||
        !reader.u8(&method) || !reader.u16(&flags))
        return fail(error, "truncated Draco header");
    if (major != 2u || minor > 2u) return fail(error, "unsupported Draco bitstream version");
    if (geometry_type != 1u || method != 1u)
        return fail(error, "Draco stream is not an EdgeBreaker triangular mesh");
    if ((flags & 0x8000u) != 0u && !skipMetadata(reader, error)) return false;

    std::uint8_t traversal_type = 0u;
    if (!reader.u8(&traversal_type) || traversal_type != 1u)
        return fail(error, "not a Draco predictive EdgeBreaker traversal");

    std::uint32_t new_vertices = 0u;
    if (minor < 2u && !reader.var32(&new_vertices))
        return fail(error, "truncated legacy Draco new-vertex count");
    (void)new_vertices;

    std::uint32_t encoded_vertices = 0u;
    std::uint32_t face_count = 0u;
    std::uint8_t attribute_data = 0u;
    std::uint32_t symbol_count = 0u;
    std::uint32_t split_count = 0u;
    if (!reader.var32(&encoded_vertices) || !reader.var32(&face_count) ||
        !reader.u8(&attribute_data) || !reader.var32(&symbol_count) || !reader.var32(&split_count))
        return fail(error, "truncated Draco EdgeBreaker counts");
    if (split_count > symbol_count || symbol_count > face_count || encoded_vertices > face_count * 3u ||
        encoded_vertices > UINT32_MAX - split_count)
        return fail(error, "invalid Draco EdgeBreaker counts");

    std::vector<SplitEvent> splits;
    PredictiveTraversal traversal;
    CornerTable table;
    const std::uint8_t *attribute_bytes = nullptr;
    std::size_t attribute_size = 0u;

    if (minor < 2u) {
        std::uint32_t connectivity_size = 0u;
        if (!reader.var32(&connectivity_size) || connectivity_size == 0u || connectivity_size > reader.remaining())
            return fail(error, "invalid legacy Draco connectivity size");
        const std::uint8_t *connectivity = reader.current();
        const std::size_t after_connectivity = reader.remaining() - connectivity_size;
        const std::uint8_t *events = connectivity + connectivity_size;
        std::size_t event_bytes = 0u;
        if (!parseLegacyEvents(events, after_connectivity, minor, face_count, &splits, &event_bytes, error))
            return false;

        Reader traversal_reader(connectivity, connectivity_size);
        if (!startPredictiveTraversal(
                traversal_reader,
                minor,
                attribute_data,
                encoded_vertices + split_count,
                &traversal,
                error))
            return false;
        if (traversal_reader.remaining() != 0u)
            return fail(error, "legacy Draco predictive connectivity has trailing bytes");

        if (!decodePredictiveTopology(
                symbol_count,
                face_count,
                encoded_vertices,
                split_count,
                splits,
                traversal,
                &table,
                error))
            return false;

        attribute_bytes = events + event_bytes;
        attribute_size = after_connectivity - event_bytes;
    } else {
        if (!parseSplits(reader, &splits, error)) return false;
        if (!startPredictiveTraversal(
                reader,
                minor,
                attribute_data,
                encoded_vertices + split_count,
                &traversal,
                error))
            return false;
        if (!decodePredictiveTopology(
                symbol_count,
                face_count,
                encoded_vertices,
                split_count,
                splits,
                traversal,
                &table,
                error))
            return false;
        attribute_bytes = reader.current();
        attribute_size = reader.remaining();
    }

    DracoEdgeBreakerTopology topology;
    if (!buildTopologyOutput(table, traversal, &topology, error)) return false;
    return decodeDracoEdgeBreakerAttributes(attribute_bytes, attribute_size, topology, mesh, error);
}

} // namespace Models::Compression
