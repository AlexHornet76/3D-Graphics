// Robust OBJ loader upgrade.
// - portable parsing (fgets/sscanf) instead of fscanf_s
// - supports v/vt/vn, v//vn, v/vt, v
// - triangulates n-gons (fan triangulation)
// - supports negative indices
// - computes missing normals (flat) and tangents/bitangents (if UVs exist)

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <vector>
#include <stdio.h>
#include <string>
#include <cstring>
#include <sstream>
#include <limits>
#include <algorithm>

#include "glm/glm.hpp"
#include "objloader.hpp"

static inline int toIndex(int idx, int count)
{
    // OBJ: 1-based positive indices, or negative indices relative to end
    if (idx > 0) return idx - 1;
    if (idx < 0) return count + idx;
    return -1;
}

static inline glm::vec3 safeNormalize(const glm::vec3& v)
{
    float len2 = glm::dot(v, v);
    if (len2 < 1e-20f) return glm::vec3(0, 0, 1);
    return v / sqrtf(len2);
}

static void computeFlatNormal(
    const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
    glm::vec3& outN)
{
    outN = safeNormalize(glm::cross(p1 - p0, p2 - p0));
}

static bool computeTBN(
    const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
    const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
    glm::vec3& outT, glm::vec3& outB)
{
    glm::vec3 e1 = p1 - p0;
    glm::vec3 e2 = p2 - p0;
    glm::vec2 dUV1 = uv1 - uv0;
    glm::vec2 dUV2 = uv2 - uv0;

    float det = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
    if (fabsf(det) < 1e-20f) {
        outT = glm::vec3(1, 0, 0);
        outB = glm::vec3(0, 1, 0);
        return false;
    }

    float f = 1.0f / det;
    outT = f * (e1 * dUV2.y - e2 * dUV1.y);
    outB = f * (-e1 * dUV2.x + e2 * dUV1.x);

    outT = safeNormalize(outT);
    outB = safeNormalize(outB);
    return true;
}

// Parse a single face vertex token:
// formats: v, v/vt, v//vn, v/vt/vn
static bool parseFaceVert(const std::string& token, int& v, int& vt, int& vn)
{
    v = vt = vn = 0;

    // Count slashes
    size_t s1 = token.find('/');
    if (s1 == std::string::npos) {
        // v
        v = std::stoi(token);
        return true;
    }

    size_t s2 = token.find('/', s1 + 1);

    std::string a = token.substr(0, s1);
    std::string b = (s2 == std::string::npos) ? token.substr(s1 + 1) : token.substr(s1 + 1, s2 - (s1 + 1));
    std::string c = (s2 == std::string::npos) ? "" : token.substr(s2 + 1);

    if (!a.empty()) v = std::stoi(a);
    if (!b.empty()) vt = std::stoi(b);
    if (!c.empty()) vn = std::stoi(c);

    return (v != 0);
}

