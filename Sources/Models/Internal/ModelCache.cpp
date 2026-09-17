#include "Models/Internal/ModelCache.hpp"

#include "Core/Jobs/Jobs.hpp"
#include "Models/Internal/ModelCacheBinary.hpp"
#include "Models/Internal/ModelCacheCodec.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace Models::Internal::ModelCache {
namespace {

using ModelCacheCodec::DependencyStamp;

constexpr std::array<std::uint8_t, 8> Magic {{'H','O','R','S','E','M','D','L'}};
constexpr std::uint32_t SchemaVersion = 1u;
constexpr std::uint32_t ImporterVersion = 1u;
constexpr std::uint64_t MaximumCacheBytes = 4ull * 1024ull * 1024ull * 1024ull;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::string normalizedPath(const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? std::filesystem::path(path) : absolute).lexically_normal().string();
}

std::uint64_t fnv1a(const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0u; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t checksum(const std::vector<std::uint8_t>& bytes)
{
    return fnv1a(bytes.data(), bytes.size());
}

std::filesystem::path cacheRoot()
{
    if (const char *override_path = std::getenv("HORSE_MODEL_CACHE_DIR");
        override_path && *override_path)
        return std::filesystem::path(override_path);

#ifdef _WIN32
    if (const char *local = std::getenv("LOCALAPPDATA"); local && *local)
        return std::filesystem::path(local) / "Horse" / "Models";
    if (const char *home = std::getenv("USERPROFILE"); home && *home)
        return std::filesystem::path(home) / "AppData" / "Local" / "Horse" / "Models";
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / "Library" / "Caches" / "Horse" / "Models";
#else
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "Horse" / "Models";
    if (const char *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".cache" / "Horse" / "Models";
#endif

    std::error_code ec;
    const std::filesystem::path temporary = std::filesystem::temp_directory_path(ec);
    return (ec ? std::filesystem::path(".") : temporary) / "Horse" / "Models";
}

DependencyStamp stamp(const std::string& path)
{
    DependencyStamp result;
    result.path = normalizedPath(path);

    std::error_code ec;
    result.exists = std::filesystem::exists(result.path, ec) && !ec;
    if (!result.exists) return result;

    ec.clear();
    if (std::filesystem::is_regular_file(result.path, ec) && !ec) {
        ec.clear();
        result.size = std::filesystem::file_size(result.path, ec);
        if (ec) result.size = 0u;
    }

    ec.clear();
    const auto modified = std::filesystem::last_write_time(result.path, ec);
    if (!ec) {
        result.modified = std::chrono::duration_cast<std::chrono::nanoseconds>(
            modified.time_since_epoch()).count();
    }
    return result;
}

bool sameStamp(const DependencyStamp& expected)
{
    const DependencyStamp current = stamp(expected.path);
    return current.exists == expected.exists &&
        (!expected.exists ||
            (current.size == expected.size && current.modified == expected.modified));
}

void appendUnique(std::vector<std::string> *paths, std::string path)
{
    if (!paths || path.empty()) return;
    path = normalizedPath(path);
    if (std::find(paths->begin(), paths->end(), path) == paths->end())
        paths->push_back(std::move(path));
}

bool collectTexturePath(
    TextureHandle handle,
    std::unordered_set<TextureHandle> *seen,
    std::vector<std::string> *paths,
    std::string *error)
{
    if (handle == INVALID_TEXTURE) return true;
    if (!seen || !paths) return false;
    if (!seen->insert(handle).second) return true;

    TextureSourceDescriptor descriptor;
    if (!textureDescriptor(handle, &descriptor))
        return fail(error, "model cache texture has no deferred source descriptor");

    if (!collectTexturePath(descriptor.source, seen, paths, error) ||
        !collectTexturePath(descriptor.secondary, seen, paths, error))
        return false;
    if (descriptor.kind == TextureSourceKind::File) appendUnique(paths, descriptor.key);
    return true;
}

bool collectMaterialPaths(
    const MaterialData& material,
    std::unordered_set<TextureHandle> *seen,
    std::vector<std::string> *paths,
    std::string *error)
{
    const std::array<TextureHandle, 7> legacy {{
        material.diffuse_texture,
        material.normal_texture,
        material.roughness_texture,
        material.metallic_texture,
        material.ambient_occlusion_texture,
        material.emissive_texture,
        material.opacity_texture,
    }};
    for (TextureHandle handle : legacy)
        if (!collectTexturePath(handle, seen, paths, error)) return false;

    const std::array<const TextureInfo *, 19> infos {{
        &material.base_color_info, &material.metallic_roughness_info, &material.normal_info,
        &material.occlusion_info, &material.emissive_info, &material.clearcoat_info,
        &material.clearcoat_roughness_info, &material.clearcoat_normal_info,
        &material.sheen_color_info, &material.sheen_roughness_info, &material.transmission_info,
        &material.thickness_info, &material.specular_info, &material.specular_color_info,
        &material.iridescence_info, &material.iridescence_thickness_info,
        &material.anisotropy_info, &material.diffuse_transmission_info,
        &material.diffuse_transmission_color_info,
    }};
    for (const TextureInfo *info : infos)
        if (!collectTexturePath(info->texture, seen, paths, error)) return false;
    return true;
}

bool dependencyStamps(
    const std::string& source,
    const Formats::Document& document,
    std::vector<DependencyStamp> *dependencies,
    std::string *error)
{
    if (!dependencies) return false;
    std::vector<std::string> paths;
    appendUnique(&paths, source);
    for (const std::string& dependency : document.dependencies) appendUnique(&paths, dependency);

    std::unordered_set<TextureHandle> seen;
    for (const Formats::Part& part : document.parts)
        if (!collectMaterialPaths(part.material, &seen, &paths, error)) return false;
    for (const Formats::VariantMaterial& variant : document.variant_materials)
        if (!collectMaterialPaths(variant.material, &seen, &paths, error)) return false;

    dependencies->clear();
    dependencies->reserve(paths.size());
    for (const std::string& path : paths) dependencies->push_back(stamp(path));
    return true;
}

bool readManifest(
    const std::vector<std::uint8_t>& body,
    std::vector<DependencyStamp> *dependencies)
{
    if (!dependencies) return false;
    ModelCacheBinary::Reader reader(body);
    std::size_t count = 0u;
    if (!reader.count(&count)) return false;
    dependencies->clear();
    dependencies->resize(count);
    for (DependencyStamp& dependency : *dependencies) {
        if (!reader.string(&dependency.path) || !reader.u64(&dependency.size) ||
            !reader.i64(&dependency.modified) || !reader.boolean(&dependency.exists))
            return false;
    }
    return true;
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t> *bytes)
{
    if (!bytes) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > MaximumCacheBytes ||
        static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max())
        return false;
    file.seekg(0, std::ios::beg);
    bytes->resize(static_cast<std::size_t>(length));
    if (!bytes->empty())
        file.read(reinterpret_cast<char *>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    return file.good() || file.eof();
}

bool decodeContainer(
    const std::vector<std::uint8_t>& file,
    std::vector<std::uint8_t> *body,
    std::string *error)
{
    if (!body) return false;
    ModelCacheBinary::Reader reader(file);
    std::array<std::uint8_t, Magic.size()> magic{};
    std::uint32_t schema = 0u;
    std::uint32_t importer = 0u;
    std::uint64_t body_size = 0u;
    std::uint64_t expected_checksum = 0u;
    if (!reader.raw(magic.data(), magic.size()) || magic != Magic ||
        !reader.u32(&schema) || !reader.u32(&importer) ||
        !reader.u64(&body_size) || !reader.u64(&expected_checksum))
        return fail(error, "invalid model cache header");
    if (schema != SchemaVersion || importer != ImporterVersion)
        return fail(error, "model cache schema is stale");
    if (body_size > MaximumCacheBytes || body_size != reader.remaining())
        return fail(error, "invalid model cache payload length");

    body->resize(static_cast<std::size_t>(body_size));
    if (!reader.raw(body->data(), body->size()) || !reader.finished())
        return fail(error, "truncated model cache payload");
    if (checksum(*body) != expected_checksum)
        return fail(error, "model cache checksum mismatch");
    return true;
}

std::vector<std::uint8_t> encodeContainer(const std::vector<std::uint8_t>& body)
{
    ModelCacheBinary::Writer writer;
    writer.raw(Magic.data(), Magic.size());
    writer.u32(SchemaVersion);
    writer.u32(ImporterVersion);
    writer.u64(static_cast<std::uint64_t>(body.size()));
    writer.u64(checksum(body));
    writer.raw(body.data(), body.size());
    return writer.take();
}

void writeFile(std::filesystem::path target, std::vector<std::uint8_t> bytes)
{
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return;

    static std::atomic<std::uint64_t> sequence {0u};
    const std::uint64_t id = sequence.fetch_add(1u, std::memory_order_relaxed);
    std::filesystem::path temporary = target;
    temporary += ".tmp." + std::to_string(id);

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return;
        if (!bytes.empty())
            file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file.good()) {
            file.close();
            std::filesystem::remove(temporary, ec);
            return;
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(target, ignored);
        ec.clear();
        std::filesystem::rename(temporary, target, ec);
    }
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
}

} // namespace

