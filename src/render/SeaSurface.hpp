#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>

namespace emerald {

class Shader;

// Wind over the sea: sets wave heights, directions and how fast the ripples drift.
struct Wind {
    float bearing = 60.0f; // direction the wind blows towards, compass degrees (from the WSW)
    float speed = 8.0f;    // metres per second, a fresh breeze
};

// Infinite sea: a camera-centred grid of rings whose radii grow geometrically from 1 m to the
// horizon, so cells stay roughly square at every distance. Gerstner waves displace it in
// sea.vert near the camera and fade out with distance before the rings get too coarse for them.
class SeaSurface {
public:
    static constexpr int kWaves = 4;
    // Wave vectors are whole multiples of 2 pi / kWavePeriod, so the waves repeat every
    // kWavePeriod metres and world positions can be wrapped (in double) before reaching float.
    static constexpr double kWavePeriod = 1536.0;

    SeaSurface(float radius, const Wind& wind);
    ~SeaSurface();

    SeaSurface(const SeaSurface&) = delete;
    SeaSurface& operator=(const SeaSurface&) = delete;

    // Sets uWaves, uWaveSteepness, uWaveOrigin and uWaveTime (seconds).
    void apply(Shader& shader, const glm::dvec3& cameraPos, double time) const;
    void draw() const;

    float radius() const { return m_radius; }
    const Wind& wind() const { return m_wind; }

private:
    float m_radius;
    Wind m_wind;
    std::array<glm::vec4, kWaves> m_waves{}; // wave vector x, z, amplitude (m), angular frequency
    glm::vec4 m_steepness{0.0f};             // Gerstner Q per wave
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_indexCount = 0;
};

} // namespace emerald
