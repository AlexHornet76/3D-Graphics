#version 330 core

in vec3 vColor;
in vec3 vFragPos;
in vec2 vUV;
flat in int vTexId;
in mat3 vTBN;

// shadow mapping (3 lights)
in vec4 vLightPosLS[3];

out vec4 out_Color;

uniform vec3 viewPos;
uniform vec3 lightPos[3];
uniform vec3 lightColor[3];
uniform int codCol;

// albedo textures
uniform sampler2D texAsphalt;
uniform sampler2D texWall;
uniform sampler2D texSign;

// normal maps
uniform sampler2D texAsphaltN;
uniform sampler2D texWallN;

uniform int useTextures;
uniform int useNormalMap;
uniform vec3 texTiling;

// sign helper
uniform int signBlackKey;

// tone mapping + gamma
uniform float exposure;
uniform float gammaValue;

// time (seconds)
uniform float timeSec;

// fog toggle (0/1)
uniform int useFog;

// shadow maps: one per light
uniform sampler2D shadowMap[3];
uniform int useShadowMap;

// -------- robust tiny noise (no overloads) ----------
float steamHash(vec2 p)
{
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float steamNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = steamHash(i);
    float b = steamHash(i + vec2(1.0, 0.0));
    float c = steamHash(i + vec2(0.0, 1.0));
    float d = steamHash(i + vec2(1.0, 1.0));

    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// -------- color ops ----------
vec3 toneMapReinhard(vec3 c)
{
    c *= max(exposure, 0.0);
    return c / (vec3(1.0) + c);
}

vec3 applyGamma(vec3 c)
{
    float g = max(gammaValue, 1e-4);
    return pow(max(c, vec3(0.0)), vec3(1.0 / g));
}

// -------- existing surface sampling ----------
vec4 sampleSurface()
{
    if (useTextures == 0) return vec4(vColor, 1.0);

    if (vTexId == 0) {
        vec2 uv = vUV * texTiling.x;
        return vec4(texture(texAsphalt, uv).rgb * vColor, 1.0);
    }
    if (vTexId == 1) {
        vec2 uv = vUV * texTiling.y;
        return vec4(texture(texWall, uv).rgb * vColor, 1.0);
    }

    // IMPORTANT: steam is not an albedo-textured surface
    if (vTexId == 3) {
        return vec4(vColor, 1.0);
    }

    // sign
    vec2 uv = vUV * texTiling.z;
    vec4 s = texture(texSign, uv);
    s.rgb *= vColor;

    if (signBlackKey == 1) {
        float lum = dot(s.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < 0.05) s.a = 0.0;
    }
    return s;
}

vec3 sampleNormalWS()
{
    vec3 N = normalize(vTBN[2]);

    if (useNormalMap == 0) return N;
    if (vTexId == 2) return N; // sign
    if (vTexId == 3) return N; // steam

    vec2 uv = vUV * (vTexId == 0 ? texTiling.x : texTiling.y);

    vec3 nTS;
    if (vTexId == 0)
        nTS = texture(texAsphaltN, uv).xyz * 2.0 - 1.0;
    else
        nTS = texture(texWallN, uv).xyz * 2.0 - 1.0;

    return normalize(vTBN * nTS);
}

// return 0 = fully lit, 1 = fully shadowed for light index li
float shadowFactorPCF(int li, vec3 N, vec3 L)
{
    vec3 proj = vLightPosLS[li].xyz / max(vLightPosLS[li].w, 1e-6);
    proj = proj * 0.5 + 0.5;

    // outside => lit
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z < 0.0 || proj.z > 1.0)
        return 0.0;

    float bias = max(0.0015 * (1.0 - dot(N, L)), 0.0006);

    vec2 texel = 1.0 / vec2(textureSize(shadowMap[li], 0));
    float shadow = 0.0;

    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
    {
        float closest = texture(shadowMap[li], proj.xy + vec2(x, y) * texel).r;
        float current = proj.z - bias;
        shadow += (current > closest) ? 1.0 : 0.0;
    }

    shadow /= 9.0;
    return shadow;
}

