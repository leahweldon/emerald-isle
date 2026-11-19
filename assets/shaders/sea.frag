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
uniform float uRain;  // 0 dry, 1 heavy rain
uniform float uOvercast; // cloud cover: no sharp glint once the sun is hidden
uniform float uFogDensity;
uniform float uFogFalloff;
uniform sampler2D uCoastMask;  // blurred land mask, see TerrainMesh::bindCoastMask
uniform vec2 uTerrainSize;     // heightmap extent in XZ, metres
uniform float uSeaRadius;      // nearest edge of the camera-centred plane, metres
uniform sampler2DArray uRipples; // two tileable ripple normal maps (see SeaSurface)
uniform vec2 uRippleTile;        // metres per repeat of each map
uniform vec2 uRippleOffset[2];   // wind drift, wrapped to each tile
uniform float uRippleStrength;   // with the wind speed
uniform float uWindSpeed;        // metres per second
uniform sampler2DArray uSeabed;  // ground height under the sea, near and wide (see SeabedMap)
uniform vec4 uSeabedArea[2];     // x, z offset from the camera, 1 / half size
uniform float uSeabedRange;      // height at depth 0, world units
uniform float uExaggeration;
uniform sampler2D uReflection;   // mirrored terrain, linear colour; alpha 0 where only sky
uniform vec2 uViewportSize;      // pixels, for screen coordinates
uniform float uWaveTime;         // seconds; frozen for screenshots

#include "shadow.glsl"

// Linear space. Saturated so distant water stays blue-green through the haze.
const vec3 kSea     = vec3(0.004, 0.055, 0.085);
const vec3 kShallow = vec3(0.020, 0.160, 0.150); // matches terrain.frag
const float kWaterF0 = 0.02;      // reflectance looking straight down (index of refraction 1.33)
const float kGlintRoughness = 0.04; // GGX alpha of a single ripple facet: a tight sun glint
// Shallow water lets the bright seabed through: turquoise fading to the open-sea colour with depth.
const vec3 kTurquoise = vec3(0.030, 0.260, 0.230);
const float kClarity = 4.0;       // metres of depth over which the seabed fades out
const float kFoamDepth = 0.8;     // metres: foam where the water is shallower than this
const vec3 kFoam = vec3(0.7);     // foam albedo
const float kReflectionDistortion = 0.03; // screen fraction per unit of ripple slope
const float kFoamCover = 0.7;     // at most this much of the surface is foam
// Raindrop rings: at most one per grid cell (the cell divides the wave period, so the pattern
// wraps with the waves), each expanding and fading over its period; rain intensity sets the share
// of cells with a ring. They fade out with distance before they alias, leaving rougher water.
const float kRingCell = 0.75;     // metres
const float kRingPeriod = 1.1;    // seconds
const float kRingRadius = 0.55;   // metres, at the end of the period
const float kRingWavelength = 0.12;
const float kRingSlope = 0.6;     // peak slope of a fresh ring
const float kRingHighlight = 0.6; // brightening of fresh crests (and the splash at the centre)
const float kRingFadeStart = 40.0;
const float kRingFadeEnd = 220.0;
const float kRainRoughness = 0.012; // slope variance added by heavy rain

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

vec3 hash3(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yxz + 33.33);
    return fract((q.xxy + q.yzz) * q.zyx);
}

// Slope (d height / dx, d height / dz) of the raindrop rings around wrapped world point p, and
// in crest how bright their young crests are: under an even overcast sky, tilting the water
// barely changes its reflection, so the rings are seen by their crests catching the light.
vec2 rainRings(vec2 p, out float crest)
{
    vec2 cell = floor(p / kRingCell);
    vec2 slope = vec2(0.0);
    crest = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 c = cell + vec2(i, j);
            vec3 h = hash3(mod(c, 1536.0 / kRingCell));
            float cycle = uWaveTime / kRingPeriod + h.z * 17.0;
            // A fresh draw each cycle decides whether this cell has a ring.
            if (hash3(vec2(floor(cycle), h.x * 113.0)).x > uRain) {
                continue;
            }
            float age = fract(cycle);
            vec2 d = p - (c + 0.15 + 0.7 * h.xy) * kRingCell;
            float dist = max(length(d), 1e-4);
            float x = dist - age * kRingRadius;
            float wave = sin(x * 6.2831853 / kRingWavelength) * exp(-x * x / (kRingWavelength * kRingWavelength));
            float fade = (1.0 - age) * (1.0 - age);
            slope += kRingSlope * fade * wave * d / dist;
            crest += fade * fade * max(wave, 0.0);
        }
    }
    return slope;
}

