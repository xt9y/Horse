#include "Models/Formats/Obj.hpp"
#include "Models/Formats/Registry.hpp"

#include <utility>

namespace Models::Formats {
namespace {

bool loadObj(const std::string& path, Document *output, std::string *error)
{
    if (!output) return false;
    Obj::Document source;
    if (!Obj::load(path, &source, error)) return false;

    output->parts.clear();
    output->parts.reserve(source.parts.size());
    for (Obj::Part& part : source.parts)
        output->parts.push_back(Part{std::move(part.mesh), std::move(part.material)});
    output->has_skeleton = false;
    output->skeleton = {};
    output->animations.clear();
    return true;
}

const Registration registration(".obj", loadObj);

} // namespace
} // namespace Models::Formats