void main()
{
    if (codCol == 1) {
        out_Color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ----------------------------
    // STEAM (texId == 3)  (restored)
    // ----------------------------
    if (vTexId == 3)
    {
        vec2 uv = vUV;              // 0..1
        vec2 p = uv * 2.0 - 1.0;    // -1..1

        float r2 = dot(p, p);
        float baseMask = smoothstep(1.35, 0.10, r2);
        float edgeSoft = smoothstep(1.0, 0.65, sqrt(r2));

        float t = timeSec;

        vec2 drift = vec2(0.18 * sin(t * 0.6), 1.10) * t;

        float n1 = steamNoise(uv * 5.5  + drift * vec2(0.25, 0.55));
        float n2 = steamNoise(uv * 11.0 + drift * vec2(0.35, 0.85) + vec2(2.3, 1.7));
        float n3 = steamNoise(uv * 22.0 + drift * vec2(0.55, 1.15) + vec2(5.1, 3.9));

        float turb = 0.55 * n1 + 0.30 * n2 + 0.15 * n3;

        float bottom = smoothstep(0.00, 0.12, uv.y);
        float topFade = smoothstep(1.0, 0.20, uv.y);
        float column = bottom * topFade;

        float holes = smoothstep(0.25, 0.8, turb);
        float streaks = smoothstep(0.15, 0.95, steamNoise(vec2(uv.x * 3.0 + turb, uv.y * 14.0 - t * 0.9)));

        float intensity = clamp(vColor.r, 0.0, 1.0);

        float alpha = baseMask * edgeSoft * column;
        alpha *= (0.70 + 0.50 * holes);
        alpha *= (0.75 + 0.25 * streaks);
        alpha *= (0.70 * intensity);

        alpha *= 1.0 - smoothstep(0.25, 1.0, abs(p.x));

        if (alpha < 0.012) discard;

        vec3 V = normalize(viewPos - vFragPos);

        float rim = pow(1.0 - max(dot(V, normalize(vec3(0,0,1))), 0.0), 2.0);

        vec3 lightAcc = vec3(0.0);
        for (int i = 0; i < 3; i++)
        {
            vec3 Lvec = lightPos[i] - vFragPos;
            float d = length(Lvec);
            vec3 L = Lvec / max(d, 1e-4);

            float scatter = pow(max(dot(L, -V), 0.0), 2.0);
            float att = 1.0 / (1.0 + 0.18 * d + 0.08 * d * d);

            lightAcc += lightColor[i] * scatter * att;
        }

        vec3 baseCol = vec3(0.78, 0.82, 0.88);
        vec3 col = baseCol * (0.25 + 1.35 * dot(lightAcc, vec3(0.333)));
        col += 0.65 * baseCol * lightAcc; // asta chiar injectează culoarea luminilor
        col += (0.35 * rim) * (0.6 + 1.4 * dot(lightAcc, vec3(0.333)));

        col = clamp(col, vec3(0.0), vec3(8.0));
        col = applyGamma(toneMapReinhard(col));

        out_Color = vec4(col, alpha);
        return;
    }

    // ----------------------------
    // normal surfaces
    // ----------------------------
    vec4 surf = sampleSurface();

    if (vTexId == 2 && surf.a < 0.05)
        discard;

    vec3 albedo = surf.rgb;
    float alphaOut = surf.a;

    vec3 N = sampleNormalWS();
    vec3 V = normalize(viewPos - vFragPos);

    vec3 result = 0.05 * albedo;

    float lightBoost = 2.4;
    float shininess = 64.0;
    float specStrength = 0.50;

    for (int i = 0; i < 3; i++)
    {
        vec3 Lvec = lightPos[i] - vFragPos;
        float dist = length(Lvec);
        vec3 L = normalize(Lvec);

        float attenuation = 1.0 / (1.0 + 0.10*dist + 0.06*dist*dist);

        float diff = max(dot(N, L), 0.0);
        vec3 R = reflect(-L, N);
        float spec = pow(max(dot(V, R), 0.0), shininess);

        vec3 diffuse  = diff * albedo * lightColor[i] * lightBoost;
        vec3 specular = specStrength * spec * lightColor[i] * lightBoost;

        float shadow = 0.0;
        if (useShadowMap == 1)
            shadow = shadowFactorPCF(i, N, L);

        result += attenuation * ((1.0 - shadow) * (diffuse + specular));
    }

    if (vTexId == 2) {
        result += albedo * 1.2;
    }

    if (useFog == 1) {
        float fogDensity = 0.075;
        vec3 fogColor = vec3(0.045, 0.03, 0.07);

        float dFog = length(viewPos - vFragPos);

        float fogFactor = exp(-fogDensity * dFog);
        fogFactor = clamp(fogFactor, 0.0, 1.0);

        fogFactor = pow(fogFactor, 1.35);
        fogFactor = max(fogFactor, 0.22);

        result = mix(fogColor, result, fogFactor);
    }

    result = applyGamma(toneMapReinhard(result));
    out_Color = vec4(result, alphaOut);
}