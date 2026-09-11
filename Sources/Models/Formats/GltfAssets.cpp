#include "Models/Formats/GltfAssets.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Models::Formats::GltfAssets {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

void preserveObject(
    const Value *source,
    std::string *extras,
    std::unordered_map<std::string, std::string> *extensions)
{
    if (!source || !source->is(Type::Object)) return;
    if (extras) {
        if (const Value *value = source->get("extras")) *extras = GltfJson::stringify(*value);
    }
    if (!extensions) return;
    const Value *value = source->get("extensions");
    if (!value || !value->is(Type::Object)) return;
    for (const auto& [name, extension] : value->object)
        extensions->insert_or_assign(name, GltfJson::stringify(extension));
}

SamplerData samplerAt(const Context& context, int index)
{
    if (index >= 0 && static_cast<std::size_t>(index) < context.samplers.size())
        return context.samplers[static_cast<std::size_t>(index)];
    return {};
}

bool rawBufferView(
    const Context& context,
    int view_index,
    const std::uint8_t **data,
    std::size_t *size,
    std::string *error)
{
    if (!data || !size || view_index < 0 || static_cast<std::size_t>(view_index) >= context.data.views.size())
        return fail(error, "glTF image references invalid bufferView");
    const GltfData::BufferView& view = context.data.views[static_cast<std::size_t>(view_index)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= context.data.buffers.size())
        return fail(error, "glTF image bufferView references invalid buffer");
    const auto& buffer = context.data.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.offset > buffer.size() || view.length > buffer.size() - view.offset)
        return fail(error, "glTF image bufferView exceeds buffer bounds");
    *data = buffer.data() + view.offset;
    *size = view.length;
    return true;
}

bool imageHandle(Context *context, int index, TextureHandle *out, std::string *error)
{
    if (!context || !out || !context->data.root || index < 0)
        return fail(error, "invalid glTF image request");
    const Value *images = context->data.root->get("images");
    if (!images || !images->is(Type::Array) || static_cast<std::size_t>(index) >= images->array.size())
        return fail(error, "glTF texture references invalid image");
    if (context->image_cache.empty()) context->image_cache.assign(images->array.size(), INVALID_TEXTURE);
    TextureHandle& cached = context->image_cache[static_cast<std::size_t>(index)];
    if (cached != INVALID_TEXTURE) {
        *out = cached;
        return true;
    }

    const Value& image = images->array[static_cast<std::size_t>(index)];
    if (!image.is(Type::Object)) return fail(error, "invalid glTF image object");
    const std::string key = context->data.path + "#image:" + std::to_string(index);
    std::string local_error;

    const Value *uri = image.get("uri");
    if (uri) {
        if (!uri->is(Type::String)) return fail(error, "glTF image URI must be a string");
        if (uri->string.starts_with("data:")) {
            std::vector<std::uint8_t> bytes;
            if (!GltfData::decodeDataUri(uri->string, &bytes, error)) return false;
            cached = loadTextureMemory(key, bytes.data(), bytes.size(), &local_error);
        } else {
            std::string decoded;
            if (!GltfData::decodeUriPath(uri->string, &decoded, error)) return false;
            cached = loadTexture(
                (context->data.directory / std::filesystem::path(decoded)).lexically_normal().string(),
                &local_error
            );
        }
    } else {
        const int view = GltfJson::integer(image.get("bufferView"));
        const std::uint8_t *bytes = nullptr;
        std::size_t size = 0u;
        if (!rawBufferView(*context, view, &bytes, &size, error)) return false;
        cached = loadTextureMemory(key, bytes, size, &local_error);
    }

    if (cached == INVALID_TEXTURE) {
        if (error) *error = local_error.empty() ? "failed to decode glTF image" : local_error;
        return false;
    }
    *out = cached;
    return true;
}

