#include "Renderer/GlobalIllumination/PhotonMapping/PhotonMap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Renderer::GlobalIllumination::PhotonMapping {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Cell {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const Cell& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct CellHash {
    std::size_t operator()(const Cell& cell) const
    {
        const std::uint64_t x = static_cast<std::uint32_t>(cell.x);
        const std::uint64_t y = static_cast<std::uint32_t>(cell.y);
        const std::uint64_t z = static_cast<std::uint32_t>(cell.z);
        std::uint64_t value = x * 0x9E3779B185EBCA87ull;
        value ^= y * 0xC2B2AE3D27D4EB4Full;
        value ^= z * 0x165667B19E3779F9ull;
        value ^= value >> 32u;
        return static_cast<std::size_t>(value);
    }
};

struct DirectionalEmissionDomain {
    Vec3 center{};
    Vec3 direction {0.0f, -1.0f, 0.0f};
    Vec3 tangent {1.0f, 0.0f, 0.0f};
    Vec3 bitangent {0.0f, 0.0f, 1.0f};
    float along = 0.0f;
    float across = 0.0f;
    float vertical = 0.0f;
    float area = 0.0f;
};

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(Vec3 a, Vec3 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float lengthSquared(Vec3 value)
{
    return dot(value, value);
}

Vec3 normalize(Vec3 value)
{
    const float squared = lengthSquared(value);
    if (squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    return multiply(value, 1.0f / std::sqrt(squared));
}

float maximumComponent(Vec3 value)
{
    return std::max({value.x, value.y, value.z});
}

float random01(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8u) * (1.0f / 16777216.0f);
}

Vec3 cosineHemisphere(Vec3 normal, std::uint32_t& seed)
{
    normal = normalize(normal);
    const float u1 = random01(seed);
    const float u2 = random01(seed);
    const float radius = std::sqrt(u1);
    const float angle = 2.0f * kPi * u2;
    const float x = radius * std::cos(angle);
    const float y = radius * std::sin(angle);
    const float z = std::sqrt(std::max(1.0f - u1, 0.0f));

    const Vec3 helper = std::abs(normal.y) < 0.999f
        ? Vec3{0.0f, 1.0f, 0.0f}
        : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 tangent = normalize(cross(helper, normal));
    const Vec3 bitangent = cross(normal, tangent);
    return normalize(add(add(multiply(tangent, x), multiply(bitangent, y)), multiply(normal, z)));
}

Vec3 fibonacciDirection(std::uint32_t index, std::uint32_t count)
{
    constexpr float golden_angle = 2.39996322972865332f;
    const float safe_count = static_cast<float>(std::max(count, 1u));
    const float t = (static_cast<float>(index) + 0.5f) / safe_count;
    const float z = 1.0f - 2.0f * t;
    const float radius = std::sqrt(std::max(1.0f - z * z, 0.0f));
    const float angle = golden_angle * static_cast<float>(index);
    return {radius * std::cos(angle), radius * std::sin(angle), z};
}

float radicalInverse(std::uint32_t value, std::uint32_t base)
{
    const float inverse_base = 1.0f / static_cast<float>(base);
    float factor = inverse_base;
    float result = 0.0f;
    while (value > 0u) {
        result += static_cast<float>(value % base) * factor;
        value /= base;
        factor *= inverse_base;
    }
    return result;
}

Cell cellFor(Vec3 position, float size)
{
    const float inverse = 1.0f / std::max(size, 1.0e-6f);
    return {
        static_cast<int>(std::floor(position.x * inverse)),
        static_cast<int>(std::floor(position.y * inverse)),
        static_cast<int>(std::floor(position.z * inverse)),
    };
}

float autoRadius(const TraceBounds& bounds, std::uint32_t photon_count)
{
    const Vec3 extent = subtract(bounds.maximum, bounds.minimum);
    const float maximum_extent = std::max(maximumComponent(extent), 1.0e-3f);
    const float cells_per_axis = std::max(
        std::cbrt(static_cast<float>(std::max(photon_count, 1u))),
        1.0f
    );
    return std::clamp(
        maximum_extent * (1.5f / cells_per_axis),
        maximum_extent * 0.005f,
        maximum_extent * 0.25f
    );
}

DirectionalEmissionDomain directionalEmissionDomain(
    const TraceBounds& bounds,
    Vec3 light_direction)
{
    DirectionalEmissionDomain domain;
    domain.direction = normalize(light_direction);
    domain.center = multiply(add(bounds.minimum, bounds.maximum), 0.5f);
    const Vec3 half = multiply(subtract(bounds.maximum, bounds.minimum), 0.5f);
    const Vec3 helper = std::abs(domain.direction.y) < 0.999f
        ? Vec3{0.0f, 1.0f, 0.0f}
        : Vec3{1.0f, 0.0f, 0.0f};
    domain.tangent = normalize(cross(helper, domain.direction));
    domain.bitangent = cross(domain.direction, domain.tangent);

    const auto support = [half](Vec3 axis) {
        return std::abs(axis.x) * half.x +
            std::abs(axis.y) * half.y +
            std::abs(axis.z) * half.z;
    };
    domain.along = support(domain.direction);
    domain.across = support(domain.tangent);
    domain.vertical = support(domain.bitangent);
    domain.area = std::max(4.0f * domain.across * domain.vertical, 1.0e-6f);
    return domain;
}

void directionalEmission(
    const DirectionalEmissionDomain& domain,
    std::uint32_t index,
    float ray_epsilon,
    Vec3& origin,
    Vec3& direction)
{
    direction = domain.direction;
    const float u = radicalInverse(index + 1u, 2u) * 2.0f - 1.0f;
    const float v = radicalInverse(index + 1u, 3u) * 2.0f - 1.0f;
    origin = add(
        add(
            subtract(
                domain.center,
                multiply(domain.direction, domain.along + ray_epsilon * 8.0f)
            ),
            multiply(domain.tangent, u * domain.across)
        ),
        multiply(domain.bitangent, v * domain.vertical)
    );
}

} // namespace

