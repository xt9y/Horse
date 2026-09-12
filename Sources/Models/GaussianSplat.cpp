#include "Models/GaussianSplat.hpp"

#include "Models/Formats/GltfJson.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace Models::GaussianSplat {
namespace {

using Formats::GltfJson::Type;
using Formats::GltfJson::Value;

constexpr std::string_view ExtensionName = "KHR_gaussian_splatting";

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

const AttributeData *attribute(const MeshData& mesh, std::string_view semantic)
{
    const auto found = mesh.attributes.find(std::string(semantic));
    return found == mesh.attributes.end() ? nullptr : &found->second;
}

bool validateAttribute(
    const AttributeData *source,
    std::size_t splats,
    std::uint32_t components,
    std::string_view semantic,
    std::string *error)
{
    if (!source)
        return fail(error, "Gaussian splat primitive is missing " + std::string(semantic));
    if (source->components != components)
        return fail(error, "Gaussian splat attribute has invalid component count: " + std::string(semantic));
    if (splats > 0u && source->values.size() != splats * static_cast<std::size_t>(components))
        return fail(error, "Gaussian splat attribute count mismatch: " + std::string(semantic));
    return true;
}

float component(const AttributeData& source, std::size_t splat, std::size_t index)
{
    return static_cast<float>(source.values[splat * source.components + index]);
}

Quat normalized(Quat value)
{
    const float length2 = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (length2 <= 1.0e-20f) return {};
    const float inverse = 1.0f / std::sqrt(length2);
    value.x *= inverse;
    value.y *= inverse;
    value.z *= inverse;
    value.w *= inverse;
    return value;
}

bool parseOptions(const MeshData& mesh, Data *output, std::string *error)
{
    const auto extension = mesh.extensions_json.find(std::string(ExtensionName));
    if (extension == mesh.extensions_json.end()) return false;

    Value object;
    if (!Formats::GltfJson::parse(extension->second, &object, error) || !object.is(Type::Object))
        return fail(error, "KHR_gaussian_splatting must be an object");

    const std::string kernel = Formats::GltfJson::stringValue(object.get("kernel"));
    if (kernel != "ellipse")
        return fail(error, "unsupported KHR_gaussian_splatting kernel: " + kernel);

    const std::string color_space = Formats::GltfJson::stringValue(object.get("colorSpace"));
    if (color_space == "srgb_rec709_display") {
        output->color_space = ColorSpace::SrgbRec709Display;
    } else if (color_space == "lin_rec709_display") {
        output->color_space = ColorSpace::LinearRec709Display;
    } else {
        return fail(error, "unsupported KHR_gaussian_splatting colorSpace: " + color_space);
    }

    const std::string projection = Formats::GltfJson::stringValue(object.get("projection"), "perspective");
    if (projection != "perspective")
        return fail(error, "unsupported KHR_gaussian_splatting projection: " + projection);
    output->projection = Projection::Perspective;

    const std::string sorting = Formats::GltfJson::stringValue(object.get("sortingMethod"), "cameraDistance");
    if (sorting != "cameraDistance")
        return fail(error, "unsupported KHR_gaussian_splatting sorting method: " + sorting);
    output->sorting = SortingMethod::CameraDistance;
    return true;
}

const AttributeData *harmonicAttribute(
    const MeshData& mesh,
    std::uint32_t degree,
    std::uint32_t coefficient)
{
    return attribute(
        mesh,
        "KHR_gaussian_splatting:SH_DEGREE_" + std::to_string(degree) +
            "_COEF_" + std::to_string(coefficient)
    );
}

bool collectHarmonics(
    const MeshData& mesh,
    std::size_t splat_count,
    std::vector<const AttributeData *> *attributes,
    std::uint32_t *degree,
    std::string *error)
{
    if (!attributes || !degree) return false;
    attributes->clear();

    const AttributeData *base = harmonicAttribute(mesh, 0u, 0u);
    if (!validateAttribute(
            base,
            splat_count,
            3u,
            "KHR_gaussian_splatting:SH_DEGREE_0_COEF_0",
            error))
        return false;
    attributes->push_back(base);
    *degree = 0u;

    for (std::uint32_t current_degree = 1u; current_degree <= 3u; ++current_degree) {
        const std::uint32_t coefficient_count = current_degree * 2u + 1u;
        bool any = false;
        bool all = true;
        std::array<const AttributeData *, 7> current{};
        for (std::uint32_t coefficient = 0u; coefficient < coefficient_count; ++coefficient) {
            current[coefficient] = harmonicAttribute(mesh, current_degree, coefficient);
            any = any || current[coefficient] != nullptr;
            all = all && current[coefficient] != nullptr;
        }
        if (!any) break;
        if (!all)
            return fail(error, "Gaussian splat spherical-harmonic degree is incomplete");
        for (std::uint32_t coefficient = 0u; coefficient < coefficient_count; ++coefficient) {
            const std::string semantic = "KHR_gaussian_splatting:SH_DEGREE_" +
                std::to_string(current_degree) + "_COEF_" + std::to_string(coefficient);
            if (!validateAttribute(current[coefficient], splat_count, 3u, semantic, error)) return false;
            attributes->push_back(current[coefficient]);
        }
        *degree = current_degree;
    }
    return true;
}

} // namespace

