#include "Models/Images/Ktx2.hpp"
#include "Models/Images/Registry.hpp"

namespace Models::Images {
namespace {

const Registration registration(".ktx2", Ktx2::matches, Ktx2::decode);

} // namespace
} // namespace Models::Images
