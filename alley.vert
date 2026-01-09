#version 330 core

layout(location=0) in vec4 in_Position;
layout(location=1) in vec3 in_Color;
layout(location=2) in vec3 in_Normal;
layout(location=3) in vec2 in_TexCoord;
layout(location=4) in float in_TexId;
layout(location=5) in vec3 in_Tangent;
layout(location=6) in vec3 in_Bitangent;

uniform mat4 matrUmbra;
uniform mat4 myMatrix;
uniform mat4 view;
uniform mat4 projection;
uniform int codCol;

// shadow mapping: 3 lights
uniform mat4 lightSpace[3];

out vec3 vColor;
out vec3 vFragPos;
out vec2 vUV;
flat out int vTexId;

// TBN in world space
out mat3 vTBN;

// positions in each light clip space
out vec4 vLightPosLS[3];

void main()
{
    vec4 worldPos = myMatrix * in_Position;
    vFragPos = worldPos.xyz;

    mat3 normalMat = transpose(inverse(mat3(myMatrix)));

    vec3 N = normalize(normalMat * in_Normal);
    vec3 T = normalize(normalMat * in_Tangent);
    vec3 B = normalize(normalMat * in_Bitangent);

    // re-orthonormalize
    T = normalize(T - dot(T, N) * N);
    B = normalize(cross(N, T));

    vTBN = mat3(T, B, N);

    vColor = in_Color;
    vUV = in_TexCoord;
    vTexId = int(in_TexId + 0.5);

    // compute clip-space positions for each light
    vLightPosLS[0] = lightSpace[0] * worldPos;
    vLightPosLS[1] = lightSpace[1] * worldPos;
    vLightPosLS[2] = lightSpace[2] * worldPos;

    if (codCol == 0)
        gl_Position = projection * view * worldPos;
    else
        gl_Position = projection * view * matrUmbra * worldPos;
}