std::filesystem::path pathFor(const std::string& source)
{
    const std::string key = normalizedPath(source);
    const std::uint64_t hash = fnv1a(key.data(), key.size());
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << hash << ".horsemodel";
    return cacheRoot() / name.str();
}

bool encode(
    const std::string& source,
    const Formats::Document& document,
    Payload *payload,
    std::string *error)
{
    if (error) error->clear();
    if (!payload) return fail(error, "null model cache payload output");

    std::vector<DependencyStamp> dependencies;
    if (!dependencyStamps(source, document, &dependencies, error)) return false;

    std::vector<std::uint8_t> body;
    std::vector<std::string> texture_dependencies;
    if (!ModelCacheCodec::encode(document, dependencies, &body, &texture_dependencies, error)) return false;

    payload->bytes = encodeContainer(body);
    if (payload->bytes.empty()) return fail(error, "failed to encode model cache container");
    return true;
}

void writeAsync(const std::string& source, Payload payload)
{
    if (payload.bytes.empty()) return;
    const std::filesystem::path target = pathFor(source);
    Core::Jobs::trySubmit(
        [target, bytes = std::move(payload.bytes)]() mutable {
            writeFile(target, std::move(bytes));
        }
    );
}

bool load(const std::string& source, Formats::Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) return fail(error, "null model cache document output");

    std::vector<std::uint8_t> file;
    if (!readFile(pathFor(source), &file)) return false;

    std::vector<std::uint8_t> body;
    std::string local_error;
    if (!decodeContainer(file, &body, &local_error)) {
        if (error) *error = std::move(local_error);
        return false;
    }

    std::vector<DependencyStamp> manifest;
    if (!readManifest(body, &manifest)) {
        if (error) *error = "invalid model cache dependency manifest";
        return false;
    }
    if (manifest.empty() || normalizedPath(manifest.front().path) != normalizedPath(source)) {
        if (error) *error = "model cache source identity mismatch";
        return false;
    }
    for (const DependencyStamp& dependency : manifest) {
        if (!sameStamp(dependency)) return false;
    }

    std::vector<DependencyStamp> decoded_manifest;
    Formats::Document decoded;
    if (!ModelCacheCodec::decode(body, &decoded_manifest, &decoded, &local_error)) {
        if (error) *error = std::move(local_error);
        return false;
    }

    *document = std::move(decoded);
    return true;
}

} // namespace Models::Internal::ModelCache
