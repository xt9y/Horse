#include "Models/Images/Png.hpp"
#include "Models/Images/Registry.hpp"

namespace Models::Images {
namespace {
const Registration registration(".png", Png::matches, Png::decode);
}
}
