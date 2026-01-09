// Cyberpunk Alley + NORMAL MAPPING + STEAM + Fog + REAL SHADOW MAPPING (3 lights, 2D shadow maps)
// Texturi langa exe/cpp:
//  - asphalt.jpg
//  - asphalt_n.jpg (sau .png)
//  - wall.jpg
//  - wall_n.jpg
//  - sign3.png
//
// Controale:
//  sageti = orbit (cuaternioni), +/- zoom
//  i/j/k/l = muta lightPos[0] (key light) pe Y/Z
//  n = toggle normal mapping
//  f = toggle fog
//  m = toggle shadow mapping

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <math.h>

#include <GL/glew.h>
#include <GL/freeglut.h>

#include "loadShaders.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"

#include "SOIL.h"

// OBJ loader
#include "objloader.hpp"

// ---------------- Globals / constants ----------------
static const float PI = 3.1415926535f;

// lights
static const int LIGHT_COUNT = 3;

// shadow map (depth)
static const int SHADOW_RES = 2048;
static const int SHADOW_TEX_UNIT_BASE = 5; // we will use 5,6,7

// ---------------- OpenGL ids ----------------
GLuint ProgramId = 0;          // main shading program
GLuint ShadowProgramId = 0;    // depth-only program

GLuint SceneVaoId = 0, SceneVboId = 0;

// textures (albedo)
GLuint texAsphalt = 0;
GLuint texWall = 0;
GLuint texSign = 0;

// textures (normal maps)
GLuint texAsphaltN = 0;
GLuint texWallN = 0;

// shadow maps (3 lights)
GLuint ShadowFBO[LIGHT_COUNT] = { 0, 0, 0 };
GLuint ShadowDepthTex[LIGHT_COUNT] = { 0, 0, 0 };

// uniforms (main)
GLuint myMatrixLocation = 0, viewLocation = 0, projLocation = 0;
GLuint matrUmbraLocation = 0; // legacy planar shadow matrix (still kept; not required for shadow mapping)
GLuint viewPosLocation = 0, codColLocation = 0;
GLuint exposureLocation = 0;
GLuint gammaLocation = 0;
GLuint timeSecLocation = 0;

// shadow mapping uniforms (main)
GLuint lightSpaceLocation_Main = 0;   // mat4 lightSpace[3]
GLuint shadowMapLocation_Main = 0;    // sampler2D shadowMap[3]
GLuint useShadowMapLocation = 0;

// lights
GLuint lightPosLocation = 0, lightColorLocation = 0;

// texture uniforms
GLuint texAsphaltLoc = 0, texWallLoc = 0, texSignLoc = 0;
GLuint texAsphaltNLoc = 0, texWallNLoc = 0;

GLuint useTexLocation = 0;
GLuint useNormalMapLocation = 0;
GLuint texTilingLocation = 0;

// sign helper
GLuint signBlackKeyLocation = 0;

// fog
GLuint useFogLocation = 0;
static int gUseFog = 1;

// shadow mapping toggle
static int gUseShadowMap = 1;

// uniforms (shadow program)
GLuint myMatrixLocation_Shadow = 0;
GLuint lightSpaceLocation_Shadow = 0; // mat4 lampLightSpace (reused per light)

// ---------------- Camera ----------------
// camera orbit
float refX = 0.0f, refY = 0.0f, refZ = 1.2f;

// OLD (kept for compatibility / not used anymore for rotation)
float alpha = PI / 10.0f, beta = -PI / 2.0f;

float dist = 11.0f;
float obsX, obsY, obsZ;

// quaternion camera orientation
static glm::quat camRot = glm::normalize(
    glm::angleAxis(beta + PI / 2.0f, glm::vec3(0, 0, 1)) *
    glm::angleAxis(-alpha, glm::vec3(1, 0, 0))
);
static float camPitchAccum = alpha;

glm::mat4 view;
glm::mat4 projection;

// projection
float width = 1200, height = 900, dNear = 0.2f, fov = 60.f * PI / 180.f;

// ---------------- Lighting data ----------------
glm::vec3 lightPos[LIGHT_COUNT] = {
    glm::vec3(-1.5f, -4.7f, 0.3f), // key light (movable) => casts shadows
    glm::vec3(2.0f,  0.5f, 2.9f),
    glm::vec3(0.0f,  2.5f, 4.1f)
};

glm::vec3 lightColor[LIGHT_COUNT] = {
    glm::vec3(0.15f, 1.20f, 1.20f), // cyan/blue-ish
    glm::vec3(1.20f, 0.15f, 1.10f), // magenta-ish
    glm::vec3(0.25f, 1.20f, 0.35f)  // green-ish
};

int codCol = 0;

// legacy planar shadow matrix (still available)
float matrUmbra[4][4];

// ---------------- Mesh data ----------------
struct Vtx {
    glm::vec4 pos;   // 0
    glm::vec3 col;   // 1
    glm::vec3 nrm;   // 2
    glm::vec2 uv;    // 3
    float texId;     // 4 (0=asphalt,1=wall,2=sign,3=steam)
    glm::vec3 tan;   // 5
    glm::vec3 bit;   // 6
};

std::vector<Vtx> gVertices;

// draw ranges
GLint groundFirst = 0;
GLsizei groundCount = 0;

GLint castersFirst = 0;
GLsizei castersCount = 0;

// steam range + shadow casters range (casters without steam)
GLint steamFirst = 0;
GLsizei steamCount = 0;
GLsizei shadowCastersCount = 0;

