#pragma once

#include <glm/glm.hpp>

namespace emerald {

class Shader;

// Direction towards the sun from a compass bearing (0 = north, 90 = east) and elevation, in degrees.
// World axes: +X east, +Y up, -Z north.
glm::vec3 sunDirection(float bearingDegrees, float elevationDegrees);

// Fraction of sunlight reaching sea level along sunDir, from the same Rayleigh + Mie model as
// sky.frag. Gives the low sun its warm colour.
glm::vec3 sunTransmittance(const glm::vec3& sunDir);

// Sun and sky parameters shared by every pass, so the visible sun matches the lighting.
// Authored colours are sRGB and converted to linear in apply(); shaders light in linear space.
struct Atmosphere {
    glm::vec3 sunDir = sunDirection(225.0f, 22.0f); // south-west, low afternoon sun
    float sunStrength = 1.0f;   // direct light on terrain = sunStrength * sunTransmittance
    float exposure = 1.3f;      // sky radiance scale into display range
    glm::vec3 ambient{0.42f, 0.47f, 0.55f};
    glm::vec3 horizonColour{0.80f, 0.86f, 0.91f};

    // Height fog: density at sea level (per metre), falling off exponentially with altitude.
    float fogDensity = 1.5e-5f;
    float fogScaleHeight = 2000.0f; // a low haze layer, so low views get a soft sea horizon
    // Density scale for high cameras, eased in between these altitudes, so overviews stay clear.
    float highAltitudeFogScale = 0.6f;
    float highAltitudeStart = 5000.0f;
    float highAltitudeEnd = 100000.0f;

    // Sets uSunDir, uSunColour, uAmbient, uHorizonColour, uFogDensity and uFogFalloff.
    void apply(Shader& shader, float cameraHeight = 0.0f) const;
};

} // namespace emerald
