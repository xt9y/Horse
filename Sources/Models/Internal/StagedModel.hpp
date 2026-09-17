#ifndef HORSE_MODELS_INTERNAL_STAGED_MODEL_HPP
#define HORSE_MODELS_INTERNAL_STAGED_MODEL_HPP

#include "Models/Formats/Registry.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Models::Internal {

inline constexpr TextureHandle StagedTextureBase = 0x80000000u;

struct StagedTexture
{
    TextureSourceDescriptor descriptor;
    TextureAsset asset;
};

struct StagedModel
{
    Formats::Document document;
    std::vector<StagedTexture> textures;
    std::unordered_map<std::string, TextureHandle> texture_lookup;
};

class StagingScope
{
public:
    explicit StagingScope(StagedModel& model);
    ~StagingScope();
    StagingScope(const StagingScope&) = delete;
    StagingScope& operator=(const StagingScope&) = delete;

private:
    StagedModel *previous_ = nullptr;
};

bool stagingActive();
bool isStagedTextureHandle(TextureHandle handle);
TextureHandle registerStagedTexture(TextureSourceDescriptor descriptor);
TextureHandle findStagedTexture(const std::string& key);
const TextureAsset *stagedTexture(TextureHandle handle);
bool stagedTextureDescriptor(TextureHandle handle, TextureSourceDescriptor *descriptor);

bool materializeStagedTextures(
    const StagedModel& staged,
    std::unordered_map<TextureHandle, TextureHandle> *mapping,
    std::string *error = nullptr
);

bool remapDocumentTextures(
    Formats::Document& document,
    const std::unordered_map<TextureHandle, TextureHandle>& mapping,
    std::string *error = nullptr
);

} // namespace Models::Internal

#endif