// ---------------- Input ----------------
void processNormalKeys(unsigned char key, int x, int y)
{
    switch (key) {
    case '+': dist -= 0.35f; if (dist < 2.0f) dist = 2.0f; break;
    case '-': dist += 0.35f; break;

    case 'j': lightPos[0].y -= 0.2f; break;
    case 'l': lightPos[0].y += 0.2f; break;
    case 'i': lightPos[0].z += 0.2f; break;
    case 'k': lightPos[0].z -= 0.2f; break;

    case 'n':
    {
        static int enabled = 1;
        enabled = 1 - enabled;
        glUseProgram(ProgramId);
        glUniform1i(useNormalMapLocation, enabled);
        printf("Normal mapping: %s\n", enabled ? "ON" : "OFF");
    }
    break;

    case 'f': // toggle fog
    {
        gUseFog = 1 - gUseFog;
        glUseProgram(ProgramId);
        glUniform1i(useFogLocation, gUseFog);
        printf("Fog: %s\n", gUseFog ? "ON" : "OFF");
    }
    break;

    case 'm': // toggle shadow mapping
    {
        gUseShadowMap = 1 - gUseShadowMap;
        glUseProgram(ProgramId);
        glUniform1i(useShadowMapLocation, gUseShadowMap);
        printf("Shadow mapping: %s\n", gUseShadowMap ? "ON" : "OFF");
    }
    break;
    }

    if (key == 27) exit(0);
    glutPostRedisplay();
}

void processSpecialKeys(int key, int xx, int yy)
{
    const float yawStep = 0.04f;
    const float pitchStep = 0.04f;

    float newPitch = camPitchAccum;

    if (key == GLUT_KEY_UP)   newPitch += pitchStep;
    if (key == GLUT_KEY_DOWN) newPitch -= pitchStep;

    float maxPitch = PI / 2.1f;
    float minPitch = -PI / 10.0f;
    if (newPitch > maxPitch) newPitch = maxPitch;
    if (newPitch < minPitch) newPitch = minPitch;

    float dPitch = newPitch - camPitchAccum;
    camPitchAccum = newPitch;

    float dYaw = 0.0f;
    if (key == GLUT_KEY_LEFT)  dYaw -= yawStep;
    if (key == GLUT_KEY_RIGHT) dYaw += yawStep;

    if (fabsf(dYaw) > 1e-8f) {
        glm::quat qYaw = glm::angleAxis(dYaw, glm::vec3(0, 0, 1));
        camRot = glm::normalize(qYaw * camRot);
    }

    if (fabsf(dPitch) > 1e-8f) {
        glm::vec3 localX = glm::normalize(camRot * glm::vec3(1, 0, 0));
        glm::quat qPitch = glm::angleAxis(-dPitch, localX);
        camRot = glm::normalize(qPitch * camRot);
    }

    glutPostRedisplay();
}

// ---------------- Texture loader (SOIL) ----------------
static GLuint LoadTexture2D_SOIL(const char* path)
{
    int w, h, ch;
    unsigned char* img = SOIL_load_image(path, &w, &h, &ch, SOIL_LOAD_AUTO);
    if (!img) {
        printf("Nu am putut incarca textura: %s\nSOIL: %s\n", path, SOIL_last_result());
        return 0;
    }

    GLenum format = (ch == 4) ? GL_RGBA : GL_RGB;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, img);
    glGenerateMipmap(GL_TEXTURE_2D);

    SOIL_free_image_data(img);
    glBindTexture(GL_TEXTURE_2D, 0);

    printf("Textura OK: %s (%dx%d, ch=%d)\n", path, w, h, ch);
    return tex;
}

// ---------------- Geometry helpers ----------------
static void pushTri(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
    const glm::vec3& n, const glm::vec3& col,
    const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
    float texId,
    const glm::vec3& tan, const glm::vec3& bit)
{
    gVertices.push_back({ glm::vec4(p0, 1.0f), col, n, uv0, texId, tan, bit });
    gVertices.push_back({ glm::vec4(p1, 1.0f), col, n, uv1, texId, tan, bit });
    gVertices.push_back({ glm::vec4(p2, 1.0f), col, n, uv2, texId, tan, bit });
}

static void computeTBN(
    const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
    const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
    glm::vec3& outT, glm::vec3& outB)
{
    glm::vec3 e1 = p1 - p0;
    glm::vec3 e2 = p2 - p0;
    glm::vec2 dUV1 = uv1 - uv0;
    glm::vec2 dUV2 = uv2 - uv0;

    float denom = (dUV1.x * dUV2.y - dUV2.x * dUV1.y);
    if (fabsf(denom) < 1e-20f) {
        outT = glm::vec3(1, 0, 0);
        outB = glm::vec3(0, 1, 0);
        return;
    }

    float f = 1.0f / denom;
    outT = f * (e1 * dUV2.y - e2 * dUV1.y);
    outB = f * (-e1 * dUV2.x + e2 * dUV1.x);

    outT = glm::normalize(outT);
    outB = glm::normalize(outB);
}

static void appendQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
    const glm::vec3& n, const glm::vec3& col,
    const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
    float texId)
{
    glm::vec3 T1, B1;
    computeTBN(p0, p1, p2, uv0, uv1, uv2, T1, B1);
    pushTri(p0, p1, p2, n, col, uv0, uv1, uv2, texId, T1, B1);

    glm::vec3 T2, B2;
    computeTBN(p0, p2, p3, uv0, uv2, uv3, T2, B2);
    pushTri(p0, p2, p3, n, col, uv0, uv2, uv3, texId, T2, B2);
}

// Steam billboards (texIdSteam = 3)
static void appendSteamPuff(const glm::vec3& center, float height, float radius, float texIdSteam, float intensity)
{
    glm::vec3 col(intensity, intensity, intensity); // intensity packed in vColor.r

    auto addBillboard = [&](float ang)
        {
            glm::vec3 right = glm::vec3(cosf(ang), sinf(ang), 0.0f) * radius;
            glm::vec3 up = glm::vec3(0.0f, 0.0f, height);

            glm::vec3 p0 = center - right;
            glm::vec3 p1 = center + right;
            glm::vec3 p2 = center + right + up;
            glm::vec3 p3 = center - right + up;

            glm::vec3 n(0, 1, 0);
            glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);

            glm::vec3 T(1, 0, 0), B(0, 0, 1);

            pushTri(p0, p1, p2, n, col, uv0, uv1, uv2, texIdSteam, T, B);
            pushTri(p0, p2, p3, n, col, uv0, uv2, uv3, texIdSteam, T, B);
        };

    addBillboard(0.0f);
    addBillboard(PI * 0.5f);
    addBillboard(PI * 0.25f);
}

