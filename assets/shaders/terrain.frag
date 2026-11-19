#version 330 core

in vec3 vRelPos;   // relative to the camera
in vec2 vWorldXZ;  // world XZ wrapped per node to within kMaterialPeriod
in float vWorldY;
in vec2 vTileUV;
in float vMorph;
in vec4 vNoise; // broad, local, patches, macro: low-frequency material noise from terrain.vert
out vec4 fragColour;

// World position for material lookups, set at the top of main().
vec3 gWorldPos;

uniform float uCameraHeight;
uniform vec3 uLightDir;   // towards the sun, or the moon at night
uniform vec3 uLightColour;
uniform samplerCube uSkyCube;
uniform vec3 uIrradianceSH[9];
uniform vec3 uAmbientTint; // scale on sky light, cooler at golden hour
uniform float uNight; // 0 by day, 1 at full night
uniform float uRain;  // 0 dry, 1 heavy rain: cloud softens what direct light is left
uniform float uWetness; // 0 dry, 1 soaked
uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uExaggeration;

uniform sampler2D uHeightTile; // streamed node tiles, see Terrain
uniform float uTileSamples;
uniform float uTileQuads;
uniform float uNodeSize;

uniform sampler2DArray uMaterialColour;
uniform sampler2DArray uMaterialNormal;

#include "shadow.glsl"

uniform sampler2DArray uHorizon; // the node's horizon map: 8 directions in two RGBA layers
uniform float uTileBorder;

// Soft edge for distant shadows, also hiding the 8-bit angle steps: about a degree.
const float kHorizonSoftness = 0.018;

// Direct-light visibility beyond the cascades: is the light above the precomputed horizon in its
// direction? Horizon angles are interpolated between the two nearest of the eight directions.
float horizonVisibility()
{
    vec2 grid = vTileUV * uTileSamples - uTileBorder - 0.5;
    vec2 uv = (grid * 0.5 + 0.5) / (0.5 * uTileQuads + 1.0);
    vec4 northToEast = texture(uHorizon, vec3(uv, 0.0));
    vec4 southToWest = texture(uHorizon, vec3(uv, 1.0));
    float angles[8] = float[8](northToEast.x, northToEast.y, northToEast.z, northToEast.w,
                               southToWest.x, southToWest.y, southToWest.z, southToWest.w);
    float bearing = atan(uLightDir.x, -uLightDir.z); // 0 = north, clockwise
    float sector = mod(bearing / (3.14159265 / 4.0), 8.0);
    int i0 = int(sector) % 8;
    float horizon = mix(angles[i0], angles[(i0 + 1) % 8], fract(sector)) * (3.14159265 / 2.0);
    float elevation = asin(clamp(uLightDir.y, -1.0, 1.0));
    return smoothstep(horizon - kHorizonSoftness, horizon + kHorizonSoftness, elevation);
}

// Layer order matches kMaterials in Application.cpp.
const float kGrass = 0.0;
const float kSand  = 1.0;
const float kHeath = 2.0;
const float kRock  = 3.0;

// Every material pattern repeats exactly every 360 km (texture tiles divide it; the noise in
// terrain.vert does too), so world coordinates can be wrapped per node and stay precise anywhere.

// Texture repeat sizes in metres. Two non-multiple scales hide tiling on flat materials.
const float kTileNear = 2000.0;
const float kTileFar  = 9000.0;
const float kTileRock = 3000.0;

// Texture detail fades to each material's average colour over this range, to stop distant shimmer.
const float kDetailFadeStart = 50000.0;
const float kDetailFadeEnd   = 150000.0;

// Linear-space multipliers on the decoded albedo.
const vec3 kHeathTint = vec3(0.22, 0.18, 0.11); // darkens Ground037 towards peat and heather
const vec3 kGrassTint = vec3(0.70, 1.00, 0.61); // lusher green
const vec3 kRockTint  = vec3(1.80, 1.35, 0.95); // Rock058 is blue-grey; shift to warm grey-brown
const vec3 kShallow   = vec3(0.020, 0.160, 0.150); // linear, matches sea.frag