bool textureHandle(Context *context, int index, TextureHandle *out, SamplerData *sampler, std::string *error)
{
    if (!context || !out || index < 0 || static_cast<std::size_t>(index) >= context->textures.size())
        return fail(error, "glTF material references invalid texture");
    const TextureRecord& texture_record = context->textures[static_cast<std::size_t>(index)];
    if (sampler) *sampler = samplerAt(*context, texture_record.sampler);
    std::string last_error;
    for (const int source : texture_record.sources) {
        TextureHandle handle = INVALID_TEXTURE;
        std::string candidate_error;
        if (imageHandle(context, source, &handle, &candidate_error)) {
            *out = handle;
            return true;
        }
        if (!candidate_error.empty()) last_error = std::move(candidate_error);
    }
    return fail(error, last_error.empty() ? "glTF texture has no decodable source" : last_error);
}

TextureHandle derivedChannel(TextureHandle source, int channel, const char *label, std::string *error)
{
    const TextureAsset *asset = texture(source);
    if (!asset || channel < 0 || channel > 3) return INVALID_TEXTURE;
    Images::Image image = asset->image;
    for (std::size_t offset = 0u; offset + 3u < image.rgba.size(); offset += 4u) {
        const std::uint8_t value = image.rgba[offset + static_cast<std::size_t>(channel)];
        image.rgba[offset + 0u] = value;
        image.rgba[offset + 1u] = value;
        image.rgba[offset + 2u] = value;
        image.rgba[offset + 3u] = 255u;
    }
    image.meaningful_alpha = false;
    return registerTextureImage(asset->path + "\n@gltf-channel:" + label, std::move(image), error);
}

TextureHandle alphaNormalized(TextureHandle source, AlphaMode mode, float cutoff, std::string *error)
{
    const TextureAsset *asset = texture(source);
    if (!asset) return INVALID_TEXTURE;
    if (mode == AlphaMode::Blend) return source;
    if (mode == AlphaMode::Opaque && !asset->image.meaningful_alpha) return source;

    Images::Image image = asset->image;
    if (mode == AlphaMode::Opaque) {
        for (std::size_t offset = 3u; offset < image.rgba.size(); offset += 4u) image.rgba[offset] = 255u;
        image.meaningful_alpha = false;
    } else {
        const int threshold = std::clamp(static_cast<int>(std::lround(cutoff * 255.0f)), 0, 255);
        for (std::size_t offset = 3u; offset < image.rgba.size(); offset += 4u)
            image.rgba[offset] = image.rgba[offset] >= threshold ? 255u : 0u;
        image.meaningful_alpha = true;
    }
    const std::string suffix = mode == AlphaMode::Opaque ? "opaque" : "mask:" + std::to_string(cutoff);
    return registerTextureImage(asset->path + "\n@gltf-alpha:" + suffix, std::move(image), error);
}

void pathFor(TextureHandle handle, std::string *out)
{
    if (!out || handle == INVALID_TEXTURE) return;
    if (const TextureAsset *asset = texture(handle)) *out = asset->path;
}

void vec3(const Value *value, Vec3 *out, Vec3 fallback)
{
    if (!out) return;
    *out = fallback;
    if (!value || !value->is(Type::Array) || value->array.size() < 3u) return;
    out->x = GltfJson::floatValue(&value->array[0], fallback.x);
    out->y = GltfJson::floatValue(&value->array[1], fallback.y);
    out->z = GltfJson::floatValue(&value->array[2], fallback.z);
}

bool parseSamplers(Context *context, std::string *error)
{
    const Value *values = context->data.root->get("samplers");
    if (!values) return true;
    if (!values->is(Type::Array)) return fail(error, "glTF samplers must be an array");
    context->samplers.reserve(values->array.size());
    for (const Value& source : values->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF sampler object");
        SamplerData sampler;
        sampler.mag_filter = GltfJson::integer(source.get("magFilter"), 9729);
        sampler.min_filter = GltfJson::integer(source.get("minFilter"), 9987);
        sampler.wrap_s = GltfJson::integer(source.get("wrapS"), 10497);
        sampler.wrap_t = GltfJson::integer(source.get("wrapT"), 10497);
        context->samplers.push_back(sampler);
    }
    return true;
}

void addSource(std::vector<int> *sources, int source)
{
    if (!sources || source < 0) return;
    if (std::find(sources->begin(), sources->end(), source) == sources->end()) sources->push_back(source);
}