// OBJ mesh append
static void appendObjMesh(const ObjMesh& m, const glm::mat4& M, const glm::vec3& col, float texId, bool forceUpNormals = false)
{
    glm::mat3 Nmat = glm::transpose(glm::inverse(glm::mat3(M)));

    size_t triCount = m.positions.size() / 3;
    for (size_t i = 0; i < triCount; i++) {
        glm::vec3 p0 = glm::vec3(M * glm::vec4(m.positions[i * 3 + 0], 1.0f));
        glm::vec3 p1 = glm::vec3(M * glm::vec4(m.positions[i * 3 + 1], 1.0f));
        glm::vec3 p2 = glm::vec3(M * glm::vec4(m.positions[i * 3 + 2], 1.0f));

        glm::vec3 n0 = glm::normalize(Nmat * m.normals[i * 3 + 0]);
        glm::vec3 n1 = glm::normalize(Nmat * m.normals[i * 3 + 1]);
        glm::vec3 n2 = glm::normalize(Nmat * m.normals[i * 3 + 2]);

        if (forceUpNormals) {
            n0 = n1 = n2 = glm::vec3(0, 0, 1);
        }

        glm::vec2 uv0(0, 0), uv1(0, 0), uv2(0, 0);
        if (m.uvs.size() == m.positions.size()) {
            uv0 = m.uvs[i * 3 + 0];
            uv1 = m.uvs[i * 3 + 1];
            uv2 = m.uvs[i * 3 + 2];
        }

        glm::vec3 t0(1, 0, 0), b0(0, 1, 0);
        if (m.tangents.size() == m.positions.size() && m.bitangents.size() == m.positions.size()) {
            t0 = glm::normalize(Nmat * m.tangents[i * 3 + 0]);
            b0 = glm::normalize(Nmat * m.bitangents[i * 3 + 0]);
        }

        pushTri(p0, p1, p2, n0, col, uv0, uv1, uv2, texId, t0, b0);
    }
}

// --- forward decls for wall/pipe/ladder/cable helpers ---
static void appendThinPipePrism(
    float xPlane, float yCenter, float z0, float z1, float r, bool onLeftWall,
    const glm::vec3& col, float texIdWall);

static void appendWallBox(
    float xPlane, float yCenter, float zCenter,
    float sx, float sy, float sz, bool onLeftWall,
    const glm::vec3& col, float texIdWall);

static void appendWallLadder(
    float xPlane, float yCenter, float z0,
    float height, float width, bool onLeftWall,
    const glm::vec3& col, float texIdWall);

static void appendWallVent(
    float xPlane, float yCenter, float zCenter,
    float w, float h, bool onLeftWall,
    const glm::vec3& col, float texIdWall);

static void appendCable(
    const glm::vec3& pA, const glm::vec3& pB,
    float sag, int segments, float halfWidth,
    const glm::vec3& col, float texIdWall);

// ----------------------------
// Helpers implementations (same as your version)
// ----------------------------
static void appendThinPipePrism(
    float xPlane,
    float yCenter,
    float z0, float z1,
    float r,
    bool onLeftWall,
    const glm::vec3& col,
    float texIdWall)
{
    float xIn = xPlane + (onLeftWall ? +0.01f : -0.01f);

    float x0 = xIn - (onLeftWall ? 0.0f : r);
    float x1 = xIn + (onLeftWall ? r : 0.0f);

    float y0 = yCenter - r;
    float y1 = yCenter + r;

    glm::vec3 A(x0, y0, z0);
    glm::vec3 B(x0, y1, z0);
    glm::vec3 C(x0, y1, z1);
    glm::vec3 D(x0, y0, z1);

    glm::vec3 E(x1, y0, z0);
    glm::vec3 F(x1, y1, z0);
    glm::vec3 G(x1, y1, z1);
    glm::vec3 H(x1, y0, z1);

    glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);

    appendQuad(A, B, C, D, glm::vec3(-1, 0, 0), col, uv0, uv1, uv2, uv3, texIdWall);
    appendQuad(E, H, G, F, glm::vec3(1, 0, 0), col, uv0, uv1, uv2, uv3, texIdWall);

    appendQuad(A, D, H, E, glm::vec3(0, -1, 0), col, uv0, uv1, uv2, uv3, texIdWall);
    appendQuad(B, F, G, C, glm::vec3(0, 1, 0), col, uv0, uv1, uv2, uv3, texIdWall);

    appendQuad(A, E, F, B, glm::vec3(0, 0, -1), col, uv0, uv1, uv2, uv3, texIdWall);
    appendQuad(D, C, G, H, glm::vec3(0, 0, 1), col, uv0, uv1, uv2, uv3, texIdWall);
}

static void appendWallBox(
    float xPlane,
    float yCenter,
    float zCenter,
    float sx, float sy, float sz,
    bool onLeftWall,
    const glm::vec3& col,
    float texIdWall)
{
    float xIn = xPlane + (onLeftWall ? +0.03f : -0.03f);

    float x0 = xIn - (onLeftWall ? 0.0f : sx);
    float x1 = xIn + (onLeftWall ? sx : 0.0f);

    float y0 = yCenter - sy;
    float y1 = yCenter + sy;
    float z0 = zCenter - sz;
    float z1 = zCenter + sz;

    glm::vec3 A(x0, y0, z0);
    glm::vec3 B(x0, y1, z0);
    glm::vec3 C(x0, y1, z1);
    glm::vec3 D(x0, y0, z1);

    glm::vec3 E(x1, y0, z0);
    glm::vec3 F(x1, y1, z0);
    glm::vec3 G(x1, y1, z1);
    glm::vec3 H(x1, y0, z1);

    glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);

    appendQuad(A, B, C, D, glm::vec3(-1, 0, 0), col, uv0, uv1, uv2, uv3, texIdWall);
    appendQuad(E, H, G, F, glm::vec3(1, 0, 0), col, uv0, uv1, uv2, uv3, texIdWall);

    appendQuad(A, D, H, E, glm::vec3(0, -1, 0), col, uv0, uv1, uv2, uv3, texIdWall);
    appendQuad(B, F, G, C, glm::vec3(0, 1, 0), col, uv0, uv1, uv2, uv3, texIdWall);

    appendQuad(A, E, F, B, glm::vec3(0, 0, -1), col, uv0, uv1, uv2, uv3, texIdWall);
    appendQuad(D, C, G, H, glm::vec3(0, 0, 1), col, uv0, uv1, uv2, uv3, texIdWall);
}

