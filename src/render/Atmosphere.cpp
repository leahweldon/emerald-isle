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
const glm::vec3 kBetaOzone(0.650e-6f, 1.881e-6f, 0.085e-6f);
constexpr float kOzoneCentre = 25e3f;    // ozone layer, a tent profile
constexpr float kOzoneHalfWidth = 15e3f;
constexpr int kSamples = 32;

// Moonlight overhead in full darkness. Far brighter than real moonlight, as if the eye had
// adapted, so the landscape stays readable; the sky model uses the same ratio to sunlight.
const glm::vec3 kMoonLight(0.090f, 0.120f, 0.200f);
constexpr float kMoonSky = 0.004f;
// Airglow and starlight: keeps the night sky (and so the ambient light) a dark blue.
const glm::vec3 kNightSky(0.0025f, 0.0045f, 0.0110f);
constexpr float kSunsetY = -0.02f; // sun about 1 degree down: the moon takes over as the light

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

glm::vec3 sunTransmittance(const glm::vec3& sunDir)
{
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
    return visible * glm::exp(-(kBetaRayleigh * depthR + kBetaOzone * depthO +
                                glm::vec3(kBetaMie * 1.1f * depthM)));
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
        return sunStrength * sunTransmittance(sunDir);
    }
    // A low moon is dimmed and warmed by the air like the sun; kMoonLight is its overhead value.
    const glm::vec3 overhead = sunTransmittance(glm::vec3(0.0f, 1.0f, 0.0f));
    return night() * kMoonLight * sunTransmittance(moonDir()) / overhead;
}

float Atmosphere::ambientScale() const
{
    return glm::mix(1.0f, 0.5f, glm::smoothstep(0.0f, 0.17f, sunDir.y));
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
    shader.set("uAmbientScale", ambientScale());
    const float t = glm::smoothstep(highAltitudeStart, highAltitudeEnd, cameraHeight);
    shader.set("uFogDensity", fogDensity * glm::mix(1.0f, highAltitudeFogScale, t));
    shader.set("uFogFalloff", 1.0f / fogScaleHeight);
}

} // namespace emerald
