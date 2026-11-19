#include "render/SeaSurface.hpp"

#include "render/Shader.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace emerald {

namespace {

constexpr int kSegments = 192;       // vertices around each ring
constexpr float kInnerRadius = 1.0f; // metres
constexpr float kRingGrowth = 1.0f + 6.2832f / kSegments; // square cells: radial step = arc step
constexpr double kGravity = 9.81;
constexpr double kPi = 3.14159265358979323846;

struct WaveShape {
    double wavelength; // metres
    double turn;       // degrees from the wind direction
    double amplitude;  // fraction of the largest wave's amplitude
};
// A long swell along the wind and shorter waves spread either side of it.
constexpr WaveShape kShapes[SeaSurface::kWaves] = {
    {52.0, 0.0, 1.0}, {31.0, 25.0, 0.6}, {19.0, -30.0, 0.45}, {11.0, 50.0, 0.3}};
constexpr double kSteepness = 0.6; // overall Gerstner sharpness, 0 (sine) to 1 (cusps)

} // namespace

SeaSurface::SeaSurface(float radius, const Wind& wind)
    : m_radius(radius)
    , m_wind(wind)
{
    // Fully developed sea (Pierson-Moskowitz): significant height ~0.21 U^2 / g; the largest
    // wave takes about a quarter of it.
    const double speed = static_cast<double>(wind.speed);
    const double largest = 0.25 * 0.21 * speed * speed / kGravity;
    const double lattice = 2.0 * kPi / kWavePeriod;
    for (int i = 0; i < kWaves; ++i) {
        const WaveShape& shape = kShapes[i];
        const double bearing = (static_cast<double>(wind.bearing) + shape.turn) * kPi / 180.0;
        // Nearest wave vector on the periodic lattice; north is -Z.
        const double reach = kWavePeriod / shape.wavelength;
        const double mx = std::round(std::sin(bearing) * reach);
        const double mz = std::round(-std::cos(bearing) * reach);
        const double kx = mx * lattice;
        const double kz = mz * lattice;
        const double k = std::sqrt(kx * kx + kz * kz);
        const double amplitude = largest * shape.amplitude;
        m_waves[static_cast<std::size_t>(i)] =
            glm::vec4(static_cast<float>(kx), static_cast<float>(kz), static_cast<float>(amplitude),
                      static_cast<float>(std::sqrt(kGravity * k))); // deep-water dispersion
        m_steepness[i] = static_cast<float>(kSteepness / (k * amplitude * kWaves));
    }

    // Vertex 0 is the centre; ring r, segment s is 1 + r * kSegments + s.
    std::vector<float> vertices = {0.0f, 0.0f};
    int rings = 0;
    for (float r = kInnerRadius; ; r *= kRingGrowth) {
        const float ringRadius = std::min(r, radius);
        for (int s = 0; s < kSegments; ++s) {
            const float a = 6.2831853f * static_cast<float>(s) / kSegments;
            vertices.push_back(ringRadius * std::cos(a));
            vertices.push_back(ringRadius * std::sin(a));
        }
        ++rings;
        if (r >= radius) {
            break;
        }
    }
    std::vector<std::uint32_t> indices;
    const auto at = [](int ring, int segment) {
        return static_cast<std::uint32_t>(1 + ring * kSegments + (segment % kSegments));
    };
    for (int s = 0; s < kSegments; ++s) {
        indices.insert(indices.end(), {0u, at(0, s + 1), at(0, s)}); // CCW seen from +Y
    }
    for (int ring = 0; ring + 1 < rings; ++ring) {
        for (int s = 0; s < kSegments; ++s) {
            const std::uint32_t a = at(ring, s), b = at(ring, s + 1);
            const std::uint32_t c = at(ring + 1, s), d = at(ring + 1, s + 1);
            indices.insert(indices.end(), {a, b, c, b, d, c});
        }
    }
    m_indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

SeaSurface::~SeaSurface()
{
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

void SeaSurface::apply(Shader& shader, const glm::dvec3& cameraPos, double time) const
{
    for (int i = 0; i < kWaves; ++i) {
        shader.set("uWaves[" + std::to_string(i) + "]", m_waves[static_cast<std::size_t>(i)]);
    }
    shader.set("uWaveSteepness", m_steepness);
    // Camera position on the wave lattice, wrapped in double so it stays small and exact.
    const auto wrap = [](double v) { return static_cast<float>(v - kWavePeriod * std::floor(v / kWavePeriod)); };
    shader.set("uWaveOrigin", glm::vec2(wrap(cameraPos.x), wrap(cameraPos.z)));
    shader.set("uWaveTime", static_cast<float>(time));
}

void SeaSurface::draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace emerald
