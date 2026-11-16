#pragma once

#include <glm/glm.hpp>

namespace emerald {

class Shader;

// Direction towards the sun from a compass bearing (0 = north, 90 = east) and elevation, in degrees.
// World axes: +X east, +Y up, -Z north.
glm::vec3 sunDirection(float bearingDegrees, float elevationDegrees);

// Sun and sky parameters shared by every pass, so the visible sun matches the lighting.
// Colours are authored in sRGB and converted to linear in apply(); shaders light in linear space.
struct Atmosphere {
    glm::vec3 sunDir = sunDirection(225.0f, 22.0f); // south-west, low afternoon sun
    glm::vec3 sunColour{1.0f, 0.92f, 0.80f};
    glm::vec3 ambient{0.42f, 0.47f, 0.55f};
    glm::vec3 horizonColour{0.80f, 0.86f, 0.91f};
    glm::vec3 zenithColour{0.24f, 0.45f, 0.72f};

    // Height fog: density at sea level (per metre), falling off exponentially with altitude.
    float fogDensity = 1.5e-5f;
    float fogScaleHeight = 2000.0f; // a low haze layer, so low views get a soft sea horizon
    // Density scale for high cameras, eased in between these altitudes, so overviews stay clear.
    float highAltitudeFogScale = 0.6f;
    float highAltitudeStart = 5000.0f;
    float highAltitudeEnd = 100000.0f;

    // Sets uSunDir, uSunColour, uAmbient, uHorizonColour, uZenithColour, uFogDensity and uFogFalloff.
    void apply(Shader& shader, float cameraHeight = 0.0f) const;
};

} // namespace emerald