static void appendWallLadder(
    float xPlane,
    float yCenter,
    float z0,
    float height,
    float width,
    bool onLeftWall,
    const glm::vec3& col,
    float texIdWall)
{
    float railR = 0.02f;
    float stepR = 0.015f;

    float yL = yCenter - width * 0.5f;
    float yR = yCenter + width * 0.5f;

    appendThinPipePrism(xPlane, yL, z0, z0 + height, railR, onLeftWall, col, texIdWall);
    appendThinPipePrism(xPlane, yR, z0, z0 + height, railR, onLeftWall, col, texIdWall);

    int steps = (int)(height / 0.35f);
    if (steps < 3) steps = 3;

    float xIn = xPlane + (onLeftWall ? +0.03f : -0.03f);
    float x0 = xIn - (onLeftWall ? 0.0f : stepR);
    float x1 = xIn + (onLeftWall ? stepR : 0.0f);

    for (int i = 0; i <= steps; i++) {
        float z = z0 + (height * (float)i / (float)steps);

        float y0 = yL;
        float y1 = yR;

        float z0s = z - stepR;
        float z1s = z + stepR;

        glm::vec3 A(x0, y0, z0s);
        glm::vec3 B(x0, y1, z0s);
        glm::vec3 C(x0, y1, z1s);
        glm::vec3 D(x0, y0, z1s);

        glm::vec3 E(x1, y0, z0s);
        glm::vec3 F(x1, y1, z0s);
        glm::vec3 G(x1, y1, z1s);
        glm::vec3 H(x1, y0, z1s);

        glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);

        appendQuad(A, B, C, D, glm::vec3(-1, 0, 0), col, uv0, uv1, uv2, uv3, texIdWall);
        appendQuad(E, H, G, F, glm::vec3(1, 0, 0), col, uv0, uv1, uv2, uv3, texIdWall);

        appendQuad(A, D, H, E, glm::vec3(0, -1, 0), col, uv0, uv1, uv2, uv3, texIdWall);
        appendQuad(B, F, G, C, glm::vec3(0, 1, 0), col, uv0, uv1, uv2, uv3, texIdWall);

        appendQuad(A, E, F, B, glm::vec3(0, 0, -1), col, uv0, uv1, uv2, uv3, texIdWall);
        appendQuad(D, C, G, H, glm::vec3(0, 0, 1), col, uv0, uv1, uv2, uv3, texIdWall);
    }
}

static void appendWallVent(
    float xPlane,
    float yCenter,
    float zCenter,
    float w, float h,
    bool onLeftWall,
    const glm::vec3& col,
    float texIdWall)
{
    float x = xPlane + (onLeftWall ? +0.02f : -0.02f);
    glm::vec3 n = onLeftWall ? glm::vec3(1, 0, 0) : glm::vec3(-1, 0, 0);

    glm::vec3 p0(x, yCenter - w * 0.5f, zCenter - h * 0.5f);
    glm::vec3 p1(x, yCenter + w * 0.5f, zCenter - h * 0.5f);
    glm::vec3 p2(x, yCenter + w * 0.5f, zCenter + h * 0.5f);
    glm::vec3 p3(x, yCenter - w * 0.5f, zCenter + h * 0.5f);

    glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
    appendQuad(p0, p1, p2, p3, n, col, uv0, uv1, uv2, uv3, texIdWall);

    float r = 0.015f;
    appendThinPipePrism(xPlane, yCenter - w * 0.5f, zCenter - h * 0.5f, zCenter + h * 0.5f, r, onLeftWall, col, texIdWall);
    appendThinPipePrism(xPlane, yCenter + w * 0.5f, zCenter - h * 0.5f, zCenter + h * 0.5f, r, onLeftWall, col, texIdWall);
}

static void appendCable(
    const glm::vec3& pA,
    const glm::vec3& pB,
    float sag,
    int segments,
    float halfWidth,
    const glm::vec3& col,
    float texIdWall)
{
    auto pointAt = [&](float t) -> glm::vec3 {
        glm::vec3 p = (1.0f - t) * pA + t * pB;
        float s = 4.0f * t * (1.0f - t);
        p.z -= sag * s;
        return p;
        };

    glm::vec3 up(0, 0, 1);

    for (int i = 0; i < segments; i++) {
        float t0 = (float)i / (float)segments;
        float t1 = (float)(i + 1) / (float)segments;

        glm::vec3 a = pointAt(t0);
        glm::vec3 b = pointAt(t1);

        glm::vec3 dir = b - a;
        float len2 = glm::dot(dir, dir);
        if (len2 < 1e-10f) continue;
        dir = glm::normalize(dir);

        glm::vec3 side = glm::cross(dir, up);
        float s2 = glm::dot(side, side);
        if (s2 < 1e-10f) side = glm::vec3(1, 0, 0);
        else side = glm::normalize(side);

        glm::vec3 o = side * halfWidth;

        glm::vec3 p0 = a - o;
        glm::vec3 p1 = a + o;
        glm::vec3 p2 = b + o;
        glm::vec3 p3 = b - o;

        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p3 - p0));
        if (glm::dot(n, n) < 1e-10f) n = glm::vec3(0, 1, 0);

        glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
        appendQuad(p0, p1, p2, p3, n, col, uv0, uv1, uv2, uv3, texIdWall);

        glm::vec3 side2 = glm::normalize(glm::cross(dir, side));
        glm::vec3 o2 = side2 * halfWidth;

        glm::vec3 q0 = a - o2;
        glm::vec3 q1 = a + o2;
        glm::vec3 q2 = b + o2;
        glm::vec3 q3 = b - o2;

        glm::vec3 n2 = glm::normalize(glm::cross(q1 - q0, q3 - q0));
        if (glm::dot(n2, n2) < 1e-10f) n2 = glm::vec3(1, 0, 0);

        appendQuad(q0, q1, q2, q3, n2, col, uv0, uv1, uv2, uv3, texIdWall);
    }
}

