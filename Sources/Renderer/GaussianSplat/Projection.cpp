#include "Renderer/GaussianSplat/Projection.hpp"

#include "Camera.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::GaussianSplat {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float MinimumVariancePixels = 0.25f;

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 subtractScaled(Vec3 value, Vec3 basis, float scale)
{
    return {value.x - basis.x * scale, value.y - basis.y * scale, value.z - basis.z * scale};
}

float lengthSquared(Vec3 value)
{
    return Math::dot(value, value);
}

Vec3 localAxis(const Models::GaussianSplat::Splat& splat, int axis)
{
    const float x = splat.rotation.x;
    const float y = splat.rotation.y;
    const float z = splat.rotation.z;
    const float w = splat.rotation.w;
    if (axis == 0) {
        return {
            splat.scale.x * (1.0f - 2.0f * (y * y + z * z)),
            splat.scale.x * (2.0f * (x * y + w * z)),
            splat.scale.x * (2.0f * (x * z - w * y)),
        };
    }
    if (axis == 1) {
        return {
            splat.scale.y * (2.0f * (x * y - w * z)),
            splat.scale.y * (1.0f - 2.0f * (x * x + z * z)),
            splat.scale.y * (2.0f * (y * z + w * x)),
        };
    }
    return {
        splat.scale.z * (2.0f * (x * z + w * y)),
        splat.scale.z * (2.0f * (y * z - w * x)),
        splat.scale.z * (1.0f - 2.0f * (x * x + y * y)),
    };
}

bool projectedAxis(
    Vec3 world_axis,
    Vec3 relative,
    const Systems::CameraState& camera,
    float depth,
    float tangent,
    float aspect,
    float *x,
    float *y)
{
    if (!x || !y || depth <= 1.0e-6f || tangent <= 1.0e-8f || aspect <= 1.0e-8f)
        return false;
    const float camera_x = Math::dot(relative, camera.right);
    const float camera_y = Math::dot(relative, camera.up);
    const float dx = Math::dot(world_axis, camera.right);
    const float dy = Math::dot(world_axis, camera.up);
    const float dz = Math::dot(world_axis, camera.forward);
    const float inverse_depth2 = 1.0f / (depth * depth);
    *x = (dx * depth - camera_x * dz) * inverse_depth2 / (aspect * tangent);
    *y = (dy * depth - camera_y * dz) * inverse_depth2 / tangent;
    return std::isfinite(*x) && std::isfinite(*y);
}

float ndcDepth(float depth, float near_plane, float far_plane)
{
    if (far_plane > near_plane) {
        return (far_plane + near_plane) / (far_plane - near_plane) -
            (2.0f * far_plane * near_plane) / ((far_plane - near_plane) * depth);
    }
    return 1.0f - 2.0f * near_plane / depth;
}

Vec3 localViewDirection(const Math::Mat4& model, Vec3 world_direction)
{
    Vec3 x_axis = Math::normalize(Math::transformVector(model, {1.0f, 0.0f, 0.0f}));
    Vec3 y_axis = Math::transformVector(model, {0.0f, 1.0f, 0.0f});
    y_axis = subtractScaled(y_axis, x_axis, Math::dot(y_axis, x_axis));
    y_axis = Math::normalize(y_axis);
    Vec3 z_axis = Math::normalize(Math::cross(x_axis, y_axis));
    const Vec3 original_z = Math::transformVector(model, {0.0f, 0.0f, 1.0f});
    if (Math::dot(z_axis, original_z) < 0.0f) z_axis = multiply(z_axis, -1.0f);
    world_direction = Math::normalize(world_direction);
    return {
        Math::dot(world_direction, x_axis),
        Math::dot(world_direction, y_axis),
        Math::dot(world_direction, z_axis),
    };
}

} // namespace

