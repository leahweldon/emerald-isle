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
const glm::vec3 kBetaOzone(0.650e-6f, 1.881e-6f, 0.085e-6f); // ozone density follows Rayleigh
constexpr int kSamples = 32;

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
    if (dir.y < -0.1f) {
        return glm::vec3(0.0f);
    }
    const float b = glm::dot(origin, dir);
    const float c = glm::dot(origin, origin) - kAtmosphereRadius * kAtmosphereRadius;
    const float length = -b + std::sqrt(b * b - c);
    const float step = length / static_cast<float>(kSamples);

    float depthR = 0.0f;
    float depthM = 0.0f;
    for (int i = 0; i < kSamples; ++i) {
        const glm::vec3 p = origin + dir * (step * (static_cast<float>(i) + 0.5f));
        const float h = std::max(glm::length(p) - kEarthRadius, 0.0f);
        depthR += std::exp(-h / kScaleHeightRayleigh) * step;
        depthM += std::exp(-h / kScaleHeightMie) * step;
    }
    return glm::exp(-((kBetaRayleigh + kBetaOzone) * depthR + glm::vec3(kBetaMie * 1.1f * depthM)));
}

void Atmosphere::apply(Shader& shader, float cameraHeight) const
{
    shader.set("uSunDir", sunDir);
    shader.set("uSunColour", sunStrength * sunTransmittance(sunDir));
    const float t = glm::smoothstep(highAltitudeStart, highAltitudeEnd, cameraHeight);
    shader.set("uFogDensity", fogDensity * glm::mix(1.0f, highAltitudeFogScale, t));
    shader.set("uFogFalloff", 1.0f / fogScaleHeight);
}

} // namespace emerald
