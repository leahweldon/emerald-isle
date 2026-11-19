#version 330 core

// Single-scattering Rayleigh + Mie sky, ray-marched per pixel when rendering the cubemap, lit by
// the sun and, at night, the moon. Constants mirror src/render/Atmosphere.cpp (used there for
// sun transmittance). The sun and moon discs are smaller than a cubemap texel, so skybox.frag
// draws them analytically.

in vec2 vUv;
out vec4 fragColour;

uniform mat4 uInvViewProjRotation; // inverse of projection * rotation-only view
uniform vec3 uSunDir;              // towards the sun, normalised
uniform vec3 uMoonDir;             // towards the moon, normalised
uniform float uMoonIntensity;      // moonlight relative to sunlight; 0 by day
uniform vec3 uNightSky;            // airglow and starlight, before exposure
uniform float uHaze;               // evening haze, 0 to 1 (see Atmosphere::haze)
uniform float uExposure;           // scales radiance into display range
uniform bool uEncodeSrgb;          // true when drawing to the screen

const float PI = 3.14159265;
const float kEarthRadius = 6360e3;
const float kAtmosphereRadius = 6420e3;
const float kObserverHeight = 200.0;
const vec3 kBetaRayleigh = vec3(5.8e-6, 13.5e-6, 33.1e-6);
const float kBetaMie = 3e-6;
// Evening aerosol: denser, and scattering short wavelengths more (Angstrom exponent ~1.3).
const vec3 kBetaMieHazy = 2e-5 * vec3(0.76, 1.0, 1.34);
// Ozone absorbs orange/red along long paths. It sits in a layer around 25 km, which the grazing
// light of twilight crosses, so the sky stays blue after sunset.
const vec3 kBetaOzone = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float kOzoneCentre = 25e3;
const float kOzoneHalfWidth = 15e3;
const float kScaleHeightRayleigh = 7994.0;
const float kScaleHeightMie = 1200.0;
const float kMieG = 0.70;
const float kSunIntensity = 20.0;
const float kSunAngularRadius = 0.0047; // radians, ~0.27 degrees; matches skybox.frag
const int kViewSamples = 16;
const int kLightSamples = 8;
// Single scattering over-extinguishes near-horizontal paths (no multiple scattering), turning the
// horizon orange; the in-scattering path elevation is smoothly kept above this value.
const float kMinPathElevation = 0.08;
// Twilight is mostly light scattered more than once, which this model leaves out; it stands in
// as a blue glow, strongest as the sun sets and fading over ~5 degrees below the horizon.
const vec3 kTwilight = vec3(0.030, 0.055, 0.110);
const float kTwilightFade = 5.0; // degrees

// Distance to the far intersection with a sphere at the origin, or -1 if missed.
float raySphereFar(vec3 origin, vec3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) {
        return -1.0;
    }
    return -b + sqrt(disc);
}

// Rayleigh, Mie and ozone densities at height h.
vec3 densities(float h)
{
    return vec3(exp(-h / kScaleHeightRayleigh), exp(-h / kScaleHeightMie),
                max(1.0 - abs(h - kOzoneCentre) / kOzoneHalfWidth, 0.0));
}

vec3 gBetaMie; // set in main() from uHaze

vec3 extinction(vec3 depth)
{
    return exp(-(kBetaRayleigh * depth.x + gBetaMie * 1.1 * depth.y + kBetaOzone * depth.z));
}

// Optical depth from p to the top of the atmosphere towards dir; false where the planet blocks it.
bool lightDepth(vec3 p, vec3 dir, out vec3 depth)
{
    float lightStep = raySphereFar(p, dir, kAtmosphereRadius) / float(kLightSamples);
    depth = vec3(0.0);
    for (int j = 0; j < kLightSamples; ++j) {
        vec3 q = p + dir * (lightStep * (float(j) + 0.5));
        float hq = length(q) - kEarthRadius;
        if (hq < 0.0) {
            return false;
        }
        depth += densities(hq) * lightStep;
    }
    return true;
}

