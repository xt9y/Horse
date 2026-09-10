#include "Renderer/Math.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::Math {
namespace {

constexpr float kPi = 3.14159265358979323846f;

} // namespace

float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(const Vec3& value)
{
    const float length_squared = dot(value, value);
    if (length_squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

Mat4 identityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
            }
        }
    }
    return result;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 result = identityMatrix();
    result[12] = x;
    result[13] = y;
    result[14] = z;
    return result;
}

Mat4 scaling(float x, float y, float z)
{
    Mat4 result{};
    result[0] = x;
    result[5] = y;
    result[10] = z;
    result[15] = 1.0f;
    return result;
}

Mat4 rotationX(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Mat4 result = identityMatrix();
    result[5] = cosine;
    result[6] = sine;
    result[9] = -sine;
    result[10] = cosine;
    return result;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = cosine;
    result[2] = -sine;
    result[8] = sine;
    result[10] = cosine;
    return result;
}

Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = cosine;
    result[1] = sine;
    result[4] = -sine;
    result[5] = cosine;
    return result;
}

Mat4 modelMatrix(const Transform& transform)
{
    return multiply(
        multiply(
            multiply(
                multiply(
                    translation(transform.position.x, transform.position.y, transform.position.z),
                    rotationX(transform.rotation.x)
                ),
                rotationY(transform.rotation.y)
            ),
            rotationZ(transform.rotation.z)
        ),
        scaling(transform.scale.x, transform.scale.y, transform.scale.z)
    );
}

Mat4 inverseModelMatrix(const Transform& transform)
{
    const float x = std::abs(transform.scale.x) > 1.0e-8f ? 1.0f / transform.scale.x : 0.0f;
    const float y = std::abs(transform.scale.y) > 1.0e-8f ? 1.0f / transform.scale.y : 0.0f;
    const float z = std::abs(transform.scale.z) > 1.0e-8f ? 1.0f / transform.scale.z : 0.0f;

    return multiply(
        multiply(
            multiply(
                multiply(
                    scaling(x, y, z),
                    rotationZ(-transform.rotation.z)
                ),
                rotationY(-transform.rotation.y)
            ),
            rotationX(-transform.rotation.x)
        ),
        translation(-transform.position.x, -transform.position.y, -transform.position.z)
    );
}

Mat4 perspective(float fov_degrees, float aspect, float near_plane, float far_plane)
{
    const float clamped_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float safe_far = std::max(far_plane, safe_near + 1.0e-3f);
    const float focal = 1.0f / std::tan(clamped_fov * (kPi / 360.0f));

    Mat4 result{};
    result[0] = focal / safe_aspect;
    result[5] = focal;
    result[10] = (safe_far + safe_near) / (safe_near - safe_far);
    result[11] = -1.0f;
    result[14] = (2.0f * safe_far * safe_near) / (safe_near - safe_far);
    return result;
}

Mat4 viewMatrix(
    const Vec3& position,
    const Vec3& forward,
    const Vec3& right,
    const Vec3& up)
{
    const Vec3 f = normalize(forward);
    const Vec3 r = normalize(right);
    const Vec3 u = normalize(up);

    return {
         r.x,  u.x, -f.x, 0.0f,
         r.y,  u.y, -f.y, 0.0f,
         r.z,  u.z, -f.z, 0.0f,
        -dot(r, position),
        -dot(u, position),
         dot(f, position),
         1.0f,
    };
}

Vec3 transformPoint(const Mat4& matrix, const Vec3& point)
{
    return {
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
    };
}

Vec3 transformNormal(const Mat4& world_to_object, const Vec3& normal)
{
    return normalize({
        world_to_object[0] * normal.x + world_to_object[1] * normal.y + world_to_object[2] * normal.z,
        world_to_object[4] * normal.x + world_to_object[5] * normal.y + world_to_object[6] * normal.z,
        world_to_object[8] * normal.x + world_to_object[9] * normal.y + world_to_object[10] * normal.z,
    });
}

} // namespace Renderer::Math