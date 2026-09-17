#include "Models/Internal/TextureStreaming.hpp"

#include "Core/Jobs/Jobs.hpp"
#include "Models/Core/Material.hpp"
#include "Models/Internal/ResourceRevision.hpp"
#include "Models/Internal/TextureStorage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Models::Internal {
namespace {

thread_local std::size_t import_depth = 0u;
constexpr std::size_t MaximumInFlightTextureJobs = 8u;

struct DecodeResult
{
    bool ok = false;
    Images::Image image;
    std::string error;
};

struct Record
{
    TextureSourceDescriptor descriptor;
    TextureState state = TextureState::Registered;
    std::uint64_t generation = 0u;
    bool diagnostic_emitted = false;
};

struct Runtime
{
    std::unordered_map<TextureHandle, Record> records;
    std::uint64_t generation = 1u;
    std::size_t in_flight = 0u;
};

Runtime& runtime()
{
    static Runtime value;
    return value;
}

Core::Jobs::Group textureJobGroup()
{
    static const Core::Jobs::Group value = Core::Jobs::createGroup();
    return value;
}

bool validImage(const Images::Image& image)
{
    if (image.width <= 0 || image.height <= 0) return false;
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    return width <= SIZE_MAX / height && width * height <= SIZE_MAX / 4u &&
        image.rgba.size() == width * height * 4u;
}

TextureState stateFor(TextureHandle handle)
{
    const auto found = runtime().records.find(handle);
    if (found != runtime().records.end()) return found->second.state;
    return textureStorageReady(handle) ? TextureState::Ready : TextureState::Registered;
}

bool dependencyReady(TextureHandle handle)
{
    return handle != INVALID_TEXTURE &&
        stateFor(handle) == TextureState::Ready &&
        textureStorageReady(handle);
}

bool dependencyFailed(TextureHandle handle)
{
    return handle != INVALID_TEXTURE && stateFor(handle) == TextureState::Failed;
}

bool applyOpacity(Images::Image *color, const Images::Image& opacity, std::string *error)
{
    if (!color || !validImage(*color) || !validImage(opacity)) {
        if (error) *error = "invalid color or opacity texture image";
        return false;
    }

    for (int y = 0; y < color->height; ++y) {
        const int opacity_y = std::clamp(
            static_cast<int>(
                (static_cast<long long>(y) * static_cast<long long>(opacity.height)) /
                static_cast<long long>(color->height)
            ),
            0,
            opacity.height - 1
        );
        for (int x = 0; x < color->width; ++x) {
            const int opacity_x = std::clamp(
                static_cast<int>(
                    (static_cast<long long>(x) * static_cast<long long>(opacity.width)) /
                    static_cast<long long>(color->width)
                ),
                0,
                opacity.width - 1
            );
            const std::size_t color_offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(color->width) +
                 static_cast<std::size_t>(x)) * 4u;
            const std::size_t opacity_offset =
                (static_cast<std::size_t>(opacity_y) * static_cast<std::size_t>(opacity.width) +
                 static_cast<std::size_t>(opacity_x)) * 4u;
            const unsigned int mask =
                (static_cast<unsigned int>(opacity.rgba[opacity_offset + 0u]) +
                 static_cast<unsigned int>(opacity.rgba[opacity_offset + 1u]) +
                 static_cast<unsigned int>(opacity.rgba[opacity_offset + 2u])) / 3u;
            const unsigned int source_alpha = color->rgba[color_offset + 3u];
            color->rgba[color_offset + 3u] = static_cast<std::uint8_t>(
                (source_alpha * mask + 127u) / 255u
            );
        }
    }
    color->meaningful_alpha = true;
    return true;
}

void failRecord(Record& record, const std::string& error)
{
    if (record.state == TextureState::Failed) return;
    record.state = TextureState::Failed;
    touchResources();
    if (record.diagnostic_emitted) return;
    record.diagnostic_emitted = true;
    std::fprintf(
        stderr,
        "[Models] texture streaming failed: %s%s%s\n",
        record.descriptor.key.c_str(),
        error.empty() ? "" : ": ",
        error.c_str()
    );
}