bool isGaussianSplat(const MeshData& mesh)
{
    return mesh.extensions_json.contains(std::string(ExtensionName));
}

bool decode(const MeshData& mesh, Data *output, std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error, "null Gaussian splat output");
    *output = {};
    if (!isGaussianSplat(mesh)) return fail(error, "mesh does not use KHR_gaussian_splatting");
    if (mesh.primitive_mode != PrimitiveMode::Points)
        return fail(error, "KHR_gaussian_splatting primitive mode must be POINTS");
    if (!parseOptions(mesh, output, error)) return false;

    const std::size_t splat_count = mesh.vertices.size();
    const AttributeData *rotation = attribute(mesh, "KHR_gaussian_splatting:ROTATION");
    const AttributeData *scale = attribute(mesh, "KHR_gaussian_splatting:SCALE");
    const AttributeData *opacity = attribute(mesh, "KHR_gaussian_splatting:OPACITY");
    if (!validateAttribute(rotation, splat_count, 4u, "KHR_gaussian_splatting:ROTATION", error) ||
        !validateAttribute(scale, splat_count, 3u, "KHR_gaussian_splatting:SCALE", error) ||
        !validateAttribute(opacity, splat_count, 1u, "KHR_gaussian_splatting:OPACITY", error))
        return false;

    std::vector<const AttributeData *> harmonics;
    if (!collectHarmonics(mesh, splat_count, &harmonics, &output->spherical_harmonic_degree, error))
        return false;

    output->splats.resize(splat_count);
    for (std::size_t index = 0u; index < splat_count; ++index) {
        Splat& splat = output->splats[index];
        splat.position = mesh.vertices[index].position;
        splat.rotation = normalized({
            component(*rotation, index, 0u),
            component(*rotation, index, 1u),
            component(*rotation, index, 2u),
            component(*rotation, index, 3u),
        });
        splat.scale = {
            std::max(component(*scale, index, 0u), 0.0f),
            std::max(component(*scale, index, 1u), 0.0f),
            std::max(component(*scale, index, 2u), 0.0f),
        };
        splat.opacity = std::clamp(component(*opacity, index, 0u), 0.0f, 1.0f);
        splat.spherical_harmonics.reserve(harmonics.size());
        for (const AttributeData *coefficient : harmonics) {
            splat.spherical_harmonics.push_back({
                component(*coefficient, index, 0u),
                component(*coefficient, index, 1u),
                component(*coefficient, index, 2u),
            });
        }
    }
    return true;
}

} // namespace Models::GaussianSplat
