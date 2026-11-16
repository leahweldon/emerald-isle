#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColour;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;     // towards the sun, normalised
uniform vec3 uSunColour;
uniform vec3 uAmbient;
uniform vec3 uHorizonColour;
uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uExaggeration;

uniform sampler2DArray uMaterialColour;
uniform sampler2DArray uMaterialNormal;

// Layer order matches kMaterials in Application.cpp.
const float kGrass = 0.0;
const float kSand  = 1.0;
const float kHeath = 2.0;
const float kRock  = 3.0;

// Texture repeat sizes in metres. Two non-multiple scales hide tiling on flat materials.
const float kTileNear = 2000.0;
const float kTileFar  = 9000.0;
const float kTileRock = 3000.0;

const vec3 kHeathTint = vec3(0.50, 0.46, 0.36); // darkens Ground037 towards peat and heather
const vec3 kShallow   = vec3(0.12, 0.36, 0.36); // matches sea.frag
const vec3 kGrassTint = vec3(0.85, 1.00, 0.80); // lusher green

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Four octaves, roughly in [0, 1].
float fbm(vec2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += amplitude * valueNoise(p);
        p = p * 2.03 + vec2(1.7, 9.2);
        amplitude *= 0.5;
    }
    return sum / 0.9375;
}

vec3 unpackNormal(vec3 rgb)
{
    return rgb * 2.0 - 1.0;
}

// Top-down projection: colour and whiteout-blended normal at one tile size.
void sampleTop(float layer, float tile, vec3 n, out vec3 colour, out vec3 normal)
{
    vec2 uv = vWorldPos.xz / tile;
    colour = texture(uMaterialColour, vec3(uv, layer)).rgb;
    vec3 t = unpackNormal(texture(uMaterialNormal, vec3(uv, layer)).rgb);
    normal = vec3(t.x + n.x, abs(t.z) * n.y, t.y + n.z);
}

// Flat materials: average of two tile scales.
void sampleFlat(float layer, vec3 n, out vec3 colour, out vec3 normal)
{
    vec3 c0, n0, c1, n1;
    sampleTop(layer, kTileNear, n, c0, n0);
    sampleTop(layer, kTileFar, n, c1, n1);
    colour = 0.5 * (c0 + c1);
    normal = normalize(n0 + n1);
}

// Triplanar projection with whiteout normal blending, for steep faces.
void sampleTriplanar(float layer, vec3 n, out vec3 colour, out vec3 normal)
{
    vec3 w = pow(abs(n), vec3(4.0));
    w /= (w.x + w.y + w.z);
    vec3 s = sign(n);

    vec2 uvX = vWorldPos.zy / kTileRock;
    vec2 uvY = vWorldPos.xz / kTileRock;
    vec2 uvZ = vWorldPos.xy / kTileRock;
    uvX.x *= s.x;
    uvY.x *= s.y;
    uvZ.x *= -s.z;

    vec3 tX = unpackNormal(texture(uMaterialNormal, vec3(uvX, layer)).rgb);
    vec3 tY = unpackNormal(texture(uMaterialNormal, vec3(uvY, layer)).rgb);
    vec3 tZ = unpackNormal(texture(uMaterialNormal, vec3(uvZ, layer)).rgb);
    tX.x *= s.x;
    tY.x *= s.y;
    tZ.x *= -s.z;
    tX = vec3(tX.xy + n.zy, abs(tX.z) * n.x);
    tY = vec3(tY.xy + n.xz, abs(tY.z) * n.y);
    tZ = vec3(tZ.xy + n.xy, abs(tZ.z) * n.z);
    normal = normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z);

    colour = texture(uMaterialColour, vec3(uvX, layer)).rgb * w.x +
             texture(uMaterialColour, vec3(uvY, layer)).rgb * w.y +
             texture(uMaterialColour, vec3(uvZ, layer)).rgb * w.z;
}

// Height fog: optical depth of exponentially thinning air along the view ray.
float fogAmount(vec3 cameraPos, vec3 worldPos)
{
    vec3 ray = worldPos - cameraPos;
    float dist = length(ray);
    float k = uFogFalloff;
    float base = exp(-k * cameraPos.y);
    float rise = k * ray.y;
    // Integral of exp(-k * height) over the ray, divided by its length.
    float avg = abs(rise) > 1e-4 ? (base - exp(-k * worldPos.y)) / rise : base;
    return 1.0 - exp(-uFogDensity * dist * avg);
}

void main()
{
    // Below sea level is covered by the sea plane.
    if (vWorldPos.y < 0.0) {
        discard;
    }

    vec3 n = normalize(vNormal);
    float metres = vWorldPos.y / uExaggeration;
    float slope = 1.0 - n.y;

    // Low-frequency noise shifts every threshold so bands do not follow contours.
    float broad = fbm(vWorldPos.xz / 25000.0);
    float local = fbm(vWorldPos.xz / 6000.0 + 17.0);

    float sand = 1.0 - smoothstep(2.0, 6.0 + 8.0 * local, metres);
    sand *= 1.0 - smoothstep(0.05, 0.15, slope);
    float heath = smoothstep(180.0 + 120.0 * broad, 320.0 + 120.0 * broad, metres);
    float rock = smoothstep(0.16 + 0.08 * (local - 0.5), 0.30, slope);
    rock = max(rock, 0.7 * smoothstep(750.0, 900.0, metres));

    // Rock overrides everything; sand overrides grass and heath.
    float wRock = rock;
    float wSand = (1.0 - wRock) * sand;
    float wHeath = (1.0 - wRock) * (1.0 - sand) * heath;
    float wGrass = (1.0 - wRock) * (1.0 - sand) * (1.0 - heath);

    vec3 cGrass, nGrass, cSand, nSand, cHeath, nHeath, cRock, nRock;
    sampleFlat(kGrass, n, cGrass, nGrass);
    sampleFlat(kSand, n, cSand, nSand);
    sampleFlat(kHeath, n, cHeath, nHeath);
    sampleTriplanar(kRock, n, cRock, nRock);
    cHeath *= kHeathTint;
    cGrass *= kGrassTint;

    vec3 albedo = cGrass * wGrass + cSand * wSand + cHeath * wHeath + cRock * wRock;
    vec3 shadingNormal = normalize(nGrass * wGrass + nSand * wSand + nHeath * wHeath + nRock * wRock);

    // Large-scale colour variation hides repetition across the island.
    float macro = fbm(vWorldPos.xz / 40000.0 + 3.0);
    albedo *= mix(vec3(0.85, 0.88, 0.82), vec3(1.10, 1.06, 0.95), macro);

    // Fade the last metres above sea level into shallow water to soften the pixel coastline.
    albedo = mix(kShallow, albedo, smoothstep(0.0, 2.5, metres));

    float diffuse = max(dot(shadingNormal, uSunDir), 0.0);
    vec3 colour = albedo * (uAmbient + uSunColour * diffuse);

    // Fog colour is the sky's horizon colour, so distant terrain meets the sky without a seam.
    colour = mix(colour, uHorizonColour, fogAmount(uCameraPos, vWorldPos));
    fragColour = vec4(colour, 1.0);
}
