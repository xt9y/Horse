#include "Models/Compression/Draco.hpp"
#include "Models/Compression/DracoEdgeBreakerComplete.hpp"
#include "Models/Compression/DracoEdgeBreakerValence.hpp"
#include "Models/Compression/DracoSequentialComplete.hpp"

#include <cstring>

namespace Models::Compression {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

} // namespace

bool decodeDracoAny(
    const std::uint8_t *data,
    std::size_t size,
    DracoMesh *mesh,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !mesh || size < 11u) return fail(error, "truncated Draco stream");
    if (std::memcmp(data, "DRACO", 5u) != 0) return fail(error, "invalid Draco magic");
    if (data[5] != 2u || data[6] > 2u) return fail(error, "unsupported Draco bitstream version");
    if (data[7] != 1u) return fail(error, "Draco stream is not a triangular mesh");

    switch (data[8]) {
        case 0u:
            return decodeDracoSequentialComplete(data, size, mesh, error);
        case 1u:
            if (size < 12u) return fail(error, "truncated Draco EdgeBreaker stream");
            switch (data[11]) {
                case 0u:
                    return decodeDracoEdgeBreakerComplete(data, size, mesh, error);
                case 2u:
                    return decodeDracoEdgeBreakerValence(data, size, mesh, error);
                case 1u:
                    return fail(error, "legacy predictive Draco EdgeBreaker traversal is not supported");
                default:
                    return fail(error, "unknown Draco EdgeBreaker traversal encoding");
            }
        default:
            return fail(error, "unsupported Draco mesh encoding method");
    }
}

} // namespace Models::Compression
