#version 330 core

in vec3 vRelPos;   // relative to the camera
in vec2 vWorldXZ;
out vec4 fragColour;

uniform float uCameraHeight;
uniform vec3 uLightDir;   // towards the sun, or the moon at night
uniform vec3 uLightColour;
uniform samplerCube uSkyCube;
uniform vec3 uIrradianceSH[9];
uniform float uAmbientScale;
uniform float uFogDensity;
uniform float uFogFalloff;
uniform sampler2D uCoastMask;  // blurred land mask, see TerrainMesh::bindCoastMask
uniform vec2 uTerrainSize;     // heightmap extent in XZ, metres
uniform float uSeaRadius;      // nearest edge of the camera-centred plane, metres

#include "shadow.glsl"

// Linear space. Saturated so distant water stays blue-green through the haze.
const vec3 kSea     = vec3(0.004, 0.055, 0.085);
const vec3 kShallow = vec3(0.020, 0.160, 0.150); // matches terrain.frag

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
    return uAmbientScale * max(uIrradianceSH[0] * 0.282095
             + uIrradianceSH[1] * 0.488603 * n.y + uIrradianceSH[2] * 0.488603 * n.z
             + uIrradianceSH[3] * 0.488603 * n.x
             + uIrradianceSH[4] * 1.092548 * n.x * n.y + uIrradianceSH[5] * 1.092548 * n.y * n.z
             + uIrradianceSH[6] * 0.315392 * (3.0 * n.z * n.z - 1.0)
             + uIrradianceSH[7] * 1.092548 * n.x * n.z
             + uIrradianceSH[8] * 0.546274 * (n.x * n.x - n.y * n.y), vec3(0.0));
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
    const vec3 n = vec3(0.0, 1.0, 0.0);
    vec3 toCamera = normalize(-vRelPos);

    // Shallower-looking water near land. No bathymetry, so distance to coast stands in for depth.
    float coast = texture(uCoastMask, vWorldXZ / uTerrainSize + 0.5).r;
    vec3 water = mix(kSea, kShallow, smoothstep(0.02, 0.5, coast));

    // Terrain shadows on the water (headlands over bays) dim the direct light and its glint.
    float diffuse = max(dot(n, uLightDir), 0.0);
    float visibility = diffuse > 0.0 ? shadowVisibility(vRelPos, n, diffuse, 1.0) : 0.0;
    diffuse *= visibility;
    // Blinn-Phong glint from the sun or moon.
    float specular = visibility * pow(max(dot(n, normalize(uLightDir + toCamera)), 0.0), 256.0);
    vec3 colour = water * (skyAmbient(n) + uLightColour * diffuse) + uLightColour * specular * 0.4;

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
