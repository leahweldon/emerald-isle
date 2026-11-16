#pragma once

#include <glad/gl.h>

#include <filesystem>

namespace emerald {

// Heightmap encoding written by tools/make_heightmap.py.
constexpr float kHeightmapMetresPerPixel = 230.0f;
constexpr float kHeightmapMetresPerUnit = 0.02f;

// Relief is hard to read at true scale from orbit.
constexpr float kVerticalExaggeration = 2.5f;

// World units are metres.
struct TerrainConfig {
    float metresPerPixel = kHeightmapMetresPerPixel;
    float metresPerUnit = kHeightmapMetresPerUnit;
    float verticalExaggeration = kVerticalExaggeration;
    // Use every Nth pixel in each direction.
    int sampleStep = 2;
    // Sea pixels (value 0) are lowered to this depth so the coast slopes under the sea plane.
    float seaFloorDepth = 30.0f;
};

// Indexed triangle grid built from a 16-bit heightmap, centred on the origin in XZ.
class TerrainMesh {
public:
    TerrainMesh(const std::filesystem::path& heightmapPath, const TerrainConfig& config);
    ~TerrainMesh();

    TerrainMesh(const TerrainMesh&) = delete;
    TerrainMesh& operator=(const TerrainMesh&) = delete;

    void draw() const;

    // Blurred land mask over the heightmap extent (R8, 1 = land), for shallow-water shading.
    // Row 0 is north (-Z); sample at uv = xz / (width, depth) + 0.5.
    void bindCoastMask(GLuint unit) const;

    float verticalExaggeration() const { return m_exaggeration; }
    float width() const { return m_width; }
    float depth() const { return m_depth; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_coastMask = 0;
    GLsizei m_indexCount = 0;
    float m_exaggeration = 1.0f;
    float m_width = 0.0f;
    float m_depth = 0.0f;
};

} // namespace emerald