// ---------------- Build scene ----------------
static void BuildAlley()
{
    gVertices.clear();
    gVertices.reserve(200000);

    float halfW = 2.2f;
    float len = 10.0f;
    float wallH = 6.0f;

    glm::vec3 tint(1.0f, 1.0f, 1.0f);

    const float TEX_ASPHALT = 0.0f;
    const float TEX_WALL = 1.0f;
    const float TEX_SIGN = 2.0f;
    const float TEX_STEAM = 3.0f;

    // ground
    groundFirst = (GLint)gVertices.size();
    {
        glm::vec3 p0(-halfW, -len * 0.5f, 0.0f);
        glm::vec3 p1(halfW, -len * 0.5f, 0.0f);
        glm::vec3 p2(halfW, len * 0.5f, 0.0f);
        glm::vec3 p3(-halfW, len * 0.5f, 0.0f);

        glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
        appendQuad(p0, p1, p2, p3, glm::vec3(0, 0, 1), tint, uv0, uv1, uv2, uv3, TEX_ASPHALT);
    }
    groundCount = (GLsizei)gVertices.size() - groundFirst;

    // everything after this is considered "casters"
    castersFirst = (GLint)gVertices.size();

    // walls + end wall
    {
        glm::vec3 p0(-halfW, -len * 0.5f, 0.0f);
        glm::vec3 p1(-halfW, len * 0.5f, 0.0f);
        glm::vec3 p2(-halfW, len * 0.5f, wallH);
        glm::vec3 p3(-halfW, -len * 0.5f, wallH);
        glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
        appendQuad(p0, p1, p2, p3, glm::vec3(1, 0, 0), tint, uv0, uv1, uv2, uv3, TEX_WALL);
    }
    {
        glm::vec3 p0(halfW, len * 0.5f, 0.0f);
        glm::vec3 p1(halfW, -len * 0.5f, 0.0f);
        glm::vec3 p2(halfW, -len * 0.5f, wallH);
        glm::vec3 p3(halfW, len * 0.5f, wallH);
        glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
        appendQuad(p0, p1, p2, p3, glm::vec3(-1, 0, 0), tint, uv0, uv1, uv2, uv3, TEX_WALL);
    }
    {
        float y = len * 0.5f;
        glm::vec3 p0(-halfW, y, 0.0f);
        glm::vec3 p1(halfW, y, 0.0f);
        glm::vec3 p2(halfW, y, wallH);
        glm::vec3 p3(-halfW, y, wallH);
        glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
        appendQuad(p0, p1, p2, p3, glm::vec3(0, -1, 0), tint, uv0, uv1, uv2, uv3, TEX_WALL);
    }

    // signs
    auto addSignLeft = [&](float y, float z, float w, float h)
        {
            float x = -halfW + 0.02f;
            glm::vec3 p0(x, y - w * 0.5f, z - h * 0.5f);
            glm::vec3 p1(x, y + w * 0.5f, z - h * 0.5f);
            glm::vec3 p2(x, y + w * 0.5f, z + h * 0.5f);
            glm::vec3 p3(x, y - w * 0.5f, z + h * 0.5f);
            glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
            appendQuad(p0, p1, p2, p3, glm::vec3(1, 0, 0), tint, uv0, uv1, uv2, uv3, TEX_SIGN);
        };

    auto addSignRight = [&](float y, float z, float w, float h)
        {
            float x = halfW - 0.02f;
            glm::vec3 p0(x, y + w * 0.5f, z - h * 0.5f);
            glm::vec3 p1(x, y - w * 0.5f, z - h * 0.5f);
            glm::vec3 p2(x, y - w * 0.5f, z + h * 0.5f);
            glm::vec3 p3(x, y + w * 0.5f, z + h * 0.5f);
            glm::vec2 uv0(0, 0), uv1(1, 0), uv2(1, 1), uv3(0, 1);
            appendQuad(p0, p1, p2, p3, glm::vec3(-1, 0, 0), tint, uv0, uv1, uv2, uv3, TEX_SIGN);
        };

    addSignLeft(-2.0f, 2.6f, 1.6f, 0.7f);
    addSignLeft(1.5f, 1.8f, 1.2f, 0.6f);
    addSignRight(0.5f, 2.2f, 1.8f, 0.8f);

    // props (trash + manhole)
    ObjMesh trashMesh, manholeMesh;
    if (!loadOBJ2("trashcan.obj", trashMesh, true)) printf("WARN: could not load trashcan.obj\n");
    if (!loadOBJ2("manhole.obj", manholeMesh, true)) printf("WARN: could not load manhole.obj\n");

    const float TRASH_SCALE = 1.8f;

    auto placeTrash = [&](glm::vec3 pos, float rotZ, glm::vec3 col)
        {
            if (trashMesh.positions.empty()) return;
            glm::mat4 M =
                glm::translate(glm::mat4(1.0f), pos) *
                glm::rotate(glm::mat4(1.0f), rotZ, glm::vec3(0, 0, 1)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(TRASH_SCALE));
            appendObjMesh(trashMesh, M, col, TEX_ASPHALT);
        };

    placeTrash(glm::vec3(-1.35f, -3.2f, 0.0f), 0.6f, glm::vec3(0.95f));
    placeTrash(glm::vec3(1.25f, 1.3f, 0.0f), 2.9f, glm::vec3(0.95f));

    auto placeManhole = [&](glm::vec3 pos, float rotZ)
        {
            if (manholeMesh.positions.empty()) return;
            glm::mat4 M =
                glm::translate(glm::mat4(1.0f), pos) *
                glm::rotate(glm::mat4(1.0f), rotZ, glm::vec3(0, 0, 1)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
            appendObjMesh(manholeMesh,
                glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0.002f)) * M,
                tint, TEX_ASPHALT, true);
        };

    placeManhole(glm::vec3(0.2f, -2.6f, 0.0f), 0.4f);
    placeManhole(glm::vec3(-0.6f, 0.2f, 0.0f), 1.0f);

    // ------------------------------------------------------------
    // ADD ALL OTHER GEOMETRY THAT SHOULD CAST SHADOWS (including cables)
    // ------------------------------------------------------------
    {
        glm::vec3 pipeCol(0.82f, 0.88f, 0.95f);

        float rThin = 0.03f;
        float rMed = 0.05f;

        appendThinPipePrism(-halfW, -3.8f, 0.0f, 3.7f, rThin, true, pipeCol, TEX_WALL);
        appendThinPipePrism(-halfW, -0.5f, 0.0f, 4.0f, rThin, true, pipeCol, TEX_WALL);
        appendThinPipePrism(-halfW, 2.2f, 0.2f, 3.2f, rThin, true, pipeCol, TEX_WALL);

        appendThinPipePrism(halfW, -2.4f, 0.0f, 3.6f, rThin, false, pipeCol, TEX_WALL);
        appendThinPipePrism(halfW, 0.8f, 0.0f, 4.1f, rThin, false, pipeCol, TEX_WALL);

        glm::vec3 boxCol(0.65f, 0.7f, 0.75f);
        for (int i = 0; i < 6; i++) {
            appendWallBox(-halfW, -1.5f + i * 0.35f, 3.2f, 0.04f, 0.12f, 0.04f, true, boxCol, TEX_WALL);
        }
        appendWallBox(-halfW, 0.7f, 3.2f, 0.06f, 0.10f, 0.06f, true, boxCol, TEX_WALL);

        appendThinPipePrism(halfW, -0.8f, 0.3f, 3.9f, rMed, false, pipeCol, TEX_WALL);
    }

    {
        glm::vec3 boxCol(0.40f, 0.42f, 0.45f);
        appendWallBox(-halfW, -3.0f, 2.8f, 0.10f, 0.18f, 0.16f, true, boxCol, TEX_WALL);
        appendWallBox(-halfW, 1.1f, 2.2f, 0.09f, 0.15f, 0.14f, true, boxCol, TEX_WALL);

        appendWallBox(halfW, -1.7f, 2.6f, 0.10f, 0.16f, 0.16f, false, boxCol, TEX_WALL);
        appendWallBox(halfW, 2.0f, 2.9f, 0.08f, 0.14f, 0.12f, false, boxCol, TEX_WALL);
    }

    {
        glm::vec3 ventCol(0.55f, 0.55f, 0.58f);
        appendWallVent(-halfW, -0.2f, 1.1f, 0.9f, 0.45f, true, ventCol, TEX_WALL);
        appendWallVent(halfW, 1.5f, 1.4f, 0.7f, 0.35f, false, ventCol, TEX_WALL);
    }

    {
        glm::vec3 ladderCol(0.35f, 0.37f, 0.40f);
        appendWallLadder(-halfW, 3.4f, 0.4f, 3.3f, 0.55f, true, ladderCol, TEX_WALL);
    }

    {
        glm::vec3 metalCol(0.30f, 0.32f, 0.35f);
        appendWallBox(halfW, 0.25f, 1.75f, 0.05f, 0.08f, 0.03f, false, metalCol, TEX_WALL);
        appendWallBox(halfW, 0.75f, 1.75f, 0.05f, 0.08f, 0.03f, false, metalCol, TEX_WALL);
        appendWallBox(halfW, 0.50f, 1.65f, 0.05f, 0.18f, 0.03f, false, metalCol, TEX_WALL);
    }

    {
        glm::vec3 cableCol(0.22f, 0.22f, 0.25f);
        float xL = -halfW + 0.08f;
        float xR = halfW - 0.08f;

        float hw = 0.022f;

        appendCable(glm::vec3(xL, -1.8f, 3.3f), glm::vec3(xR, -1.2f, 3.1f), 0.55f, 18, hw, cableCol, TEX_WALL);
        appendCable(glm::vec3(xL, 0.4f, 3.8f), glm::vec3(xR, 0.9f, 3.7f), 0.45f, 18, hw, cableCol, TEX_WALL);
        appendCable(glm::vec3(xL, 2.6f, 3.0f), glm::vec3(xR, 2.2f, 3.2f), 0.40f, 16, hw, cableCol, TEX_WALL);

        appendCable(glm::vec3(xL, -3.2f, 3.9f), glm::vec3(xR, -2.8f, 3.8f), 0.35f, 16, hw, cableCol, TEX_WALL);
        appendCable(glm::vec3(xL, 1.8f, 3.95f), glm::vec3(xR, 1.5f, 3.9f), 0.30f, 14, hw, cableCol, TEX_WALL);

        appendCable(glm::vec3(xL, -3.0f, 2.8f), glm::vec3(xL + 0.4f, -3.2f, 0.6f), 0.25f, 12, hw * 0.9f, cableCol, TEX_WALL);
        appendCable(glm::vec3(xR, -1.7f, 2.6f), glm::vec3(xR - 0.35f, -1.9f, 0.7f), 0.25f, 12, hw * 0.9f, cableCol, TEX_WALL);
    }

    // ------------------------------------------------------------
    // NOW CLOSE SHADOW CASTERS RANGE (everything so far casts shadows)
    // ------------------------------------------------------------
    shadowCastersCount = (GLsizei)gVertices.size() - castersFirst;

    // ------------------------------------------------------------
    // Steam (excluded from shadow casters)
    // ------------------------------------------------------------
    steamFirst = (GLint)gVertices.size();
    appendSteamPuff(glm::vec3(0.2f, -2.6f, 0.03f), 2.2f, 0.28f, TEX_STEAM, 1.0f);
    appendSteamPuff(glm::vec3(-0.6f, 0.2f, 0.03f), 1.8f, 0.34f, TEX_STEAM, 0.9f);
    steamCount = (GLsizei)gVertices.size() - steamFirst;

    castersCount = (GLsizei)gVertices.size() - castersFirst;
}