bool parseTextures(Context *context, std::string *error)
{
    const Value *values = context->data.root->get("textures");
    if (!values) return true;
    if (!values->is(Type::Array)) return fail(error, "glTF textures must be an array");
    context->textures.reserve(values->array.size());
    for (const Value& source : values->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF texture object");
        TextureRecord record;
        record.sampler = GltfJson::integer(source.get("sampler"));
        const Value *extensions = source.get("extensions");
        if (extensions && extensions->is(Type::Object)) {
            static constexpr const char *names[] = {
                "KHR_texture_basisu",
                "EXT_texture_webp",
                "MSFT_texture_dds",
            };
            for (const char *name : names) {
                const Value *extension = extensions->get(name);
                if (extension && extension->is(Type::Object))
                    addSource(&record.sources, GltfJson::integer(extension->get("source")));
            }
        }
        addSource(&record.sources, GltfJson::integer(source.get("source")));
        if (record.sources.empty()) return fail(error, "glTF texture has no image source");
        context->textures.push_back(std::move(record));
    }
    return true;
}

bool materialTexture(
    Context *context,
    const Value *source,
    TextureInfo *info,
    TextureHandle *legacy,
    std::string *legacy_path,
    std::string *error)
{
    if (!source) return true;
    if (!textureInfo(context, source, info, error)) return false;
    if (legacy) *legacy = info->texture;
    if (legacy_path) pathFor(info->texture, legacy_path);
    return true;
}