struct PhotonMap::Storage {
    std::vector<Photon> photons;
    std::unordered_map<Cell, std::vector<std::uint32_t>, CellHash> cells;
    float radius = 0.0f;
};

PhotonMap::PhotonMap() : storage_(std::make_unique<Storage>()) {}
PhotonMap::~PhotonMap() = default;
PhotonMap::PhotonMap(PhotonMap&&) noexcept = default;
PhotonMap& PhotonMap::operator=(PhotonMap&&) noexcept = default;

void PhotonMap::rebuild(
    const TraceScene& scene,
    const Scenes::LightState& light,
    const Settings& settings)
{
    clear();
    if (!storage_ || !settings.enabled || !light.valid || light.intensity <= 0.0f ||
        scene.empty() || settings.photon_count == 0u || settings.bounces == 0u)
    {
        return;
    }

    const TraceBounds scene_bounds = scene.bounds();
    if (!scene_bounds.valid) return;

    storage_->radius = settings.radius > 0.0f
        ? settings.radius
        : autoRadius(scene_bounds, settings.photon_count);
    if (storage_->radius <= 0.0f) return;

    const std::uint8_t maximum_bounces = std::clamp<std::uint8_t>(settings.bounces, 1u, 8u);
    storage_->photons.reserve(static_cast<std::size_t>(settings.photon_count) * maximum_bounces);

    const bool directional = light.type == LightType::Directional;
    const DirectionalEmissionDomain domain = directional
        ? directionalEmissionDomain(scene_bounds, light.direction)
        : DirectionalEmissionDomain{};
    const float emitted_power = std::max(light.intensity, 0.0f) *
        (directional ? domain.area : 4.0f * kPi);
    const Vec3 initial_power = multiply(
        light.color,
        emitted_power / static_cast<float>(settings.photon_count)
    );

    for (std::uint32_t photon_index = 0u; photon_index < settings.photon_count; ++photon_index) {
        Vec3 origin{};
        Vec3 direction{};
        if (directional) {
            directionalEmission(domain, photon_index, settings.ray_epsilon, origin, direction);
        } else {
            origin = light.position;
            direction = fibonacciDirection(photon_index, settings.photon_count);
        }

        Vec3 power = initial_power;
        std::uint32_t seed = photon_index * 747796405u + 2891336453u;
        for (std::uint8_t bounce = 0u; bounce < maximum_bounces; ++bounce) {
            const TraceHit hit = scene.traceClosest(
                origin,
                direction,
                std::numeric_limits<float>::infinity(),
                settings.ray_epsilon
            );
            if (!hit.found) break;

            if (bounce > 0u) {
                storage_->photons.push_back(Photon{
                    .position = hit.position,
                    .normal = hit.normal,
                    .direction = direction,
                    .power = power,
                });
            }

            power = multiply(power, scene.albedo(hit));
            if (maximumComponent(power) <= 1.0e-7f) break;

            direction = cosineHemisphere(hit.normal, seed);
            origin = add(hit.position, multiply(hit.normal, settings.ray_epsilon * 4.0f));
        }
    }

    storage_->cells.reserve(storage_->photons.size());
    for (std::uint32_t index = 0u; index < storage_->photons.size(); ++index) {
        storage_->cells[cellFor(storage_->photons[index].position, storage_->radius)].push_back(index);
    }
}

Vec3 PhotonMap::sample(Vec3 position, Vec3 normal) const
{
    if (!valid()) return {};
    normal = normalize(normal);
    const float radius_squared = storage_->radius * storage_->radius;
    const Cell center = cellFor(position, storage_->radius);
    Vec3 sum{};

    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const Cell cell{center.x + x, center.y + y, center.z + z};
                const auto found = storage_->cells.find(cell);
                if (found == storage_->cells.end()) continue;

                for (const std::uint32_t index : found->second) {
                    if (index >= storage_->photons.size()) continue;
                    const Photon& photon = storage_->photons[index];
                    if (dot(normal, photon.normal) <= 0.0f) continue;
                    const float distance_squared = lengthSquared(subtract(photon.position, position));
                    if (distance_squared >= radius_squared) continue;
                    const float weight = 1.0f - distance_squared / radius_squared;
                    sum = add(sum, multiply(photon.power, weight));
                }
            }
        }
    }

    return multiply(sum, 1.0f / (kPi * radius_squared));
}

void PhotonMap::clear()
{
    if (!storage_) storage_ = std::make_unique<Storage>();
    storage_->photons.clear();
    storage_->cells.clear();
    storage_->radius = 0.0f;
}

bool PhotonMap::valid() const
{
    return storage_ && storage_->radius > 0.0f && !storage_->photons.empty();
}

std::size_t PhotonMap::photonCount() const
{
    return storage_ ? storage_->photons.size() : 0u;
}

const Photon *PhotonMap::photonData() const
{
    return storage_ && !storage_->photons.empty() ? storage_->photons.data() : nullptr;
}

float PhotonMap::radius() const
{
    return storage_ ? storage_->radius : 0.0f;
}

} // namespace Renderer::GlobalIllumination::PhotonMapping
