#ifndef OBJLOADER_H
#define OBJLOADER_H

#include <vector>
#include "glm/glm.hpp"

// Original simple loader (kept for compatibility)
bool loadOBJ(
    const char* path,
    std::vector<glm::vec3>& out_vertices,
    std::vector<glm::vec2>& out_uvs,
    std::vector<glm::vec3>& out_normals
);

// Rich mesh for rendering with your shader (includes tangents/bitangents)
struct ObjMesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    // For normal mapping (same idea as your procedural BuildAlley)
    std::vector<glm::vec3> tangents;
    std::vector<glm::vec3> bitangents;

    // Triangle indices into the above arrays (optional; you can also draw arrays)
    std::vector<unsigned int> indices;
};

// Robust OBJ loader:
// - supports v, vt, vn
// - faces: v/vt/vn, v//vn, v/vt, v
// - triangulates quads/ngons
// - supports negative indices
// - computes missing normals (flat) and/or tangents (if UVs exist)
bool loadOBJ2(const char* path, ObjMesh& outMesh, bool computeTangents = true);

#endif