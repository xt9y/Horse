#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_FILTER_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_FILTER_HPP

#include "Models/Images/WebpVp8Frame.hpp"
#include "Models/Images/WebpVp8Modes.hpp"
#include "Models/Images/WebpVp8Reconstruct.hpp"

#include <string>

namespace Models::Images::WebpVp8Internal {

bool filterFrame(
    const KeyFrame& frame,
    const MacroblockGrid& modes,
    YuvFrame *image,
    std::string *error = nullptr
);

} // namespace Models::Images::WebpVp8Internal

#endif
