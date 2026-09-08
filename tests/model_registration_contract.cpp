#include "Models/Models.hpp"

#include <utility>

int main()
{
    Models::MeshData mesh;
    mesh.vertices = {
        Models::Vertex{{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {}},
        Models::Vertex{{ 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {}},
        Models::Vertex{{ 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f}, {}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.bounds.minimum = {-1.0f, 0.0f, 0.0f};
    mesh.bounds.maximum = { 1.0f, 1.0f, 0.0f};

    Models::MaterialData material;
    material.color = {0.25f, 0.5f, 0.75f};

    const Models::MeshHandle mesh_handle = Models::registerMesh(std::move(mesh));
    const Models::MaterialHandle material_handle = Models::registerMaterial(std::move(material));

    if (mesh_handle == Models::INVALID_MESH) return 2;
    if (material_handle == Models::INVALID_MATERIAL) return 3;

    const Models::MeshData *stored_mesh = Models::mesh(mesh_handle);
    const Models::MaterialData *stored_material = Models::material(material_handle);
    if (!stored_mesh || stored_mesh->vertices.size() != 3u || stored_mesh->indices.size() != 3u) return 4;
    if (!stored_material || stored_material->color.y != 0.5f) return 5;

    Models::clearCache();
    if (Models::mesh(mesh_handle) != nullptr) return 6;
    if (Models::material(material_handle) != nullptr) return 7;

    return 0;
}
