#include "Renderer/Math.hpp"

#include <cassert>
#include <cmath>

namespace {

bool close(float a, float b, float epsilon = 1.0e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

Renderer::Vec3 addScaled(
    const Renderer::Vec3& origin,
    const Renderer::Vec3& direction,
    float amount)
{
    return {
        origin.x + direction.x * amount,
        origin.y + direction.y * amount,
        origin.z + direction.z * amount,
    };
}

} // namespace

int main()
{
    const Renderer::Vec3 position {3.0f, 2.0f, -4.0f};
    const Renderer::Vec3 forward = Renderer::Math::normalize({
        -0.61237246f,
         0.5f,
        -0.61237246f,
    });
    const Renderer::Vec3 right = Renderer::Math::normalize({
         0.70710677f,
         0.0f,
        -0.70710677f,
    });
    const Renderer::Vec3 up = Renderer::Math::normalize(
        Renderer::Math::cross(right, forward)
    );

    const Renderer::Math::Mat4 view = Renderer::Math::viewMatrix(
        position,
        forward,
        right,
        up
    );

    const Renderer::Vec3 ahead = Renderer::Math::transformPoint(
        view,
        addScaled(position, forward, 10.0f)
    );
    assert(close(ahead.x, 0.0f));
    assert(close(ahead.y, 0.0f));
    assert(close(ahead.z, -10.0f));

    Renderer::Vec3 ahead_right = addScaled(position, forward, 10.0f);
    ahead_right = addScaled(ahead_right, right, 2.0f);
    const Renderer::Vec3 camera_right = Renderer::Math::transformPoint(view, ahead_right);
    assert(close(camera_right.x, 2.0f));
    assert(close(camera_right.y, 0.0f));
    assert(close(camera_right.z, -10.0f));

    return 0;
}
