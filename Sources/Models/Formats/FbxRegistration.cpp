#include "Models/Formats/Fbx.hpp"
#include "Models/Formats/FbxSanitize.hpp"
#include "Models/Formats/Registry.hpp"

#include <utility>

namespace Models::Formats {
namespace {

bool loadFbx(const std::string& path, Document *output, std::string *error)
{
    if (!output) return false;
    Fbx::Document source;
    if (!Fbx::load(path, &source, error)) return false;
    Fbx::sanitize(&source);
    if (source.parts.empty()) {
        if (error) *error = "FBX contains no valid renderable triangles after validation: " + path;
        return false;
    }

    output->parts.clear();
    output->parts.reserve(source.parts.size());
    for (Fbx::Part& part : source.parts)
        output->parts.push_back(Part{std::move(part.mesh), std::move(part.material)});
    output->skeleton = std::move(source.skeleton);
    output->animations = std::move(source.animations);
    output->has_skeleton = source.has_skeleton;
    return true;
}

const Registration registration(".fbx", loadFbx);

} // namespace
} // namespace Models::Formats