bool project(
    const Models::GaussianSplat::Splat& splat,
    std::uint32_t spherical_harmonic_degree,
    const Math::Mat4& model,
    const Systems::CameraState& camera,
    int width,
    int height,
    ProjectedSplat *output)
{
    if (!output || width <= 0 || height <= 0 || camera.projection != Camera::Projection::Perspective)
        return false;

    const Vec3 world = Math::transformPoint(model, {
        splat.position.x,
        splat.position.y,
        splat.position.z,
    });
    const Vec3 relative = subtract(world, camera.position);
    const float depth = Math::dot(relative, camera.forward);
    const float near_plane = std::max(camera.near_plane, 1.0e-4f);
    if (depth <= near_plane || (camera.far_plane > near_plane && depth >= camera.far_plane))
        return false;

    const float tangent = std::tan(std::clamp(camera.fov_degrees, 1.0f, 179.0f) * Pi / 360.0f);
    const float aspect = camera.aspect_ratio > 0.0f
        ? camera.aspect_ratio
        : static_cast<float>(width) / static_cast<float>(height);
    const float camera_x = Math::dot(relative, camera.right);
    const float camera_y = Math::dot(relative, camera.up);
    const float center_x = camera_x / (depth * aspect * tangent);
    const float center_y = camera_y / (depth * tangent);

    float projected_x[3]{};
    float projected_y[3]{};
    for (int axis = 0; axis < 3; ++axis) {
        const Vec3 world_axis = Math::transformVector(model, localAxis(splat, axis));
        if (!projectedAxis(
                world_axis,
                relative,
                camera,
                depth,
                tangent,
                aspect,
                &projected_x[axis],
                &projected_y[axis]))
            return false;
    }

    float covariance_xx = 0.0f;
    float covariance_xy = 0.0f;
    float covariance_yy = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        covariance_xx += projected_x[axis] * projected_x[axis];
        covariance_xy += projected_x[axis] * projected_y[axis];
        covariance_yy += projected_y[axis] * projected_y[axis];
    }
    const float pixel_x = 2.0f / static_cast<float>(width);
    const float pixel_y = 2.0f / static_cast<float>(height);
    covariance_xx += MinimumVariancePixels * pixel_x * pixel_x;
    covariance_yy += MinimumVariancePixels * pixel_y * pixel_y;

    const float trace = covariance_xx + covariance_yy;
    const float determinant = covariance_xx * covariance_yy - covariance_xy * covariance_xy;
    const float discriminant = std::sqrt(std::max(trace * trace * 0.25f - determinant, 0.0f));
    const float lambda0 = std::max(trace * 0.5f + discriminant, 0.0f);
    const float lambda1 = std::max(trace * 0.5f - discriminant, 0.0f);
    if (lambda0 <= 1.0e-16f) return false;

    float eigen_x = 0.0f;
    float eigen_y = 0.0f;
    if (std::abs(covariance_xy) > 1.0e-12f) {
        eigen_x = lambda0 - covariance_yy;
        eigen_y = covariance_xy;
    } else if (covariance_xx >= covariance_yy) {
        eigen_x = 1.0f;
    } else {
        eigen_y = 1.0f;
    }
    const float eigen_length = std::sqrt(eigen_x * eigen_x + eigen_y * eigen_y);
    if (eigen_length <= 1.0e-12f) return false;
    eigen_x /= eigen_length;
    eigen_y /= eigen_length;

    const float sigma0 = std::sqrt(lambda0);
    const float sigma1 = std::sqrt(lambda1);
    output->center_x = center_x;
    output->center_y = center_y;
    output->axis0_x = eigen_x * sigma0;
    output->axis0_y = eigen_y * sigma0;
    output->axis1_x = -eigen_y * sigma1;
    output->axis1_y = eigen_x * sigma1;
    const Vec3 local_view_direction = localViewDirection(model, relative);
    const Models::Vec3 sh_color = Models::GaussianSplat::evaluateSphericalHarmonics(
        splat,
        spherical_harmonic_degree,
        Models::Vec3{
            local_view_direction.x,
            local_view_direction.y,
            local_view_direction.z,
        }
    );
    output->color = {sh_color.x, sh_color.y, sh_color.z};
    output->opacity = splat.opacity;
    output->ndc_depth = std::clamp(ndcDepth(depth, near_plane, camera.far_plane), -1.0f, 1.0f);
    output->linear_depth = depth;
    output->distance_squared = lengthSquared(relative);

    const float radius_x = 3.0f * (std::abs(output->axis0_x) + std::abs(output->axis1_x));
    const float radius_y = 3.0f * (std::abs(output->axis0_y) + std::abs(output->axis1_y));
    return center_x + radius_x >= -1.0f && center_x - radius_x <= 1.0f &&
        center_y + radius_y >= -1.0f && center_y - radius_y <= 1.0f;
}

} // namespace Renderer::GaussianSplat
