#include "Models/Models.hpp"

#include <cmath>
#include <string>

namespace {

bool near(float a, float b)
{
    return std::fabs(a - b) < 1.0e-6f;
}

} // namespace

int main()
{
    Models::clearCache();

    std::string error;
    const Models::ModelHandle model = Models::load("tests/fixtures/earth_regression.fbx", &error);
    if (model == Models::INVALID_MODEL || Models::partCount(model) == 0u) return 2;

    const Models::ModelPart *part = Models::part(model, 0u);
    if (!part) return 3;

    const Models::MaterialData *original = Models::material(part->material);
    if (!original) return 4;

    Models::MaterialData replacement = *original;
    replacement.color = {0.125f, 0.5f, 0.875f};
    replacement.opacity = 0.625f;
    replacement.texture_path = "synthetic-diffuse.jpg";
    replacement.diffuse_texture = 77u;

    if (!Models::updateMaterial(part->material, replacement)) return 5;

    const Models::MaterialData *updated = Models::material(part->material);
    if (!updated) return 6;
    if (!near(updated->color.x, 0.125f) || !near(updated->color.y, 0.5f) || !near(updated->color.z, 0.875f)) return 7;
    if (!near(updated->opacity, 0.625f)) return 8;
    if (updated->texture_path != "synthetic-diffuse.jpg") return 9;
    if (updated->diffuse_texture != 77u) return 10;

    Models::clearCache();
    return 0;
}
