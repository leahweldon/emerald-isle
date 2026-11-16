#version 330 core

in vec3 vWorldPos;
out vec4 fragColour;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;     // towards the sun, normalised
uniform vec3 uSunColour;
uniform vec3 uAmbient;
uniform vec3 uHorizonColour;
uniform float uFogDensity;
uniform float uFogFalloff;
uniform sampler2D uCoastMask;  // blurred land mask, see TerrainMesh::bindCoastMask
uniform vec2 uTerrainSize;     // heightmap extent in XZ, metres
uniform float uSeaRadius;      // nearest edge of the camera-centred plane, metres

// Linear space. Saturated so distant water stays blue-green through the haze.
const vec3 kSea     = vec3(0.004, 0.055, 0.085);
const vec3 kShallow = vec3(0.020, 0.160, 0.150); // matches terrain.frag

// Lighting is linear; the default framebuffer expects sRGB.
vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
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
    const vec3 n = vec3(0.0, 1.0, 0.0);
    vec3 toCamera = normalize(uCameraPos - vWorldPos);

    // Shallower-looking water near land. No bathymetry, so distance to coast stands in for depth.
    float coast = texture(uCoastMask, vWorldPos.xz / uTerrainSize + 0.5).r;
    vec3 water = mix(kSea, kShallow, smoothstep(0.02, 0.5, coast));

    float diffuse = max(dot(n, uSunDir), 0.0);
    // Blinn-Phong glint from the sun.
    float specular = pow(max(dot(n, normalize(uSunDir + toCamera)), 0.0), 256.0);
    vec3 colour = water * (uAmbient + uSunColour * diffuse) + uSunColour * specular * 0.4;

    // Same fog as terrain.frag.
    // Ramp fog to exactly 1 at the plane's edge, so the sea reaches the sky's horizon colour
    // whatever the camera height. Linear in 1/distance, which is roughly linear on screen near
    // the horizon, so the fade spreads over many rows instead of a few.
    float d = max(length(vWorldPos.xz - uCameraPos.xz), 1.0);
    float nearEdge = 0.1 * uSeaRadius;
    float edge = smoothstep(0.0, 1.0, clamp((1.0 / nearEdge - 1.0 / d) /
                                            (1.0 / nearEdge - 1.0 / uSeaRadius), 0.0, 1.0));
    colour = mix(colour, uHorizonColour, max(fogAmount(uCameraPos, vWorldPos), edge));
    fragColour = vec4(linearToSrgb(colour), 1.0);
}
