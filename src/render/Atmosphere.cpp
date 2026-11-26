#include "render/Atmosphere.hpp"

#include "render/Shader.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Mirrors sky.frag.
constexpr float kEarthRadius = 6360e3f;
constexpr float kAtmosphereRadius = 6420e3f;
constexpr float kObserverHeight = 200.0f;
constexpr float kScaleHeightRayleigh = 7994.0f;
constexpr float kScaleHeightMie = 1200.0f;
const glm::vec3 kBetaRayleigh(5.8e-6f, 13.5e-6f, 33.1e-6f);
constexpr float kBetaMie = 3e-6f;
// Evening aerosol: denser, and scattering short wavelengths more (Angstrom exponent ~1.3).
const glm::vec3 kBetaMieHazy = 2e-5f * glm::vec3(0.76f, 1.0f, 1.34f);
const glm::vec3 kBetaOzone(0.650e-6f, 1.881e-6f, 0.085e-6f);
constexpr float kOzoneCentre = 25e3f;    // ozone layer, a tent profile
constexpr float kOzoneHalfWidth = 15e3f;
constexpr int kSamples = 32;

// Moonlight overhead in full darkness. Far brighter than real moonlight, as if the eye had
// adapted, so the landscape stays readable; the sky model uses the same ratio to sunlight.
const glm::vec3 kMoonLight(0.135f, 0.180f, 0.300f);
constexpr float kMoonSky = 0.004f;
// Airglow and starlight: keeps the night sky (and so the ambient light) a dark blue.
const glm::vec3 kNightSky(0.00375f, 0.00675f, 0.0165f);
constexpr float kGoldenSaturation = 0.7f; // extra saturation of a low sun's light
constexpr float kGoldenBoost = 0.4f;      // extra strength of a low sun's light
// Shade at golden hour is lit by the blue sky overhead rather than the warm horizon.
const glm::vec3 kGoldenShade(0.80f, 0.93f, 1.25f);
constexpr float kSunsetY = -0.02f; // sun about 1 degree down: the moon takes over as the light
// Rain: the fraction of direct light full cloud removes, and the fog density multiplier at
// heavy rain (visibility falls from ~250 km to ~30 km).
constexpr float kRainDimming = 0.85f;
constexpr float kRainFog = 8.0f;
// Overcast zenith radiance per unit of sun height, cool grey; heavy rain darkens it further.
const glm::vec3 kCloudRadiance = 0.30f * glm::vec3(0.92f, 0.96f, 1.0f);
constexpr float kHeavyCloudDarkening = 0.65f;
constexpr float kRainForFullCloud = 0.4f; // rain intensity at which cloud cover is complete

} // namespace