vec2 phases(float mu)
{
    float g2 = kMieG * kMieG;
    return vec2(3.0 / (16.0 * PI) * (1.0 + mu * mu),
                3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + mu * mu)) /
                    ((2.0 + g2) * pow(1.0 + g2 - 2.0 * kMieG * mu, 1.5)));
}

vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main()
{
    vec4 far = uInvViewProjRotation * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 ray = normalize(far.xyz / far.w);
    // Below the horizon is shaded as the horizon, so fog and sea meet it without a seam. The
    // elevation is lifted smoothly (no kink) so the sky gradient has no visible band.
    float up = max(ray.y, 0.0);
    float lifted = sqrt(up * up + kMinPathElevation * kMinPathElevation);
    vec3 dir = normalize(vec3(ray.x, lifted, ray.z));

    vec3 origin = vec3(0.0, kEarthRadius + kObserverHeight, 0.0);
    float step_ = raySphereFar(origin, dir, kAtmosphereRadius) / float(kViewSamples);
    gBetaMie = mix(vec3(kBetaMie), kBetaMieHazy, uHaze);
    // With a low sun, single scattering strips the blue from long near-horizontal paths and the
    // horizon turns green; multiple scattering would refill it. The Rayleigh (blue sky) light is
    // attenuated along a shortened view path instead. The Mie glow towards the sun keeps its
    // full path, so it stays warm.
    float rayleighView = mix(1.0, 0.5, uHaze);
    bool moonUp = uMoonIntensity > 0.0;

    // In-scattered Rayleigh and Mie light from the sun and the moon.
    vec3 sunR = vec3(0.0), sunM = vec3(0.0), moonR = vec3(0.0), moonM = vec3(0.0);
    vec3 depth = vec3(0.0);
    for (int i = 0; i < kViewSamples; ++i) {
        vec3 p = origin + dir * (step_ * (float(i) + 0.5));
        vec3 d = densities(length(p) - kEarthRadius) * step_;
        depth += d;
        vec3 toLight;
        if (lightDepth(p, uSunDir, toLight)) {
            sunR += extinction(depth * rayleighView + toLight) * d.x;
            sunM += extinction(depth + toLight) * d.y;
        }
        if (moonUp && lightDepth(p, uMoonDir, toLight)) {
            moonR += extinction(depth * rayleighView + toLight) * d.x;
            moonM += extinction(depth + toLight) * d.y;
        }
    }
    vec2 sunPhase = phases(dot(dir, uSunDir));
    vec2 moonPhase = phases(dot(dir, uMoonDir));
    vec3 colour = kSunIntensity * (sunR * kBetaRayleigh * sunPhase.x + sunM * gBetaMie * sunPhase.y);
    colour += kSunIntensity * uMoonIntensity *
              (moonR * kBetaRayleigh * moonPhase.x + moonM * gBetaMie * moonPhase.y);

    // Soft halo around the sun, wide enough to survive the cubemap resolution.
    vec3 transmittance = extinction(depth);
    float r = acos(clamp(dot(ray, uSunDir), -1.0, 1.0)) / kSunAngularRadius;
    colour += kSunIntensity * transmittance * 0.02 * exp(-r * r / 40.0);

    float sunElevation = degrees(asin(clamp(uSunDir.y, -1.0, 1.0)));
    float twilight = smoothstep(4.0, 0.0, sunElevation) * exp(min(sunElevation, 0.0) / kTwilightFade);
    vec2 towardsSun = normalize(uSunDir.xz + 1e-6);
    colour += kTwilight * twilight * (1.0 + 0.5 * max(dot(normalize(dir.xz + 1e-6), towardsSun), 0.0)) *
              (0.8 + 0.4 * (1.0 - dir.y));

    // Airglow and starlight, brighter towards the horizon, so the night sky is never black.
    colour += uNightSky * (1.0 + 0.6 * (1.0 - dir.y));

    colour *= uExposure;
    fragColour = vec4(uEncodeSrgb ? linearToSrgb(colour) : colour, 1.0);
}