bool loadOBJ2(const char* path, ObjMesh& outMesh, bool computeTangents)
{
    outMesh = ObjMesh{};

    printf("Loading OBJ file (robust) %s...\n", path);

    FILE* file = fopen(path, "rb");
    if (!file) {
        printf("Impossible to open the file: %s\n", path);
        return false;
    }

    std::vector<glm::vec3> tempPos;
    std::vector<glm::vec2> tempUV;
    std::vector<glm::vec3> tempNrm;

    // We will expand to "non-indexed" vertices as we parse faces
    std::vector<glm::vec3> pos;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> nrm;
    std::vector<glm::vec3> tan;
    std::vector<glm::vec3> bit;

    char line[2048];
    while (fgets(line, sizeof(line), file))
    {
        // Skip leading spaces
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;

        // Empty/comment
        if (*p == '\0' || *p == '\n' || *p == '#')
            continue;

        // Vertex position
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            glm::vec3 v;
            if (sscanf(p, "v %f %f %f", &v.x, &v.y, &v.z) == 3) {
                tempPos.push_back(v);
            }
            continue;
        }

        // Texture coord
        if (p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            glm::vec2 t;
            if (sscanf(p, "vt %f %f", &t.x, &t.y) >= 2) {
                // keep your old convention: invert V (DDS-style). Works fine for typical images too
                t.y = -t.y;
                tempUV.push_back(t);
            }
            continue;
        }

        // Normal
        if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            glm::vec3 nn;
            if (sscanf(p, "vn %f %f %f", &nn.x, &nn.y, &nn.z) == 3) {
                tempNrm.push_back(safeNormalize(nn));
            }
            continue;
        }

        // Face
        if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            // Tokenize: f a b c d ...
            std::istringstream iss(p);
            std::string head;
            iss >> head; // "f"

            std::vector<std::string> tokens;
            std::string tok;
            while (iss >> tok) {
                // stop at comment within line
                if (!tok.empty() && tok[0] == '#') break;
                tokens.push_back(tok);
            }

            if (tokens.size() < 3) continue;

            // Convert all face verts first
            struct FV { int v, vt, vn; };
            std::vector<FV> face;
            face.reserve(tokens.size());

            for (const auto& tkn : tokens) {
                int iv, it, in;
                if (!parseFaceVert(tkn, iv, it, in)) continue;
                face.push_back({ iv, it, in });
            }
            if (face.size() < 3) continue;

            // Triangulate via fan: (0, i, i+1)
            for (size_t i = 1; i + 1 < face.size(); i++) {
                FV fv0 = face[0];
                FV fv1 = face[i];
                FV fv2 = face[i + 1];

                int p0i = toIndex(fv0.v, (int)tempPos.size());
                int p1i = toIndex(fv1.v, (int)tempPos.size());
                int p2i = toIndex(fv2.v, (int)tempPos.size());
                if (p0i < 0 || p1i < 0 || p2i < 0) continue;

                glm::vec3 P0 = tempPos[p0i];
                glm::vec3 P1 = tempPos[p1i];
                glm::vec3 P2 = tempPos[p2i];

                bool hasUV = (!tempUV.empty() && fv0.vt != 0 && fv1.vt != 0 && fv2.vt != 0);
                bool hasVN = (!tempNrm.empty() && fv0.vn != 0 && fv1.vn != 0 && fv2.vn != 0);

                glm::vec2 UV0(0, 0), UV1(0, 0), UV2(0, 0);
                if (hasUV) {
                    int t0i = toIndex(fv0.vt, (int)tempUV.size());
                    int t1i = toIndex(fv1.vt, (int)tempUV.size());
                    int t2i = toIndex(fv2.vt, (int)tempUV.size());
                    if (t0i >= 0 && t1i >= 0 && t2i >= 0) {
                        UV0 = tempUV[t0i];
                        UV1 = tempUV[t1i];
                        UV2 = tempUV[t2i];
                    }
                    else {
                        hasUV = false;
                    }
                }

                glm::vec3 N0, N1, N2;
                if (hasVN) {
                    int n0i = toIndex(fv0.vn, (int)tempNrm.size());
                    int n1i = toIndex(fv1.vn, (int)tempNrm.size());
                    int n2i = toIndex(fv2.vn, (int)tempNrm.size());
                    if (n0i >= 0 && n1i >= 0 && n2i >= 0) {
                        N0 = tempNrm[n0i];
                        N1 = tempNrm[n1i];
                        N2 = tempNrm[n2i];
                    }
                    else {
                        hasVN = false;
                    }
                }

                if (!hasVN) {
                    glm::vec3 flatN;
                    computeFlatNormal(P0, P1, P2, flatN);
                    N0 = N1 = N2 = flatN;
                }

                glm::vec3 T(1, 0, 0), B(0, 1, 0);
                if (computeTangents && hasUV) {
                    computeTBN(P0, P1, P2, UV0, UV1, UV2, T, B);
                }

                unsigned int base = (unsigned int)pos.size();
                pos.push_back(P0); pos.push_back(P1); pos.push_back(P2);
                uvs.push_back(UV0); uvs.push_back(UV1); uvs.push_back(UV2);
                nrm.push_back(N0); nrm.push_back(N1); nrm.push_back(N2);

                tan.push_back(T); tan.push_back(T); tan.push_back(T);
                bit.push_back(B); bit.push_back(B); bit.push_back(B);

                // indices for optional indexed draw
                outMesh.indices.push_back(base + 0);
                outMesh.indices.push_back(base + 1);
                outMesh.indices.push_back(base + 2);
            }

            continue;
        }

        // ignore anything else: o, g, s, usemtl, mtllib, etc.
    }

    fclose(file);

    if (pos.empty()) {
        printf("OBJ loaded but produced 0 triangles: %s\n", path);
        return false;
    }

    outMesh.positions = std::move(pos);
    outMesh.uvs = std::move(uvs);
    outMesh.normals = std::move(nrm);
    outMesh.tangents = std::move(tan);
    outMesh.bitangents = std::move(bit);

    printf("OBJ OK: %s (verts=%zu, tris=%zu)\n", path, outMesh.positions.size(), outMesh.positions.size() / 3);
    return true;
}

// ---------------- Legacy loader (your original) ----------------
// Kept so old code still compiles; implemented via loadOBJ2 for convenience.
// It will still return expanded arrays (no indices).
bool loadOBJ(
    const char* path,
    std::vector<glm::vec3>& out_vertices,
    std::vector<glm::vec2>& out_uvs,
    std::vector<glm::vec3>& out_normals
)
{
    ObjMesh m;
    if (!loadOBJ2(path, m, false))
        return false;

    out_vertices = std::move(m.positions);
    out_uvs = std::move(m.uvs);
    out_normals = std::move(m.normals);
    return true;
}