// Wet ground: the fraction of albedo lost as water fills the pores (sand and bare rock darken
// most, grass least), how much of a water film each surface holds (blades of grass and heather
// break it up), and the film's GGX roughness from damp slopes to flat, puddled ground.
const vec4 kWetDarkening = vec4(0.25, 0.45, 0.30, 0.40); // grass, sand, heath, rock
const vec4 kWetFilm = vec4(0.15, 0.8, 0.25, 0.6);
const float kWetRoughSlope = 0.5;
const float kWetRoughFlat = 0.2;

vec3 unpackNormal(vec3 rgb)
{
    return rgb * 2.0 - 1.0;
}

// Sampling uses textureGrad with derivatives taken once in main(), so materials can be skipped
// in non-uniform branches without breaking mip selection.

// Top-down projection: colour and whiteout-blended normal at one tile size.
void sampleTop(float layer, float tile, vec3 n, vec3 dPdx, vec3 dPdy, out vec3 colour,
               out vec3 normal)
{
    vec3 uv = vec3(gWorldPos.xz / tile, layer);
    vec2 gx = dPdx.xz / tile;
    vec2 gy = dPdy.xz / tile;
    colour = textureGrad(uMaterialColour, uv, gx, gy).rgb;
    vec3 t = unpackNormal(textureGrad(uMaterialNormal, uv, gx, gy).rgb);
    normal = vec3(t.x + n.x, abs(t.z) * n.y, t.y + n.z);
}

// Flat materials: average of two tile scales.
void sampleFlat(float layer, vec3 n, vec3 dPdx, vec3 dPdy, out vec3 colour, out vec3 normal)
{
    vec3 c0, n0, c1, n1;
    sampleTop(layer, kTileNear, n, dPdx, dPdy, c0, n0);
    sampleTop(layer, kTileFar, n, dPdx, dPdy, c1, n1);
    colour = 0.5 * (c0 + c1);
    normal = normalize(n0 + n1);
}

// Triplanar projection with whiteout normal blending, for steep faces.
void sampleTriplanar(float layer, vec3 n, vec3 dPdx, vec3 dPdy, out vec3 colour, out vec3 normal)
{
    vec3 w = pow(abs(n), vec3(4.0));
    w /= (w.x + w.y + w.z);
    vec3 s = sign(n);

    vec3 uvX = vec3(gWorldPos.zy / kTileRock * vec2(s.x, 1.0), layer);
    vec3 uvY = vec3(gWorldPos.xz / kTileRock * vec2(s.y, 1.0), layer);
    vec3 uvZ = vec3(gWorldPos.xy / kTileRock * vec2(-s.z, 1.0), layer);
    vec2 gxX = dPdx.zy / kTileRock * vec2(s.x, 1.0);
    vec2 gyX = dPdy.zy / kTileRock * vec2(s.x, 1.0);
    vec2 gxY = dPdx.xz / kTileRock * vec2(s.y, 1.0);
    vec2 gyY = dPdy.xz / kTileRock * vec2(s.y, 1.0);
    vec2 gxZ = dPdx.xy / kTileRock * vec2(-s.z, 1.0);
    vec2 gyZ = dPdy.xy / kTileRock * vec2(-s.z, 1.0);

    vec3 tX = unpackNormal(textureGrad(uMaterialNormal, uvX, gxX, gyX).rgb);
    vec3 tY = unpackNormal(textureGrad(uMaterialNormal, uvY, gxY, gyY).rgb);
    vec3 tZ = unpackNormal(textureGrad(uMaterialNormal, uvZ, gxZ, gyZ).rgb);
    tX.x *= s.x;
    tY.x *= s.y;
    tZ.x *= -s.z;
    tX = vec3(tX.xy + n.zy, abs(tX.z) * n.x);
    tY = vec3(tY.xy + n.xz, abs(tY.z) * n.y);
    tZ = vec3(tZ.xy + n.xy, abs(tZ.z) * n.z);
    normal = normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z);

    colour = textureGrad(uMaterialColour, uvX, gxX, gyX).rgb * w.x +
             textureGrad(uMaterialColour, uvY, gxY, gyY).rgb * w.y +
             textureGrad(uMaterialColour, uvZ, gxZ, gyZ).rgb * w.z;
}

