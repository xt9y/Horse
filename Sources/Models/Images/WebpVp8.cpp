#include "Models/Images/WebpVp8.hpp"

#include "Models/Images/WebpVp8Filter.hpp"
#include "Models/Images/WebpVp8Frame.hpp"
#include "Models/Images/WebpVp8Modes.hpp"
#include "Models/Images/WebpVp8Reconstruct.hpp"
#include "Models/Images/WebpVp8Residue.hpp"

namespace Models::Images::WebpVp8 {

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image) {
        if (error) *error = "null VP8 image output";
        return false;
    }

    WebpVp8Internal::KeyFrame frame;
    if (!WebpVp8Internal::parseKeyFrame(data, size, &frame, error)) return false;

    WebpVp8Internal::MacroblockGrid modes;
    if (!WebpVp8Internal::decodeMacroblockModes(&frame, &modes, error)) return false;

    WebpVp8Internal::ResidueGrid residue;
    if (!WebpVp8Internal::decodeResidue(frame, modes, &residue, error)) return false;

    WebpVp8Internal::YuvFrame reconstructed;
    if (!WebpVp8Internal::reconstruct(frame, modes, residue, &reconstructed, error)) return false;
    if (!WebpVp8Internal::filterFrame(frame, modes, &reconstructed, error)) return false;
    return WebpVp8Internal::toRgba(reconstructed, image, error);
}

} // namespace Models::Images::WebpVp8