// Depth of water in metres, from the nearer seabed layer that covers this point; -1 if neither.
float waterDepth()
{
    for (int i = 0; i < 2; ++i) {
        vec2 uv = (vRelPos.xz + uSeabedArea[i].xy) * uSeabedArea[i].z * 0.5 + 0.5;
        if (all(greaterThan(uv, vec2(0.01))) && all(lessThan(uv, vec2(0.99)))) {
            float height = (0.5 - texture(uSeabed, vec3(uv, float(i))).r) * 2.0 * uSeabedRange;
            return max(-height, 0.0) / uExaggeration;
        }
    }
    return -1.0;
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
    float crest = 0.0;
    if (uRain > 0.0) {
        float near = 1.0 - smoothstep(kRingFadeStart, kRingFadeEnd, length(vRelPos));
        if (near > 0.0) {
            vec2 ring = near * rainRings(vWaveXZ, crest);
            crest *= near;
            n = normalize(n - vec3(ring.x, 0.0, ring.y));
        }
        slopeVariance += kRainRoughness * uRain;
    }
    // Seen edge-on, a ripple can face away from the camera; keep the shading normal visible.
    n = normalize(n + vec3(0.0, max(0.05 - dot(n, v), 0.0), 0.0));

    // Shallower-looking water near land: around Ireland the blurred coast mask tints kilometres
    // of coastal water, then the seabed depth adds clear turquoise shallows and foam at the shore.
    float coast = texture(uCoastMask, vWorldXZ / uTerrainSize + 0.5).r;
    vec3 water = mix(kSea, kShallow, smoothstep(0.02, 0.5, coast));
    float depth = waterDepth();
    float foam = 0.0;
    if (depth >= 0.0) {
        water = mix(water, kTurquoise, exp(-depth / kClarity));
        // Broken by the ripples, so the foam line is lacy rather than a solid band. Far away the
        // lace averages out, so the line thins to a faint edge instead of a white stripe.
        float lace = texture(uRipples, vec3(vWaveXZ / uRippleTile.y, 1.0)).r;
        float distanceFade = mix(1.0, 0.3, smoothstep(3e3, 25e3, length(vRelPos)));
        foam = kFoamCover * distanceFade * smoothstep(kFoamDepth, 0.1, depth) *
               smoothstep(0.4, 0.65, lace + 0.3 * (1.0 - depth / kFoamDepth));
    }

    // Terrain shadows on the water (headlands over bays) dim the direct light and its glint.
    float diffuse = max(dot(n, uLightDir), 0.0);
    float visibility = uLightDir.y > 0.0 ? shadowVisibility(vRelPos, vec3(0.0, 1.0, 0.0), uLightDir.y, 1.0) : 0.0;
    visibility = mix(visibility, 1.0, 0.5 * uRain); // cloud softens what direct light is left
    vec3 lightOnSurface = skyAmbient(n) + uLightColour * diffuse * visibility;
    vec3 body = water * lightOnSurface;

    // Mirror reflection of the sky, weighted by Fresnel: mostly water colour looking down, mostly
    // sky at grazing angles, so the far sea runs into the horizon.
    vec3 r = reflect(-v, n);
    r.y = abs(r.y); // a ripple tilted past the view reflects the sky on its other side
    vec3 reflection = texture(uSkyCube, r).rgb;
    // Mirrored terrain where it was drawn, nudged by the ripples, over the sky behind it.
    vec2 screen = gl_FragCoord.xy / uViewportSize + n.xz * kReflectionDistortion;
    vec4 mirrored = texture(uReflection, clamp(screen, vec2(0.0), vec2(1.0)));
    reflection = mix(reflection, mirrored.rgb, mirrored.a);
    float fresnel = schlick(dot(n, v));
    // Waves too small for the ripple maps still tilt the surface (Cox and Munk: slope variance
    // 0.003 + 0.00512 per m/s of wind). They count once the ripples stop being resolved, which
    // widens the distant glint into a glitter path.
    float unresolved = (0.003 + 0.00512 * uWindSpeed) * smoothstep(50.0, 3000.0, length(vRelPos));
    float alpha = sqrt(kGlintRoughness * kGlintRoughness + max(slopeVariance, unresolved));
    vec3 glint = uLightColour * visibility * ggxSpecular(n, v, uLightDir, alpha) * (1.0 - uOvercast);
    vec3 colour = mix(mix(body, reflection, fresnel) + glint, kFoam * lightOnSurface, foam);
    colour += kRingHighlight * crest * kFoam * lightOnSurface;
    colour = nightVision(colour);

    // Same fog as terrain.frag.
    // Ramp fog to exactly 1 at the plane's edge, so the sea reaches the sky's horizon colour
    // whatever the camera height. Linear in 1/distance, which is roughly linear on screen near
    // the horizon, so the fade spreads over many rows instead of a few.
    float d = max(length(vRelPos.xz), 1.0);
    float nearEdge = 0.1 * uSeaRadius;
    float edge = smoothstep(0.0, 1.0, clamp((1.0 / nearEdge - 1.0 / d) /
                                            (1.0 / nearEdge - 1.0 / uSeaRadius), 0.0, 1.0));
    colour = mix(colour, fogColour(vRelPos), max(fogAmount(vRelPos), edge));
    fragColour = vec4(colour, 1.0); // linear HDR, tone mapped in tonemap.frag
}