float tileHeight(vec2 uv)
{
    return textureLod(uHeightTile, uv, 0.0).r;
}

// Per-pixel normal from the node's tile (heights already exaggerated). Blends from one-texel to
// two-texel differences as the vertices morph, so lighting matches the coarser neighbour.
vec3 terrainNormal()
{
    float texel = 1.0 / uTileSamples;
    float spacing = uNodeSize / uTileQuads;
    vec3 fine = vec3(tileHeight(vTileUV - vec2(texel, 0.0)) - tileHeight(vTileUV + vec2(texel, 0.0)),
                     2.0 * spacing,
                     tileHeight(vTileUV - vec2(0.0, texel)) - tileHeight(vTileUV + vec2(0.0, texel)));
    if (vMorph <= 0.0) {
        return normalize(fine); // most fragments: no morphing, skip the coarse lookups
    }
    vec3 coarse = vec3(tileHeight(vTileUV - vec2(2.0 * texel, 0.0)) - tileHeight(vTileUV + vec2(2.0 * texel, 0.0)),
                       4.0 * spacing,
                       tileHeight(vTileUV - vec2(0.0, 2.0 * texel)) - tileHeight(vTileUV + vec2(0.0, 2.0 * texel)));
    return normalize(mix(normalize(fine), normalize(coarse), vMorph));
}

// Average colour of a layer: its 1x1 mip.
vec3 materialAverage(float layer)
{
    return textureLod(uMaterialColour, vec3(0.5, 0.5, layer), 16.0).rgb;
}

// Diffuse sky light from order-2 SH (coefficients pre-convolved and divided by pi on the CPU).
vec3 skyAmbient(vec3 n)
{
    return uAmbientTint * max(uIrradianceSH[0] * 0.282095
             + uIrradianceSH[1] * 0.488603 * n.y + uIrradianceSH[2] * 0.488603 * n.z
             + uIrradianceSH[3] * 0.488603 * n.x
             + uIrradianceSH[4] * 1.092548 * n.x * n.y + uIrradianceSH[5] * 1.092548 * n.y * n.z
             + uIrradianceSH[6] * 0.315392 * (3.0 * n.z * n.z - 1.0)
             + uIrradianceSH[7] * 1.092548 * n.x * n.z
             + uIrradianceSH[8] * 0.546274 * (n.x * n.x - n.y * n.y), vec3(0.0));
}

// GGX specular for one light, including n.l, with Schlick Fresnel for a water film (F0 0.02).
float ggxSpecular(vec3 n, vec3 v, vec3 l, float alpha)
{
    vec3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 1e-3);
    float nh = max(dot(n, h), 0.0);
    float a2 = alpha * alpha;
    float d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
    float k = 0.5 * alpha;
    float vis = 1.0 / ((nl * (1.0 - k) + k) * (nv * (1.0 - k) + k));
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(h, v), 0.0, 1.0), 5.0);
    return d * fresnel * vis * 0.25 * nl;
}

// Night vision: as the light fails, colours fade and shift towards blue (the Purkinje effect),
// so moonlit land reads blue-grey rather than green.
vec3 nightVision(vec3 c)
{
    float luminance = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return mix(c, luminance * vec3(0.70, 0.86, 1.25), 0.85 * uNight);
}

// Fog colour is the sky behind the fragment, so distant terrain and sea meet the skybox exactly.
vec3 fogColour(vec3 relPos)
{
    return texture(uSkyCube, normalize(relPos)).rgb;
}

