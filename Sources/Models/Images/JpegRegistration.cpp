#include "Models/Images/Jpeg.hpp"
#include "Models/Images/Registry.hpp"

namespace Models::Images {
namespace {
const Registration jpg_registration(".jpg", Jpeg::matches, Jpeg::decode);
const Registration jpeg_registration(".jpeg", Jpeg::matches, Jpeg::decode);
}
}