void complete(TextureHandle handle, std::uint64_t generation, const std::shared_ptr<DecodeResult>& result)
{
    Runtime& state = runtime();
    if (generation != state.generation) return;
    const auto found = state.records.find(handle);
    if (found == state.records.end() || found->second.generation != generation) return;

    if (state.in_flight != 0u) --state.in_flight;
    Record& record = found->second;
    if (result->ok && publishTexture(handle, std::move(result->image))) {
        record.state = TextureState::Ready;
        return;
    }
    failRecord(record, result->error);
}

bool schedule(TextureHandle handle)
{
    Runtime& state = runtime();
    const auto found = state.records.find(handle);
    if (found == state.records.end()) return false;
    Record& record = found->second;
    if (record.state != TextureState::Registered) return false;

    const TextureSourceDescriptor descriptor = record.descriptor;
    const TextureAsset *source = nullptr;
    const TextureAsset *secondary = nullptr;

    if (descriptor.kind == TextureSourceKind::Channel || descriptor.kind == TextureSourceKind::Alpha) {
        if (dependencyFailed(descriptor.source)) {
            failRecord(record, "source texture failed");
            return true;
        }
        if (!dependencyReady(descriptor.source)) return false;
        source = texture(descriptor.source);
    } else if (descriptor.kind == TextureSourceKind::Opacity) {
        if (dependencyFailed(descriptor.source) || dependencyFailed(descriptor.secondary)) {
            failRecord(record, "source texture failed");
            return true;
        }
        if (!dependencyReady(descriptor.source) || !dependencyReady(descriptor.secondary)) return false;
        source = texture(descriptor.source);
        secondary = texture(descriptor.secondary);
    }

    if (state.in_flight >= MaximumInFlightTextureJobs) return false;

    const std::uint64_t generation = record.generation;
    const auto result = std::make_shared<DecodeResult>();

    const bool queued = Core::Jobs::trySubmit(
        textureJobGroup(),
        [descriptor, source, secondary, result] {
            switch (descriptor.kind) {
                case TextureSourceKind::File:
                    result->ok = Images::load(descriptor.key, &result->image, &result->error);
                    break;
                case TextureSourceKind::Memory:
                    result->ok = Images::loadMemory(
                        descriptor.bytes.data(), descriptor.bytes.size(), &result->image, &result->error);
                    break;
                case TextureSourceKind::Channel: {
                    if (!source || descriptor.channel < 0 || descriptor.channel > 3) {
                        result->error = "invalid derived channel source";
                        break;
                    }
                    result->image = source->image;
                    for (std::size_t offset = 0u; offset + 3u < result->image.rgba.size(); offset += 4u) {
                        const std::uint8_t value = result->image.rgba[
                            offset + static_cast<std::size_t>(descriptor.channel)];
                        result->image.rgba[offset + 0u] = value;
                        result->image.rgba[offset + 1u] = value;
                        result->image.rgba[offset + 2u] = value;
                        result->image.rgba[offset + 3u] = 255u;
                    }
                    result->image.meaningful_alpha = false;
                    result->ok = validImage(result->image);
                    break;
                }
                case TextureSourceKind::Alpha: {
                    if (!source) {
                        result->error = "invalid alpha source";
                        break;
                    }
                    result->image = source->image;
                    const float factor = std::clamp(descriptor.factor, 0.0f, 1.0f);
                    const float cutoff = std::clamp(descriptor.cutoff, 0.0f, 1.0f);
                    if (descriptor.alpha_mode == AlphaMode::Opaque) {
                        for (std::size_t offset = 3u; offset < result->image.rgba.size(); offset += 4u)
                            result->image.rgba[offset] = 255u;
                        result->image.meaningful_alpha = false;
                    } else if (descriptor.alpha_mode == AlphaMode::Mask) {
                        for (std::size_t offset = 3u; offset < result->image.rgba.size(); offset += 4u) {
                            const float alpha =
                                (static_cast<float>(result->image.rgba[offset]) / 255.0f) * factor;
                            result->image.rgba[offset] = alpha >= cutoff ? 255u : 0u;
                        }
                        result->image.meaningful_alpha = true;
                    }
                    result->ok = validImage(result->image);
                    break;
                }
                case TextureSourceKind::Opacity:
                    if (!source || !secondary) {
                        result->error = "invalid opacity source";
                        break;
                    }
                    result->image = source->image;
                    result->ok = applyOpacity(&result->image, secondary->image, &result->error);
                    break;
            }
        },
        [handle, generation, result] {
            complete(handle, generation, result);
        }
    );

    if (!queued) return false;
    ++state.in_flight;
    record.state = TextureState::Queued;
    return true;
}

