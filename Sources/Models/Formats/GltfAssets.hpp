#ifndef HORSE_MODELS_FORMATS_GLTF_ASSETS_HPP
#define HORSE_MODELS_FORMATS_GLTF_ASSETS_HPP

#include "Models/Core/Material.hpp"
#include "Models/Formats/GltfData.hpp"

#include <string>
#include <vector>

namespace Models::Formats::GltfAssets {

struct TextureRecord {
    std::vector<int> sources;
    int sampler = -1;
};

struct Context {
    GltfData::Context data;
    std::vector<SamplerData> samplers;
    std::vector<TextureRecord> textures;
    std::vector<TextureHandle> image_cache;
    std::vector<MaterialData> materials;
};

bool load(Context *context, std::string *error = nullptr);
bool textureInfo(Context *context, const GltfJson::Value *value, TextureInfo *out, std::string *error = nullptr);

} // namespace Models::Formats::GltfAssets

#endif
