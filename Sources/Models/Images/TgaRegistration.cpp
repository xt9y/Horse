#include "Models/Images/Registry.hpp"
#include "Models/Images/Tga.hpp"

namespace Models::Images {
namespace {
const Registration registration(".tga", Tga::matches, Tga::decode);
}
}