TextureHandle registerDescriptor(TextureSourceDescriptor descriptor)
{
    if (descriptor.key.empty()) return INVALID_TEXTURE;
    const TextureHandle existing = findTexture(descriptor.key);
    const TextureHandle handle = reserveTexture(descriptor.key);
    if (handle == INVALID_TEXTURE) return INVALID_TEXTURE;

    Runtime& state = runtime();
    if (state.records.find(handle) != state.records.end()) return handle;

    Record record;
    record.descriptor = std::move(descriptor);
    record.generation = state.generation;
    record.state = existing != INVALID_TEXTURE && textureStorageReady(handle)
        ? TextureState::Ready
        : TextureState::Registered;
    state.records.emplace(handle, std::move(record));
    schedule(handle);
    return handle;
}

std::string pathFor(TextureHandle handle)
{
    const TextureAsset *asset = texture(handle);
    return asset ? asset->path : std::string{};
}

bool parseFloat(std::string_view text, float *value)
{
    if (!value || text.empty()) return false;
    std::string copy(text);
    char *end = nullptr;
    const float parsed = std::strtof(copy.c_str(), &end);
    if (!end || end == copy.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

} // namespace

TextureImportScope::TextureImportScope()
{
    ++import_depth;
}

TextureImportScope::~TextureImportScope()
{
    if (import_depth != 0u) --import_depth;
}

bool textureImportActive()
{
    return import_depth != 0u;
}

TextureHandle registerDeferredFile(const std::string& path)
{
    if (path.empty()) return INVALID_TEXTURE;
    TextureSourceDescriptor descriptor;
    descriptor.kind = TextureSourceKind::File;
    descriptor.key = normalizeTexturePath(path);
    return registerDescriptor(std::move(descriptor));
}

TextureHandle registerDeferredMemory(const std::string& key, std::vector<std::uint8_t> bytes)
{
    if (key.empty() || bytes.empty()) return INVALID_TEXTURE;
    TextureSourceDescriptor descriptor;
    descriptor.kind = TextureSourceKind::Memory;
    descriptor.key = key;
    descriptor.bytes = std::move(bytes);
    return registerDescriptor(std::move(descriptor));
}

TextureHandle registerDeferredChannel(TextureHandle source, int channel, const std::string& label)
{
    const std::string source_path = pathFor(source);
    if (source_path.empty() || channel < 0 || channel > 3) return INVALID_TEXTURE;
    TextureSourceDescriptor descriptor;
    descriptor.kind = TextureSourceKind::Channel;
    descriptor.key = source_path + "\n@gltf-channel:" + label;
    descriptor.source = source;
    descriptor.channel = channel;
    return registerDescriptor(std::move(descriptor));
}

TextureHandle registerDeferredAlpha(TextureHandle source, AlphaMode mode, float factor, float cutoff)
{
    if (mode == AlphaMode::Blend) return source;
    const std::string source_path = pathFor(source);
    if (source_path.empty()) return INVALID_TEXTURE;

    const float safe_factor = std::clamp(factor, 0.0f, 1.0f);
    const float safe_cutoff = std::clamp(cutoff, 0.0f, 1.0f);
    const std::uint32_t factor_key = static_cast<std::uint32_t>(safe_factor * 65535.0f + 0.5f);
    const std::uint32_t cutoff_key = static_cast<std::uint32_t>(safe_cutoff * 65535.0f + 0.5f);

    TextureSourceDescriptor descriptor;
    descriptor.kind = TextureSourceKind::Alpha;
    descriptor.key = source_path + "\n@gltf-alpha:" +
        (mode == AlphaMode::Opaque
            ? std::string("opaque")
            : "mask:" + std::to_string(factor_key) + ":" + std::to_string(cutoff_key));
    descriptor.source = source;
    descriptor.alpha_mode = mode;
    descriptor.factor = safe_factor;
    descriptor.cutoff = safe_cutoff;
    return registerDescriptor(std::move(descriptor));
}

TextureHandle registerDeferredOpacity(TextureHandle color, TextureHandle opacity)
{
    const std::string color_path = pathFor(color);
    const std::string opacity_path = pathFor(opacity);
    if (color_path.empty() || opacity_path.empty()) return INVALID_TEXTURE;
    TextureSourceDescriptor descriptor;
    descriptor.kind = TextureSourceKind::Opacity;
    descriptor.key = color_path + "\n@opacity:" + opacity_path;
    descriptor.source = color;
    descriptor.secondary = opacity;
    return registerDescriptor(std::move(descriptor));
}

TextureHandle registerDeferredDescriptor(TextureSourceDescriptor descriptor)
{
    return registerDescriptor(std::move(descriptor));
}

TextureHandle registerDeferredDerivedKey(const std::string& key)
{
    static constexpr std::string_view ChannelMarker = "\n@gltf-channel:";
    static constexpr std::string_view AlphaMarker = "\n@gltf-alpha:";

    if (const std::size_t marker = key.rfind(ChannelMarker); marker != std::string::npos) {
        const TextureHandle source = findTexture(key.substr(0u, marker));
        if (source == INVALID_TEXTURE) return INVALID_TEXTURE;
        const std::string label = key.substr(marker + ChannelMarker.size());
        int channel = -1;
        if (label == "roughness") channel = 1;
        else if (label == "metallic") channel = 2;
        if (channel < 0) return INVALID_TEXTURE;

        TextureSourceDescriptor descriptor;
        descriptor.kind = TextureSourceKind::Channel;
        descriptor.key = key;
        descriptor.source = source;
        descriptor.channel = channel;
        return registerDescriptor(std::move(descriptor));
    }

    if (const std::size_t marker = key.rfind(AlphaMarker); marker != std::string::npos) {
        const TextureHandle source = findTexture(key.substr(0u, marker));
        if (source == INVALID_TEXTURE) return INVALID_TEXTURE;
        const std::string suffix = key.substr(marker + AlphaMarker.size());

        TextureSourceDescriptor descriptor;
        descriptor.kind = TextureSourceKind::Alpha;
        descriptor.key = key;
        descriptor.source = source;
        descriptor.factor = 1.0f;
        descriptor.cutoff = 0.0f;

        if (suffix == "opaque") {
            descriptor.alpha_mode = AlphaMode::Opaque;
            return registerDescriptor(std::move(descriptor));
        }
        if (!suffix.starts_with("mask:")) return INVALID_TEXTURE;

        descriptor.alpha_mode = AlphaMode::Mask;
        const std::string_view values(suffix.data() + 5u, suffix.size() - 5u);
        const std::size_t separator = values.find(':');
        if (separator == std::string_view::npos) {
            if (!parseFloat(values, &descriptor.cutoff)) return INVALID_TEXTURE;
        } else {
            float factor_key = 0.0f;
            float cutoff_key = 0.0f;
            if (!parseFloat(values.substr(0u, separator), &factor_key) ||
                !parseFloat(values.substr(separator + 1u), &cutoff_key))
                return INVALID_TEXTURE;
            descriptor.factor = std::clamp(factor_key / 65535.0f, 0.0f, 1.0f);
            descriptor.cutoff = std::clamp(cutoff_key / 65535.0f, 0.0f, 1.0f);
        }
        return registerDescriptor(std::move(descriptor));
    }

    return INVALID_TEXTURE;
}

TextureState textureState(TextureHandle handle)
{
    return stateFor(handle);
}

bool textureDescriptor(TextureHandle handle, TextureSourceDescriptor *descriptor)
{
    if (!descriptor) return false;
    const auto found = runtime().records.find(handle);
    if (found == runtime().records.end()) return false;
    *descriptor = found->second.descriptor;
    return true;
}

std::size_t pumpTextureResources()
{
    const std::size_t completed = Core::Jobs::pump(textureJobGroup());
    for (auto& [handle, record] : runtime().records) {
        (void)record;
        schedule(handle);
    }
    return completed;
}

void clearTextureStreaming()
{
    Runtime& state = runtime();
    ++state.generation;
    if (state.generation == 0u) state.generation = 1u;
    Core::Jobs::cancelPending(textureJobGroup());
    Core::Jobs::wait(textureJobGroup());
    Core::Jobs::pump(textureJobGroup());
    state.records.clear();
    state.in_flight = 0u;
}

} // namespace Models::Internal