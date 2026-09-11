#include "Models/Images/Registry.hpp"
#include "Models/Images/Webp.hpp"

namespace Models::Images {
namespace {

const Registration webp_registration(".webp", Webp::matches, Webp::decode);

} // namespace
} // namespace Models::Images
