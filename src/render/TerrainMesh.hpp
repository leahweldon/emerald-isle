#pragma once

#include <glad/gl.h>

#include <filesystem>

namespace emerald {

struct TerrainConfig {
    // World units between adjacent heightmap pixels.
    float horizontalSpacing = 1.0f;
    // World height of a full-white (255) pixel.
    float heightScale = 30.0f;
    // Use every Nth pixel in each direction.
    int sampleStep = 2;
};

// Indexed triangle grid built from a greyscale heightmap, centred on the origin in XZ,
// plus a flat sea quad of half-size width() around it.
class TerrainMesh {
public:
    TerrainMesh(const std::filesystem::path& heightmapPath, const TerrainConfig& config);
    ~TerrainMesh();

    TerrainMesh(const TerrainMesh&) = delete;
    TerrainMesh& operator=(const TerrainMesh&) = delete;

    void draw() const;

    float heightScale() const { return m_heightScale; }
    float width() const { return m_width; }
    float depth() const { return m_depth; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_indexCount = 0;
    float m_heightScale = 0.0f;
    float m_width = 0.0f;
    float m_depth = 0.0f;
};

} // namespace emerald
