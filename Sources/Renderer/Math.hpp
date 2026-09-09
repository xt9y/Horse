#ifndef RW_ENGINE_RENDERER_MATH_HPP
#define RW_ENGINE_RENDERER_MATH_HPP

#include "Renderer/Components.hpp"

#include <array>

namespace Renderer::Math {

using Mat4 = std::array<float, 16>;

Mat4 identityMatrix();
Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 translation(float x, float y, float z);
Mat4 scaling(float x, float y, float z);
Mat4 rotationX(float degrees);
Mat4 rotationY(float degrees);
Mat4 rotationZ(float degrees);
Mat4 modelMatrix(const Transform& transform);
Mat4 inverseModelMatrix(const Transform& transform);
Mat4 viewMatrix(
    const Vec3& position,
    const Vec3& forward,
    const Vec3& right,
    const Vec3& up
);

Vec3 transformPoint(const Mat4& matrix, const Vec3& point);
Vec3 transformNormal(const Mat4& world_to_object, const Vec3& normal);

float dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
Vec3 normalize(const Vec3& value);

} // namespace Renderer::Math

#endif