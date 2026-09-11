#include "Models/Formats/Gltf.hpp"
#include "Models/Formats/Registry.hpp"

namespace Models::Formats {
namespace {

const Registration gltf_registration(".gltf", Gltf::load);
const Registration glb_registration(".glb", Gltf::load);

} // namespace
} // namespace Models::Formats