// ---------------- VBO/VAO ----------------
static void CreateSceneVBO()
{
    BuildAlley();

    glGenVertexArrays(1, &SceneVaoId);
    glBindVertexArray(SceneVaoId);

    glGenBuffers(1, &SceneVboId);
    glBindBuffer(GL_ARRAY_BUFFER, SceneVboId);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(gVertices.size() * sizeof(Vtx)), gVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, col));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, nrm));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, uv));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, texId));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, tan));

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (GLvoid*)offsetof(Vtx, bit));

    glBindVertexArray(0);
}

static void DestroyScene()
{
    if (SceneVboId) glDeleteBuffers(1, &SceneVboId);
    if (SceneVaoId) glDeleteVertexArrays(1, &SceneVaoId);
    SceneVboId = 0; SceneVaoId = 0;
}

// ---------------- Shadow map init ----------------
static void CreateShadowMaps()
{
    for (int i = 0; i < LIGHT_COUNT; i++) {
        glGenFramebuffers(1, &ShadowFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, ShadowFBO[i]);

        glGenTextures(1, &ShadowDepthTex[i]);
        glBindTexture(GL_TEXTURE_2D, ShadowDepthTex[i]);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

        // filtering (PCF is in shader; keep this stable)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // outside -> lit
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderCol[4] = { 1.f, 1.f, 1.f, 1.f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderCol);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ShadowDepthTex[i], 0);

        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            printf("ERROR: ShadowFBO[%d] incomplete, status=0x%x\n", i, status);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void DestroyShadowMaps()
{
    for (int i = 0; i < LIGHT_COUNT; i++) {
        if (ShadowDepthTex[i]) glDeleteTextures(1, &ShadowDepthTex[i]);
        if (ShadowFBO[i]) glDeleteFramebuffers(1, &ShadowFBO[i]);
        ShadowDepthTex[i] = 0;
        ShadowFBO[i] = 0;
    }
}

// ---------------- Shaders ----------------
static void CreateShaders()
{
    // main shader
    ProgramId = LoadShaders("alley.vert", "alley.frag");
    glUseProgram(ProgramId);

    myMatrixLocation = glGetUniformLocation(ProgramId, "myMatrix");
    matrUmbraLocation = glGetUniformLocation(ProgramId, "matrUmbra");
    viewLocation = glGetUniformLocation(ProgramId, "view");
    projLocation = glGetUniformLocation(ProgramId, "projection");

    lightPosLocation = glGetUniformLocation(ProgramId, "lightPos");
    lightColorLocation = glGetUniformLocation(ProgramId, "lightColor");
    viewPosLocation = glGetUniformLocation(ProgramId, "viewPos");
    codColLocation = glGetUniformLocation(ProgramId, "codCol");

    texAsphaltLoc = glGetUniformLocation(ProgramId, "texAsphalt");
    texWallLoc = glGetUniformLocation(ProgramId, "texWall");
    texSignLoc = glGetUniformLocation(ProgramId, "texSign");
    texAsphaltNLoc = glGetUniformLocation(ProgramId, "texAsphaltN");
    texWallNLoc = glGetUniformLocation(ProgramId, "texWallN");

    useTexLocation = glGetUniformLocation(ProgramId, "useTextures");
    useNormalMapLocation = glGetUniformLocation(ProgramId, "useNormalMap");
    texTilingLocation = glGetUniformLocation(ProgramId, "texTiling");

    signBlackKeyLocation = glGetUniformLocation(ProgramId, "signBlackKey");

    exposureLocation = glGetUniformLocation(ProgramId, "exposure");
    gammaLocation = glGetUniformLocation(ProgramId, "gammaValue");
    timeSecLocation = glGetUniformLocation(ProgramId, "timeSec");

    useFogLocation = glGetUniformLocation(ProgramId, "useFog");

    // shadow mapping uniforms (main) - arrays
    lightSpaceLocation_Main = glGetUniformLocation(ProgramId, "lightSpace");
    shadowMapLocation_Main = glGetUniformLocation(ProgramId, "shadowMap");
    useShadowMapLocation = glGetUniformLocation(ProgramId, "useShadowMap");

    // depth-only shader
    ShadowProgramId = LoadShaders("shadow_depth.vert", "shadow_depth.frag");
    glUseProgram(ShadowProgramId);
    myMatrixLocation_Shadow = glGetUniformLocation(ShadowProgramId, "myMatrix");
    lightSpaceLocation_Shadow = glGetUniformLocation(ShadowProgramId, "lampLightSpace");

    glUseProgram(0);
}

static void DestroyShaders()
{
    if (ProgramId) glDeleteProgram(ProgramId);
    if (ShadowProgramId) glDeleteProgram(ShadowProgramId);
    ProgramId = 0;
    ShadowProgramId = 0;
}

// ---------------- Camera + lighting ----------------
static void UpdateCameraMatrices()
{
    glUseProgram(ProgramId);

    glm::vec3 ref(refX, refY, refZ);

    glm::vec3 baseOffset(0.0f, -dist, 0.0f);
    glm::vec3 offset = camRot * baseOffset;
    glm::vec3 obs = ref + offset;

    obsX = obs.x; obsY = obs.y; obsZ = obs.z;

    glm::vec3 up(0, 0, 1);

    view = glm::lookAt(obs, ref, up);
    glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));

    projection = glm::perspective(fov, width / height, dNear, 100.0f);
    glUniformMatrix4fv(projLocation, 1, GL_FALSE, glm::value_ptr(projection));

    glUniform3f(viewPosLocation, obsX, obsY, obsZ);

    glUniform3fv(lightPosLocation, LIGHT_COUNT, glm::value_ptr(lightPos[0]));
    glUniform3fv(lightColorLocation, LIGHT_COUNT, glm::value_ptr(lightColor[0]));
}