bool parseMaterialExtensions(Context *context, const Value& source, MaterialData *material, std::string *error)
{
    if (!material) return false;
    const Value *extensions = source.get("extensions");
    if (!extensions) return true;
    if (!extensions->is(Type::Object)) return fail(error, "glTF material extensions must be an object");

    if (extensions->get("KHR_materials_unlit")) material->unlit = true;

    if (const Value *ext = extensions->get("KHR_materials_emissive_strength"); ext && ext->is(Type::Object))
        material->emissive_strength = std::max(GltfJson::floatValue(ext->get("emissiveStrength"), 1.0f), 0.0f);

    if (const Value *ext = extensions->get("KHR_materials_ior"); ext && ext->is(Type::Object))
        material->ior = std::max(GltfJson::floatValue(ext->get("ior"), 1.5f), 1.0f);

    if (const Value *ext = extensions->get("KHR_materials_clearcoat"); ext && ext->is(Type::Object)) {
        material->clearcoat = std::clamp(GltfJson::floatValue(ext->get("clearcoatFactor")), 0.0f, 1.0f);
        material->clearcoat_roughness = std::clamp(GltfJson::floatValue(ext->get("clearcoatRoughnessFactor")), 0.0f, 1.0f);
        if (!materialTexture(context, ext->get("clearcoatTexture"), &material->clearcoat_info, nullptr, nullptr, error) ||
            !materialTexture(context, ext->get("clearcoatRoughnessTexture"), &material->clearcoat_roughness_info, nullptr, nullptr, error) ||
            !materialTexture(context, ext->get("clearcoatNormalTexture"), &material->clearcoat_normal_info, nullptr, nullptr, error))
            return false;
        if (const Value *normal = ext->get("clearcoatNormalTexture"); normal && normal->is(Type::Object))
            material->clearcoat_normal_info.scale = GltfJson::floatValue(normal->get("scale"), 1.0f);
    }

    if (const Value *ext = extensions->get("KHR_materials_sheen"); ext && ext->is(Type::Object)) {
        vec3(ext->get("sheenColorFactor"), &material->sheen_color, {0.0f, 0.0f, 0.0f});
        material->sheen_roughness = std::clamp(GltfJson::floatValue(ext->get("sheenRoughnessFactor")), 0.0f, 1.0f);
        if (!materialTexture(context, ext->get("sheenColorTexture"), &material->sheen_color_info, nullptr, nullptr, error) ||
            !materialTexture(context, ext->get("sheenRoughnessTexture"), &material->sheen_roughness_info, nullptr, nullptr, error))
            return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_specular"); ext && ext->is(Type::Object)) {
        material->specular = std::clamp(GltfJson::floatValue(ext->get("specularFactor"), 1.0f), 0.0f, 1.0f);
        vec3(ext->get("specularColorFactor"), &material->specular_color, {1.0f, 1.0f, 1.0f});
        if (!materialTexture(context, ext->get("specularTexture"), &material->specular_info, nullptr, nullptr, error) ||
            !materialTexture(context, ext->get("specularColorTexture"), &material->specular_color_info, nullptr, nullptr, error))
            return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_transmission"); ext && ext->is(Type::Object)) {
        material->transmission = std::clamp(GltfJson::floatValue(ext->get("transmissionFactor")), 0.0f, 1.0f);
        if (!materialTexture(context, ext->get("transmissionTexture"), &material->transmission_info, nullptr, nullptr, error)) return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_volume"); ext && ext->is(Type::Object)) {
        material->thickness = std::max(GltfJson::floatValue(ext->get("thicknessFactor")), 0.0f);
        material->attenuation_distance = std::max(GltfJson::floatValue(ext->get("attenuationDistance")), 0.0f);
        vec3(ext->get("attenuationColor"), &material->attenuation_color, {1.0f, 1.0f, 1.0f});
        if (!materialTexture(context, ext->get("thicknessTexture"), &material->thickness_info, nullptr, nullptr, error)) return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_iridescence"); ext && ext->is(Type::Object)) {
        material->iridescence = std::clamp(GltfJson::floatValue(ext->get("iridescenceFactor")), 0.0f, 1.0f);
        material->iridescence_ior = std::max(GltfJson::floatValue(ext->get("iridescenceIor"), 1.3f), 1.0f);
        material->iridescence_thickness_min = std::max(GltfJson::floatValue(ext->get("iridescenceThicknessMinimum"), 100.0f), 0.0f);
        material->iridescence_thickness_max = std::max(
            GltfJson::floatValue(ext->get("iridescenceThicknessMaximum"), 400.0f),
            material->iridescence_thickness_min
        );
        if (!materialTexture(context, ext->get("iridescenceTexture"), &material->iridescence_info, nullptr, nullptr, error) ||
            !materialTexture(context, ext->get("iridescenceThicknessTexture"), &material->iridescence_thickness_info, nullptr, nullptr, error))
            return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_anisotropy"); ext && ext->is(Type::Object)) {
        material->anisotropy_strength = std::clamp(GltfJson::floatValue(ext->get("anisotropyStrength")), 0.0f, 1.0f);
        material->anisotropy_rotation = GltfJson::floatValue(ext->get("anisotropyRotation"));
        if (!materialTexture(context, ext->get("anisotropyTexture"), &material->anisotropy_info, nullptr, nullptr, error)) return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_dispersion"); ext && ext->is(Type::Object))
        material->dispersion = std::max(GltfJson::floatValue(ext->get("dispersion")), 0.0f);

    if (const Value *ext = extensions->get("KHR_materials_diffuse_transmission"); ext && ext->is(Type::Object)) {
        material->diffuse_transmission = std::clamp(GltfJson::floatValue(ext->get("diffuseTransmissionFactor")), 0.0f, 1.0f);
        vec3(ext->get("diffuseTransmissionColorFactor"), &material->diffuse_transmission_color, {1.0f, 1.0f, 1.0f});
        if (!materialTexture(context, ext->get("diffuseTransmissionTexture"), &material->diffuse_transmission_info, nullptr, nullptr, error) ||
            !materialTexture(context, ext->get("diffuseTransmissionColorTexture"), &material->diffuse_transmission_color_info, nullptr, nullptr, error))
            return false;
    }

    if (const Value *ext = extensions->get("KHR_materials_pbrSpecularGlossiness"); ext && ext->is(Type::Object)) {
        if (const Value *factor = ext->get("diffuseFactor"); factor && factor->is(Type::Array) && factor->array.size() >= 4u) {
            material->color = {
                GltfJson::floatValue(&factor->array[0], 1.0f),
                GltfJson::floatValue(&factor->array[1], 1.0f),
                GltfJson::floatValue(&factor->array[2], 1.0f),
            };
            material->opacity = std::clamp(GltfJson::floatValue(&factor->array[3], 1.0f), 0.0f, 1.0f);
        }
        vec3(ext->get("specularFactor"), &material->specular_color, {1.0f, 1.0f, 1.0f});
        material->roughness = 1.0f - std::clamp(GltfJson::floatValue(ext->get("glossinessFactor"), 1.0f), 0.0f, 1.0f);
        if (!materialTexture(context, ext->get("diffuseTexture"), &material->base_color_info,
            &material->diffuse_texture, &material->texture_path, error)) return false;
    }
    return true;
}

bool parseMaterials(Context *context, std::string *error)
{
    const Value *values = context->data.root->get("materials");
    if (!values) return true;
    if (!values->is(Type::Array)) return fail(error, "glTF materials must be an array");
    context->materials.reserve(values->array.size());

    for (std::size_t index = 0u; index < values->array.size(); ++index) {
        const Value& source = values->array[index];
        if (!source.is(Type::Object)) return fail(error, "invalid glTF material object");
        MaterialData material;
        material.name = GltfJson::stringValue(source.get("name"), "gltf_material_" + std::to_string(index));
        material.color = {1.0f, 1.0f, 1.0f};
        material.opacity = 1.0f;
        material.roughness = 1.0f;
        material.metallic = 1.0f;
        material.ambient_occlusion = 1.0f;
        material.emissive_strength = 1.0f;
        material.alpha_cutoff = std::clamp(GltfJson::floatValue(source.get("alphaCutoff"), 0.5f), 0.0f, 1.0f);
        material.double_sided = GltfJson::boolValue(source.get("doubleSided"));
        const std::string alpha = GltfJson::stringValue(source.get("alphaMode"), "OPAQUE");
        material.alpha_mode = alpha == "MASK" ? AlphaMode::Mask : (alpha == "BLEND" ? AlphaMode::Blend : AlphaMode::Opaque);

        const Value *pbr = source.get("pbrMetallicRoughness");
        if (pbr) {
            if (!pbr->is(Type::Object)) return fail(error, "glTF pbrMetallicRoughness must be an object");
            if (const Value *factor = pbr->get("baseColorFactor"); factor && factor->is(Type::Array) && factor->array.size() >= 4u) {
                material.color = {
                    GltfJson::floatValue(&factor->array[0], 1.0f),
                    GltfJson::floatValue(&factor->array[1], 1.0f),
                    GltfJson::floatValue(&factor->array[2], 1.0f),
                };
                material.opacity = std::clamp(GltfJson::floatValue(&factor->array[3], 1.0f), 0.0f, 1.0f);
            }
            material.metallic = std::clamp(GltfJson::floatValue(pbr->get("metallicFactor"), 1.0f), 0.0f, 1.0f);
            material.roughness = std::clamp(GltfJson::floatValue(pbr->get("roughnessFactor"), 1.0f), 0.0f, 1.0f);
            if (!materialTexture(context, pbr->get("baseColorTexture"), &material.base_color_info,
                nullptr, nullptr, error)) return false;
            if (material.base_color_info.texture != INVALID_TEXTURE) {
                material.diffuse_texture = alphaNormalized(
                    material.base_color_info.texture,
                    material.alpha_mode,
                    material.alpha_cutoff,
                    error
                );
                if (material.diffuse_texture == INVALID_TEXTURE) return false;
                pathFor(material.diffuse_texture, &material.texture_path);
            }
            if (!materialTexture(context, pbr->get("metallicRoughnessTexture"), &material.metallic_roughness_info,
                nullptr, nullptr, error)) return false;
            if (material.metallic_roughness_info.texture != INVALID_TEXTURE) {
                material.roughness_texture = derivedChannel(material.metallic_roughness_info.texture, 1, "roughness", error);
                material.metallic_texture = derivedChannel(material.metallic_roughness_info.texture, 2, "metallic", error);
                if (material.roughness_texture == INVALID_TEXTURE || material.metallic_texture == INVALID_TEXTURE) return false;
                pathFor(material.roughness_texture, &material.roughness_texture_path);
                pathFor(material.metallic_texture, &material.metallic_texture_path);
            }
        }
        if (material.alpha_mode == AlphaMode::Opaque) material.opacity = 1.0f;

        if (!materialTexture(context, source.get("normalTexture"), &material.normal_info,
            &material.normal_texture, &material.normal_texture_path, error)) return false;
        if (const Value *normal = source.get("normalTexture"); normal && normal->is(Type::Object)) {
            material.normal_scale = GltfJson::floatValue(normal->get("scale"), 1.0f);
            material.normal_info.scale = material.normal_scale;
        }

        if (!materialTexture(context, source.get("occlusionTexture"), &material.occlusion_info,
            &material.ambient_occlusion_texture, &material.ambient_occlusion_texture_path, error)) return false;
        if (const Value *occlusion = source.get("occlusionTexture"); occlusion && occlusion->is(Type::Object)) {
            material.ambient_occlusion = std::clamp(GltfJson::floatValue(occlusion->get("strength"), 1.0f), 0.0f, 1.0f);
            material.occlusion_info.scale = material.ambient_occlusion;
        }

        vec3(source.get("emissiveFactor"), &material.emissive_color, {0.0f, 0.0f, 0.0f});
        if (!materialTexture(context, source.get("emissiveTexture"), &material.emissive_info,
            &material.emissive_texture, &material.emissive_texture_path, error)) return false;

        if (!parseMaterialExtensions(context, source, &material, error)) return false;
        preserveObject(&source, &material.extras_json, &material.extensions_json);
        context->materials.push_back(std::move(material));
    }
    return true;
}

} // namespace

