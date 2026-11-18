#pragma once

#include <glm/glm.hpp>

namespace emerald {

class Shader;

// Direction towards the sun from a compass bearing (0 = north, 90 = east) and elevation, in degrees.
// World axes: +X east, +Y up, -Z north.
glm::vec3 sunDirection(float bearingDegrees, float elevationDegrees);

// Fraction of sunlight reaching sea level along sunDir, from the same Rayleigh + Mie model as
// sky.frag. Gives the low sun its warm colour, and fades to zero as the sun sets.
glm::vec3 sunTransmittance(const glm::vec3& sunDir);

// Sun, moon and sky parameters shared by every pass, so the visible sun matches the lighting.
// Ambient light and fog colour come from the sky cubemap (see Sky).
struct Atmosphere {
    glm::vec3 sunDir = sunDirection(225.0f, 22.0f); // south-west, low afternoon sun
    float sunStrength = 1.0f;   // direct light on terrain = sunStrength * sunTransmittance
    float exposure = 1.3f;      // sky radiance scale into display range
    glm::mat3 starRotation{1.0f}; // world directions to the celestial sphere, from the time of day

    // Always full, opposite the sun: up at night, down by day.
    glm::vec3 moonDir() const { return -sunDir; }
    // 0 by day, 1 from the end of civil twilight (sun 7 degrees down).
    float night() const;
    // The light that shades and casts shadows: the sun, or the moon once the sun has set.
    glm::vec3 lightDir() const;
    glm::vec3 lightColour() const;
    // Moonlight relative to sunlight in the sky model, and the airglow floor (before exposure).
    float moonSkyIntensity() const;
    glm::vec3 nightSky() const;

    // Height fog: density at sea level (per metre), falling off exponentially with altitude.
    float fogDensity = 1.1e-5f;
    float fogScaleHeight = 2000.0f; // a low haze layer, so low views get a soft sea horizon
    // Density scale for high cameras, eased in between these altitudes, so overviews stay clear.
    float highAltitudeFogScale = 0.6f;
    float highAltitudeStart = 5000.0f;
    float highAltitudeEnd = 100000.0f;

    // Sets uLightDir, uLightColour, uFogDensity and uFogFalloff.
    void apply(Shader& shader, float cameraHeight = 0.0f) const;
};

} // namespace emerald
