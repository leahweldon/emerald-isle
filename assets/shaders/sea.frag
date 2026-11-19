#version 330 core

in vec3 vRelPos;   // relative to the camera
in vec2 vWorldXZ;
in vec2 vWaveXZ;
in vec3 vWaveNormal;
out vec4 fragColour;

uniform float uCameraHeight;
uniform vec3 uLightDir;   // towards the sun, or the moon at night
uniform vec3 uLightColour;
uniform samplerCube uSkyCube;
uniform vec3 uIrradianceSH[9];
uniform vec3 uAmbientTint; // scale on sky light, cooler at golden hour
uniform float uNight; // 0 by day, 1 at full night
uniform float uFogDensity;
uniform float uFogFalloff;
uniform sampler2D uCoastMask;  // blurred land mask, see TerrainMesh::bindCoastMask
uniform vec2 uTerrainSize;     // heightmap extent in XZ, metres
uniform float uSeaRadius;      // nearest edge of the camera-centred plane, metres
uniform sampler2DArray uRipples; // two tileable ripple normal maps (see SeaSurface)
uniform vec2 uRippleTile;        // metres per repeat of each map
uniform vec2 uRippleOffset[2];   // wind drift, wrapped to each tile
uniform float uRippleStrength;   // with the wind speed

#include "shadow.glsl"

// Linear space. Saturated so distant water stays blue-green through the haze.
const vec3 kSea     = vec3(0.004, 0.055, 0.085);
const vec3 kShallow = vec3(0.020, 0.160, 0.150); // matches terrain.frag
const float kWaterF0 = 0.02;      // reflectance looking straight down (index of refraction 1.33)
const float kGlintRoughness = 0.04; // GGX alpha of a single ripple facet: a tight sun glint

// Lighting is linear; the default framebuffer expects sRGB.
// Display transform shared by every pass that writes to the screen: linear below 0.8, then a
// smooth shoulder to 1 so the bright sky around the sun compresses instead of clipping.
vec3 toneMap(vec3 c)
{
    const float knee = 0.8;
    vec3 over = max(c - knee, 0.0);
    return min(c, vec3(knee)) + (1.0 - knee) * (1.0 - exp(-over / (1.0 - knee)));
}

vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
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

// Ripple normal from both maps, whiteout-blended onto the wave normal. Far away the mipmaps
// average the ripples out; the shortening of the averaged normal measures how much slope was
// lost (Toksvig), returned as extra roughness so the glint spreads into a glitter path instead.
vec3 rippleNormal(vec3 waveNormal, out float slopeVariance)
{
    vec3 a = texture(uRipples, vec3((vWaveXZ - uRippleOffset[0]) / uRippleTile.x, 0.0)).rgb * 2.0 - 1.0;
    vec3 b = texture(uRipples, vec3((vWaveXZ - uRippleOffset[1]) / uRippleTile.y, 1.0)).rgb * 2.0 - 1.0;
    float la = max(length(a), 1e-3);
    float lb = max(length(b), 1e-3);
    slopeVariance = uRippleStrength * ((1.0 - la) / la + (1.0 - lb) / lb);
    // Texture X is world +X, texture Y is world +Z, texture Z is up.
    vec2 slope = uRippleStrength * (a.xy / max(a.z, 0.1) + b.xy / max(b.z, 0.1));
    return normalize(vec3(waveNormal.x + slope.x * waveNormal.y, waveNormal.y,
                          waveNormal.z + slope.y * waveNormal.y));
}

float schlick(float cosine)
{
    return kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);
}

// GGX specular for one light, including n.l: distribution, Schlick Fresnel and Smith visibility.
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
    return d * schlick(dot(h, v)) * vis * 0.25 * nl;
}

void main()
{
    vec3 v = normalize(-vRelPos);
    float slopeVariance;
    vec3 n = rippleNormal(normalize(vWaveNormal), slopeVariance);
    // Seen edge-on, a ripple can face away from the camera; keep the shading normal visible.
    n = normalize(n + vec3(0.0, max(0.05 - dot(n, v), 0.0), 0.0));

    // Shallower-looking water near land: distance to the coast stands in for depth offshore.
    float coast = texture(uCoastMask, vWorldXZ / uTerrainSize + 0.5).r;
    vec3 water = mix(kSea, kShallow, smoothstep(0.02, 0.5, coast));

    // Terrain shadows on the water (headlands over bays) dim the direct light and its glint.
    float diffuse = max(dot(n, uLightDir), 0.0);
    float visibility = uLightDir.y > 0.0 ? shadowVisibility(vRelPos, vec3(0.0, 1.0, 0.0), uLightDir.y, 1.0) : 0.0;
    vec3 body = water * (skyAmbient(n) + uLightColour * diffuse * visibility);

    // Mirror reflection of the sky, weighted by Fresnel: mostly water colour looking down, mostly
    // sky at grazing angles, so the far sea runs into the horizon.
    vec3 r = reflect(-v, n);
    r.y = abs(r.y); // a ripple tilted past the view reflects the sky on its other side
    vec3 reflection = texture(uSkyCube, r).rgb;
    float fresnel = schlick(dot(n, v));
    float alpha = sqrt(kGlintRoughness * kGlintRoughness + slopeVariance);
    vec3 glint = uLightColour * visibility * ggxSpecular(n, v, uLightDir, alpha);
    vec3 colour = nightVision(mix(body, reflection, fresnel) + glint);

    // Same fog as terrain.frag.
    // Ramp fog to exactly 1 at the plane's edge, so the sea reaches the sky's horizon colour
    // whatever the camera height. Linear in 1/distance, which is roughly linear on screen near
    // the horizon, so the fade spreads over many rows instead of a few.
    float d = max(length(vRelPos.xz), 1.0);
    float nearEdge = 0.1 * uSeaRadius;
    float edge = smoothstep(0.0, 1.0, clamp((1.0 / nearEdge - 1.0 / d) /
                                            (1.0 / nearEdge - 1.0 / uSeaRadius), 0.0, 1.0));
    colour = mix(colour, fogColour(vRelPos), max(fogAmount(vRelPos), edge));
    fragColour = vec4(linearToSrgb(toneMap(colour)), 1.0);
}
