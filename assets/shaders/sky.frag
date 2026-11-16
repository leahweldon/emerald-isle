#version 330 core

// Single-scattering Rayleigh + Mie sky, ray-marched per pixel.
// Constants mirror src/render/Atmosphere.cpp (used there for sun transmittance).

in vec2 vUv;
out vec4 fragColour;

uniform mat4 uInvViewProjRotation; // inverse of projection * rotation-only view
uniform vec3 uSunDir;              // towards the sun, normalised
uniform float uExposure;           // scales radiance into display range
uniform bool uEncodeSrgb;          // true when drawing to the screen

const float PI = 3.14159265;
const float kEarthRadius = 6360e3;
const float kAtmosphereRadius = 6420e3;
const float kObserverHeight = 200.0;
const vec3 kBetaRayleigh = vec3(5.8e-6, 13.5e-6, 33.1e-6);
const float kBetaMie = 3e-6;
// Ozone absorbs orange/red along long paths; without it single scattering yellows the horizon.
const vec3 kBetaOzone = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float kScaleHeightRayleigh = 7994.0;
const float kScaleHeightMie = 1200.0;
const float kMieG = 0.70;
const float kSunIntensity = 20.0;
const float kSunAngularRadius = 0.0047; // radians, ~0.27 degrees
const int kViewSamples = 16;
const int kLightSamples = 8;
// Single scattering over-extinguishes near-horizontal paths (no multiple scattering), turning the
// horizon orange; the in-scattering path elevation is smoothly kept above this value.
const float kMinPathElevation = 0.08;

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
    float length_ = raySphereFar(origin, dir, kAtmosphereRadius);
    float step_ = length_ / float(kViewSamples);

    float mu = dot(dir, uSunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g2 = kMieG * kMieG;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + mu * mu)) /
                   ((2.0 + g2) * pow(1.0 + g2 - 2.0 * kMieG * mu, 1.5));

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float depthR = 0.0;
    float depthM = 0.0;
    for (int i = 0; i < kViewSamples; ++i) {
        vec3 p = origin + dir * (step_ * (float(i) + 0.5));
        float h = length(p) - kEarthRadius;
        float dR = exp(-h / kScaleHeightRayleigh) * step_;
        float dM = exp(-h / kScaleHeightMie) * step_;
        depthR += dR;
        depthM += dM;

        // Sun light reaching this sample; skipped where the planet blocks it.
        float lightLength = raySphereFar(p, uSunDir, kAtmosphereRadius);
        float lightStep = lightLength / float(kLightSamples);
        float lightR = 0.0;
        float lightM = 0.0;
        bool shadowed = false;
        for (int j = 0; j < kLightSamples; ++j) {
            vec3 q = p + uSunDir * (lightStep * (float(j) + 0.5));
            float hq = length(q) - kEarthRadius;
            if (hq < 0.0) {
                shadowed = true;
                break;
            }
            lightR += exp(-hq / kScaleHeightRayleigh) * lightStep;
            lightM += exp(-hq / kScaleHeightMie) * lightStep;
        }
        if (!shadowed) {
            vec3 tau = (kBetaRayleigh + kBetaOzone) * (depthR + lightR) +
                       kBetaMie * 1.1 * (depthM + lightM);
            vec3 attenuation = exp(-tau);
            sumR += attenuation * dR;
            sumM += attenuation * dM;
        }
    }
    vec3 colour = kSunIntensity * (sumR * kBetaRayleigh * phaseR + sumM * kBetaMie * phaseM);

    // Sun disc with limb darkening, seen through the atmosphere along this ray, plus a soft halo.
    vec3 transmittance = exp(-((kBetaRayleigh + kBetaOzone) * depthR + kBetaMie * 1.1 * depthM));
    float r = acos(clamp(dot(ray, uSunDir), -1.0, 1.0)) / kSunAngularRadius;
    float edge = fwidth(r);
    float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - r * r, 0.0)));
    colour += kSunIntensity * transmittance * limb * (1.0 - smoothstep(1.0 - edge, 1.0 + edge, r));
    colour += kSunIntensity * transmittance * 0.02 * exp(-r * r / 40.0);

    colour *= uExposure;
    fragColour = vec4(uEncodeSrgb ? linearToSrgb(toneMap(colour)) : colour, 1.0);
}