// legacy planar shadow matrix (kept)
static void UpdatePlanarShadowMatrix(float D)
{
    glUseProgram(ProgramId);

    float xL = lightPos[0].x;
    float yL = lightPos[0].y;
    float zL = lightPos[0].z;

    matrUmbra[0][0] = zL + D;  matrUmbra[0][1] = 0;       matrUmbra[0][2] = 0;       matrUmbra[0][3] = 0;
    matrUmbra[1][0] = 0;       matrUmbra[1][1] = zL + D;  matrUmbra[1][2] = 0;       matrUmbra[1][3] = 0;
    matrUmbra[2][0] = -xL;     matrUmbra[2][1] = -yL;     matrUmbra[2][2] = D;       matrUmbra[2][3] = -1;
    matrUmbra[3][0] = -D * xL; matrUmbra[3][1] = -D * yL; matrUmbra[3][2] = -D * zL; matrUmbra[3][3] = zL;

    glUniformMatrix4fv(matrUmbraLocation, 1, GL_FALSE, &matrUmbra[0][0]);
}

// compute light-space matrix for shadow map (2D) for a given light index
static glm::mat4 ComputeLightSpace(int li)
{
    // Stable "directional-ish" shadow: look from light position to a fixed target
    glm::vec3 target(0.0f, 0.0f, 1.6f);

    glm::vec3 up(0, 0, 1);
    glm::vec3 L = lightPos[li];

    if (fabs(glm::dot(glm::normalize(target - L), up)) > 0.98f) {
        up = glm::vec3(0, 1, 0);
    }

    glm::mat4 lightView = glm::lookAt(L, target, up);

    // orthographic volume (tune if needed)
    float orthoHalfX = 4.0f;
    float orthoHalfY = 7.0f;
    float nearZ = 0.1f;
    float farZ = 25.0f;

    glm::mat4 lightProj = glm::ortho(-orthoHalfX, orthoHalfX, -orthoHalfY, orthoHalfY, nearZ, farZ);
    return lightProj * lightView;
}