namespace emerald {

glm::vec3 sunDirection(float bearingDegrees, float elevationDegrees)
{
    const float bearing = glm::radians(bearingDegrees);
    const float elevation = glm::radians(elevationDegrees);
    const float horizontal = std::cos(elevation);
    return glm::normalize(glm::vec3(horizontal * std::sin(bearing), std::sin(elevation),
                                    -horizontal * std::cos(bearing)));
}

glm::vec3 sunTransmittance(const glm::vec3& sunDir, float haze)
{
    const glm::vec3 betaMie = glm::mix(glm::vec3(kBetaMie), kBetaMieHazy, haze);
    const glm::vec3 origin(0.0f, kEarthRadius + kObserverHeight, 0.0f);
    const glm::vec3 dir = glm::normalize(sunDir);
    // The sea horizon hides the sun as its disc (~0.5 degrees) sets.
    const float visible = glm::smoothstep(-0.0047f, 0.0047f, dir.y);
    if (visible <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const float b = glm::dot(origin, dir);
    const float c = glm::dot(origin, origin) - kAtmosphereRadius * kAtmosphereRadius;
    const float length = -b + std::sqrt(b * b - c);
    const float step = length / static_cast<float>(kSamples);

    float depthR = 0.0f;
    float depthM = 0.0f;
    float depthO = 0.0f;
    for (int i = 0; i < kSamples; ++i) {
        const glm::vec3 p = origin + dir * (step * (static_cast<float>(i) + 0.5f));
        const float h = std::max(glm::length(p) - kEarthRadius, 0.0f);
        depthR += std::exp(-h / kScaleHeightRayleigh) * step;
        depthM += std::exp(-h / kScaleHeightMie) * step;
        depthO += std::max(1.0f - std::abs(h - kOzoneCentre) / kOzoneHalfWidth, 0.0f) * step;
    }
    return visible * glm::exp(-(kBetaRayleigh * depthR + kBetaOzone * depthO + betaMie * 1.1f * depthM));
}

float Atmosphere::haze() const
{
    return glm::smoothstep(0.5f, 0.208f, sunDir.y);
}

float Atmosphere::night() const
{
    return glm::smoothstep(kSunsetY, -0.12f, sunDir.y);
}

glm::vec3 Atmosphere::lightDir() const
{
    return sunDir.y > kSunsetY ? sunDir : moonDir();
}

glm::vec3 Atmosphere::lightColour() const
{
    if (sunDir.y > kSunsetY) {
        // Golden hour: the low sun's colour is saturated and lifted beyond the transmittance,
        // so lit slopes glow orange against the blue shade, as a photograph exposes for them.
        const glm::vec3 sun = sunTransmittance(sunDir, haze());
        const float luminance = glm::dot(sun, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        const glm::vec3 golden = glm::max(glm::vec3(luminance) + (sun - luminance) * (1.0f + kGoldenSaturation * haze()),
                                          glm::vec3(0.0f));
        return (1.0f - kRainDimming * overcast()) * sunStrength * (1.0f + kGoldenBoost * haze()) * golden;
    }
    // A low moon is dimmed and warmed by the air like the sun; kMoonLight is its overhead value.
    const glm::vec3 overhead = sunTransmittance(glm::vec3(0.0f, 1.0f, 0.0f), haze());
    return (1.0f - kRainDimming * overcast()) * night() * kMoonLight * sunTransmittance(moonDir(), haze()) / overhead;
}

float Atmosphere::ambientScale() const
{
    // Under cloud the sky is the light, so it is no longer halved against the sun.
    const float clear = glm::mix(1.0f, 0.5f, glm::smoothstep(-0.02f, 0.05f, sunDir.y));
    return glm::mix(clear, 1.0f, overcast());
}

float Atmosphere::overcast() const
{
    return glm::smoothstep(0.0f, kRainForFullCloud, rain);
}

glm::vec3 Atmosphere::overcastSky() const
{
    // Daylight through the cloud follows the sun's height, softened since cloud scatters light
    // from the whole sky, and fades through twilight; at night the cloud glows faintly with
    // moonlight and airglow.
    const float sun = (0.15f + 0.85f * std::max(sunDir.y, 0.0f)) * glm::smoothstep(-0.12f, 0.02f, sunDir.y);
    const glm::vec3 day = kCloudRadiance * sun;
    const glm::vec3 nightGlow = nightSky() * 3.0f + glm::vec3(moonSkyIntensity() * 4.0f);
    return (day + nightGlow) * glm::mix(1.0f, kHeavyCloudDarkening, rain);
}

float Atmosphere::moonSkyIntensity() const
{
    return kMoonSky * night();
}

glm::vec3 Atmosphere::nightSky() const
{
    return kNightSky * night();
}

void Atmosphere::apply(Shader& shader, float cameraHeight) const
{
    shader.set("uLightDir", lightDir());
    shader.set("uLightColour", lightColour());
    shader.set("uAmbientTint", ambientScale() * glm::mix(glm::vec3(1.0f), kGoldenShade, haze() * (1.0f - night())));
    shader.set("uNight", night());
    const float t = glm::smoothstep(highAltitudeStart, highAltitudeEnd, cameraHeight);
    shader.set("uFogDensity", (fog ? fogDensity : 0.0f) * glm::mix(1.0f, highAltitudeFogScale, t) * glm::mix(1.0f, kRainFog, rain));
    shader.set("uFogFalloff", 1.0f / fogScaleHeight);
    shader.set("uRain", rain);
    shader.set("uOvercast", overcast());
    shader.set("uWetness", wetness);
}

} // namespace emerald
