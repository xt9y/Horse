#include "Models/Core/Texture.hpp"
#include "Models/Internal/StagedModel.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

int main()
{
    using namespace Models;

    clearTextureCache();
    const std::size_t global_before = Internal::textureStorageCount();

    Internal::StagedModel staged;
    TextureHandle file = INVALID_TEXTURE;
    TextureHandle memory = INVALID_TEXTURE;
    TextureHandle channel = INVALID_TEXTURE;
    TextureHandle opacity = INVALID_TEXTURE;
    {
        Internal::StagingScope scope(staged);
        Internal::TextureImportScope import;

        file = loadTexture("Assets/staged.png");
        const std::uint8_t bytes[] = {0x89u, 0x50u, 0x4eu, 0x47u};
        memory = loadTextureMemory("staged-memory", bytes, sizeof(bytes));
        channel = Internal::registerDeferredChannel(file, 1, "roughness");
        opacity = Internal::registerDeferredOpacity(file, memory);

        assert(file != INVALID_TEXTURE);
        assert(memory != INVALID_TEXTURE);
        assert(channel != INVALID_TEXTURE);
        assert(opacity != INVALID_TEXTURE);
        assert(file != memory);
        assert(Internal::isStagedTextureHandle(file));
        assert(Internal::isStagedTextureHandle(memory));
        assert(Internal::isStagedTextureHandle(channel));
        assert(Internal::isStagedTextureHandle(opacity));
        assert(Internal::textureStorageCount() == global_before);

        Internal::TextureSourceDescriptor descriptor;
        assert(Internal::stagedTextureDescriptor(channel, &descriptor));
        assert(descriptor.kind == Internal::TextureSourceKind::Channel);
        assert(descriptor.source == file);
        assert(descriptor.channel == 1);

        const TextureAsset *asset = texture(file);
        assert(asset);
        assert(!asset->path.empty());
    }

    assert(staged.textures.size() == 4u);
    assert(Internal::textureStorageCount() == global_before);

    Formats::Document document;
    Formats::Part part;
    MaterialData& material = part.material;
    material.diffuse_texture = file;
    material.normal_texture = file;
    material.roughness_texture = file;
    material.metallic_texture = file;
    material.ambient_occlusion_texture = file;
    material.emissive_texture = file;
    material.opacity_texture = file;
    material.base_color_info.texture = file;
    material.metallic_roughness_info.texture = file;
    material.normal_info.texture = file;
    material.occlusion_info.texture = file;
    material.emissive_info.texture = file;
    material.clearcoat_info.texture = file;
    material.clearcoat_roughness_info.texture = file;
    material.clearcoat_normal_info.texture = file;
    material.sheen_color_info.texture = file;
    material.sheen_roughness_info.texture = file;
    material.transmission_info.texture = file;
    material.thickness_info.texture = file;
    material.specular_info.texture = file;
    material.specular_color_info.texture = file;
    material.iridescence_info.texture = file;
    material.iridescence_thickness_info.texture = file;
    material.anisotropy_info.texture = file;
    material.diffuse_transmission_info.texture = file;
    material.diffuse_transmission_color_info.texture = file;
    document.parts.push_back(part);

    const TextureHandle published = 17u;
    const std::unordered_map<TextureHandle, TextureHandle> mapping{{file, published}};
    std::string error;
    assert(Internal::remapDocumentTextures(document, mapping, &error));
    assert(error.empty());

    const MaterialData& remapped = document.parts.front().material;
    assert(remapped.diffuse_texture == published);
    assert(remapped.normal_texture == published);
    assert(remapped.roughness_texture == published);
    assert(remapped.metallic_texture == published);
    assert(remapped.ambient_occlusion_texture == published);
    assert(remapped.emissive_texture == published);
    assert(remapped.opacity_texture == published);
    assert(remapped.base_color_info.texture == published);
    assert(remapped.metallic_roughness_info.texture == published);
    assert(remapped.normal_info.texture == published);
    assert(remapped.occlusion_info.texture == published);
    assert(remapped.emissive_info.texture == published);
    assert(remapped.clearcoat_info.texture == published);
    assert(remapped.clearcoat_roughness_info.texture == published);
    assert(remapped.clearcoat_normal_info.texture == published);
    assert(remapped.sheen_color_info.texture == published);
    assert(remapped.sheen_roughness_info.texture == published);
    assert(remapped.transmission_info.texture == published);
    assert(remapped.thickness_info.texture == published);
    assert(remapped.specular_info.texture == published);
    assert(remapped.specular_color_info.texture == published);
    assert(remapped.iridescence_info.texture == published);
    assert(remapped.iridescence_thickness_info.texture == published);
    assert(remapped.anisotropy_info.texture == published);
    assert(remapped.diffuse_transmission_info.texture == published);
    assert(remapped.diffuse_transmission_color_info.texture == published);

    clearTextureCache();
    return 0;
}