// ---------------- Init / Render ----------------
void Initialize()
{
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    CreateShaders();
    CreateShadowMaps();
    CreateSceneVBO();

    texAsphalt = LoadTexture2D_SOIL("asphalt.jpg");
    texWall = LoadTexture2D_SOIL("wall.jpg");
    texSign = LoadTexture2D_SOIL("sign3.png");

    texAsphaltN = LoadTexture2D_SOIL("asphalt_n.jpg");
    texWallN = LoadTexture2D_SOIL("wall_n.jpg");

    glUseProgram(ProgramId);

    glUniform1i(texAsphaltLoc, 0);
    glUniform1i(texWallLoc, 1);
    glUniform1i(texSignLoc, 2);
    glUniform1i(texAsphaltNLoc, 3);
    glUniform1i(texWallNLoc, 4);

    // shadow map sampler array -> texture units 5,6,7
    GLint shadowUnits[LIGHT_COUNT] = {
        SHADOW_TEX_UNIT_BASE + 0,
        SHADOW_TEX_UNIT_BASE + 1,
        SHADOW_TEX_UNIT_BASE + 2
    };
    glUniform1iv(shadowMapLocation_Main, LIGHT_COUNT, shadowUnits);

    glUniform1i(useTexLocation, 1);
    glUniform1i(useNormalMapLocation, 1);

    glUniform3f(texTilingLocation, 1.0f, 1.0f, 1.0f);
    glUniform1i(signBlackKeyLocation, 0);

    glUniform1f(exposureLocation, 1.15f);
    glUniform1f(gammaLocation, 2.2f);
    glUniform1f(timeSecLocation, 0.0f);

    glUniform1i(useFogLocation, gUseFog);
    glUniform1i(useShadowMapLocation, gUseShadowMap);

    glUseProgram(0);
}

static void RenderShadowPass(const glm::mat4& model, const glm::mat4& lightSpace, int li)
{
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glBindFramebuffer(GL_FRAMEBUFFER, ShadowFBO[li]);
    glClear(GL_DEPTH_BUFFER_BIT);

    // reduce peter-panning via polygon offset in shadow pass
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    glUseProgram(ShadowProgramId);
    glUniformMatrix4fv(myMatrixLocation_Shadow, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(lightSpaceLocation_Shadow, 1, GL_FALSE, glm::value_ptr(lightSpace));

    glBindVertexArray(SceneVaoId);

    // Draw ONLY shadow casters (exclude steam)
    glDrawArrays(GL_TRIANGLES, castersFirst, shadowCastersCount);

    glBindVertexArray(0);
    glUseProgram(0);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderFunction()
{
    // time
    float t = 0.001f * (float)glutGet(GLUT_ELAPSED_TIME);

    // model
    glm::mat4 model(1.0f);

    // compute light-space matrices
    glm::mat4 lightSpace[LIGHT_COUNT];
    for (int i = 0; i < LIGHT_COUNT; i++) {
        lightSpace[i] = ComputeLightSpace(i);
    }

    // 1) Shadow passes (depth only)
    if (gUseShadowMap) {
        for (int i = 0; i < LIGHT_COUNT; i++) {
            RenderShadowPass(model, lightSpace[i], i);
        }
    }

    // 2) Main pass
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(ProgramId);

    glUniform1f(timeSecLocation, t);

    UpdateCameraMatrices();

    // bind textures
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texAsphalt);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texWall);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texSign);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, texAsphaltN);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, texWallN);

    // bind shadow maps to units 5,6,7
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, ShadowDepthTex[0]);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, ShadowDepthTex[1]);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, ShadowDepthTex[2]);

    glUniformMatrix4fv(myMatrixLocation, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(lightSpaceLocation_Main, LIGHT_COUNT, GL_FALSE, glm::value_ptr(lightSpace[0]));
    glUniform1i(useShadowMapLocation, gUseShadowMap);

    // normal render (codCol = 0)
    codCol = 0;
    glUniform1i(codColLocation, codCol);

    glBindVertexArray(SceneVaoId);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)gVertices.size());
    glBindVertexArray(0);

    glutSwapBuffers();
    glFlush();
}

void Cleanup()
{
    if (texAsphalt) glDeleteTextures(1, &texAsphalt);
    if (texWall) glDeleteTextures(1, &texWall);
    if (texSign) glDeleteTextures(1, &texSign);
    if (texAsphaltN) glDeleteTextures(1, &texAsphaltN);
    if (texWallN) glDeleteTextures(1, &texWallN);

    DestroyShadowMaps();
    DestroyShaders();
    DestroyScene();
}

int main(int argc, char* argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize((int)width, (int)height);
    glutCreateWindow("Cyberpunk Alley");
    glewInit();

    Initialize();

    glutIdleFunc(RenderFunction);
    glutDisplayFunc(RenderFunction);
    glutKeyboardFunc(processNormalKeys);
    glutSpecialFunc(processSpecialKeys);
    glutCloseFunc(Cleanup);

    glutMainLoop();
    return 0;

}