// Height fog: optical depth of exponentially thinning air along the view ray from the camera.
float fogAmount(vec3 relPos)
{
    float dist = length(relPos);
    float k = uFogFalloff;
    float base = exp(-k * uCameraHeight);
    float rise = k * relPos.y;
    // Integral of exp(-k * height) over the ray, divided by its length.
    float avg = abs(rise) > 1e-4 ? (base - exp(-k * (uCameraHeight + relPos.y))) / rise : base;
    return 1.0 - exp(-uFogDensity * dist * avg);
}

void main()
{
    gWorldPos = vec3(vWorldXZ.x, vWorldY, vWorldXZ.y);
    // Taken before any branch or discard.
    vec3 dPdx = dFdx(gWorldPos);
    vec3 dPdy = dFdy(gWorldPos);

    // Below sea level is covered by the sea plane.
    if (gWorldPos.y < 0.0) {
        discard;
    }

    // Fully fogged (distant islands at the horizon): the fog colour is all that shows.
    float fog = fogAmount(vRelPos);
    if (fog > 0.995) {
        fragColour = vec4(fogColour(vRelPos), 1.0);
        return;
    }

    vec3 n = terrainNormal();
    float metres = gWorldPos.y / uExaggeration;
    float slope = 1.0 - n.y;

    // Low-frequency noise shifts every threshold so bands do not follow contours.
    float broad = vNoise.x;
    float local = vNoise.y;

    float patches = vNoise.z;

    // Beach width varies along the coast: wide in places, a thin strip or none elsewhere.
    float beach = smoothstep(0.35, 0.7, 0.6 * local + 0.4 * patches);
    float sandTop = mix(0.3, 14.0, beach);
    float sand = 1.0 - smoothstep(0.4 * sandTop, sandTop, metres);
    sand *= 1.0 - smoothstep(0.05, 0.15, slope);

    // Heath follows slope and patch noise; height nudges it, so summits get heath/grass mixes and
    // gentle lower slopes stay green.
    float heathScore = 1.8 * slope + 0.8 * (patches - 0.5) + 0.3 * (broad - 0.5) +
                       0.8 * smoothstep(150.0, 500.0, metres) - 0.04;
    float heath = smoothstep(0.35, 0.65, heathScore);

    // Rock only on steep faces.
    float rock = smoothstep(0.20, 0.34, slope + 0.06 * (local - 0.5));

    // Rock overrides everything; sand overrides grass and heath.
    float wRock = rock;
    float wSand = (1.0 - wRock) * sand;
    float wHeath = (1.0 - wRock) * (1.0 - sand) * heath;
    float wGrass = (1.0 - wRock) * (1.0 - sand) * (1.0 - heath);

    // Only sample materials that contribute; most pixels need one or two. Past the fade range
    // only the average colour is fetched.
    const float kMinWeight = 0.002;
#ifdef REFLECTION_PASS
    float detail = 0.0; // rippled and half resolution: average material colours are enough
#else
    float detail = 1.0 - smoothstep(kDetailFadeStart, kDetailFadeEnd, length(vRelPos));
#endif
    vec3 albedo = vec3(0.0);
    vec3 normalSum = vec3(0.0);
    vec3 c, nm;
    if (wGrass > kMinWeight) {
        c = materialAverage(kGrass);
        nm = n;
        if (detail > 0.0) {
            vec3 dc, dn;
            sampleFlat(kGrass, n, dPdx, dPdy, dc, dn);
            c = mix(c, dc, detail);
            nm = mix(n, dn, detail);
        }
        albedo += c * kGrassTint * wGrass;
        normalSum += nm * wGrass;
    }
    if (wSand > kMinWeight) {
        c = materialAverage(kSand);
        nm = n;
        if (detail > 0.0) {
            vec3 dc, dn;
            sampleFlat(kSand, n, dPdx, dPdy, dc, dn);
            c = mix(c, dc, detail);
            nm = mix(n, dn, detail);
        }
        albedo += c * wSand;
        normalSum += nm * wSand;
    }
    if (wHeath > kMinWeight) {
        c = materialAverage(kHeath);
        nm = n;
        if (detail > 0.0) {
            vec3 dc, dn;
            sampleFlat(kHeath, n, dPdx, dPdy, dc, dn);
            c = mix(c, dc, detail);
            nm = mix(n, dn, detail);
        }
        albedo += c * kHeathTint * wHeath;
        normalSum += nm * wHeath;
    }
    if (wRock > kMinWeight) {
        c = materialAverage(kRock);
        nm = n;
        if (detail > 0.0) {
            vec3 dc, dn;
            sampleTriplanar(kRock, n, dPdx, dPdy, dc, dn);
            c = mix(c, dc, detail);
            nm = mix(n, dn, detail);
        }
        albedo += c * kRockTint * wRock;
        normalSum += nm * wRock;
    }
    // Skipped weights are below kMinWeight; renormalise so they do not darken the result.
    albedo /= max(wGrass * step(kMinWeight, wGrass) + wSand * step(kMinWeight, wSand) +
                  wHeath * step(kMinWeight, wHeath) + wRock * step(kMinWeight, wRock), 1e-4);
    vec3 shadingNormal = normalize(normalSum + n * 1e-4);
    float weightSum = wGrass + wSand + wHeath + wRock;
    float wetDarkening = dot(vec4(wGrass, wSand, wHeath, wRock), kWetDarkening) / max(weightSum, 1e-4);
    float wetFilm = dot(vec4(wGrass, wSand, wHeath, wRock), kWetFilm) / max(weightSum, 1e-4);

    // Large-scale colour variation hides repetition across the island.
    float macro = vNoise.w;
    albedo *= mix(vec3(0.70, 0.75, 0.65), vec3(1.23, 1.14, 0.89), macro);

    albedo *= 1.0 - uWetness * wetDarkening;

    // Fade the last metres above sea level into shallow water to soften the pixel coastline.
    albedo = mix(kShallow, albedo, smoothstep(0.0, 2.5, metres));

    // Shadows dim direct light only; the sky's ambient light reaches into them.
    float diffuse = max(dot(shadingNormal, uLightDir), 0.0);
    float lit = 0.0;
    if (diffuse > 0.0) {
        float far = dot(vRelPos, uViewForward) > uCascadeStart.w ? horizonVisibility() : 1.0;
        lit = mix(shadowVisibility(vRelPos, n, max(dot(n, uLightDir), 0.0), far), 1.0, 0.5 * uRain);
        diffuse *= lit;
    }
    vec3 colour = albedo * (skyAmbient(shadingNormal) + uLightColour * diffuse);

    // A film of water on wet ground: glossier on the flat, where it pools. It reflects the sky,
    // with Fresnel capped by roughness so rough slopes never turn to mirrors at grazing angles,
    // and catches the sun (or moon) as a soft highlight.
    // The film is smoother than the material's normal map, so it mostly follows the ground.
    if (uWetness > 0.0) {
        float flat_ = smoothstep(0.85, 0.98, n.y);
        float alpha = mix(kWetRoughSlope, kWetRoughFlat, flat_);
        vec3 filmNormal = normalize(mix(n, shadingNormal, 0.3));
        vec3 v = normalize(-vRelPos);
        float nv = max(dot(filmNormal, v), 1e-3);
        float fresnel = 0.02 + (1.0 - alpha - 0.02) * pow(1.0 - nv, 5.0);
        vec3 r = reflect(-v, filmNormal);
        r.y = abs(r.y);
        vec3 sheen = fresnel * textureLod(uSkyCube, r, 0.0).rgb +
                     uLightColour * lit * ggxSpecular(filmNormal, v, uLightDir, alpha);
        colour += uWetness * wetFilm * mix(0.3, 1.0, flat_) * sheen;
    }
    colour = nightVision(colour);

    colour = mix(colour, fogColour(vRelPos), fog);
    fragColour = vec4(colour, 1.0); // linear HDR, tone mapped in tonemap.frag
}