bool textureInfo(Context *context, const Value *value, TextureInfo *out, std::string *error)
{
    if (!out) return fail(error, "glTF texture info output is null");
    *out = {};
    if (!value) return true;
    if (!context || !value->is(Type::Object)) return fail(error, "invalid glTF texture info");
    const int index = GltfJson::integer(value->get("index"));
    if (index < 0) return fail(error, "glTF texture info has invalid index");
    if (!textureHandle(context, index, &out->texture, &out->sampler, error)) return false;
    const int texcoord = GltfJson::integer(value->get("texCoord"), 0);
    if (texcoord < 0) return fail(error, "glTF texture texCoord must be nonnegative");
    out->texcoord = static_cast<std::uint32_t>(texcoord);

    const Value *extensions = value->get("extensions");
    if (extensions && extensions->is(Type::Object)) {
        const Value *transform = extensions->get("KHR_texture_transform");
        if (transform && transform->is(Type::Object)) {
            if (const Value *offset = transform->get("offset"); offset && offset->is(Type::Array) && offset->array.size() >= 2u)
                out->transform.offset = {
                    GltfJson::floatValue(&offset->array[0]),
                    GltfJson::floatValue(&offset->array[1]),
                };
            if (const Value *scale = transform->get("scale"); scale && scale->is(Type::Array) && scale->array.size() >= 2u)
                out->transform.scale = {
                    GltfJson::floatValue(&scale->array[0], 1.0f),
                    GltfJson::floatValue(&scale->array[1], 1.0f),
                };
            out->transform.rotation = GltfJson::floatValue(transform->get("rotation"));
            out->transform.texcoord = GltfJson::integer(transform->get("texCoord"), -1);
            if (out->transform.texcoord >= 0) out->texcoord = static_cast<std::uint32_t>(out->transform.texcoord);
        }
    }
    return true;
}

bool load(Context *context, std::string *error)
{
    if (error) error->clear();
    if (!context || !context->data.root) return fail(error, "glTF asset context is null");
    context->samplers.clear();
    context->textures.clear();
    context->image_cache.clear();
    context->materials.clear();
    return parseSamplers(context, error) && parseTextures(context, error) && parseMaterials(context, error);
}

} // namespace Models::Formats::GltfAssets
