#ifndef RW_ENGINE_MODELS_FORMATS_FBX_SANITIZE_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_SANITIZE_HPP

#include "Models/Formats/Fbx.hpp"

namespace Models::Fbx {

// Repairs invalid/non-unit normals and FBX normal/winding hemisphere mismatches,
// removes non-finite/degenerate triangles, and recomputes bounds. Skin/UV data
// and valid authored smooth normals are preserved.
void sanitize(Document *document);

} // namespace Models::Fbx

#endif
