#ifndef HORSE_MODELS_FORMATS_REGISTRY_HPP
#define HORSE_MODELS_FORMATS_REGISTRY_HPP

#include "Animation/Animation.hpp"
#include "Models/Models.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Models::Formats {

struct Part {
    MeshData mesh;
    MaterialData material;
};

struct Document {
    std::vector<Part> parts;
    Animation::Skeleton skeleton;
    std::vector<Animation::AnimationClip> animations;
    bool has_skeleton = false;
};

using Loader = bool (*)(const std::string&, Document *, std::string *);

bool registerLoader(std::string extension, Loader loader);
Loader loaderFor(std::string_view extension);

class Registration {
public:
    Registration(const char *extension, Loader loader);
};

} // namespace Models::Formats

#endif
