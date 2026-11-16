#include "render/TerrainMesh.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace emerald {

namespace {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

struct Heightmap {
    int width = 0;
    int height = 0;
    std::vector<float> values; // world-space Y

    float at(int x, int y) const
    {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
};

Heightmap loadHeightmap(const std::filesystem::path& path, const TerrainConfig& config)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_us, void (*)(void*)> pixels(
        stbi_load_16(path.string().c_str(), &width, &height, &channels, 1), stbi_image_free);
    if (!pixels) {
        throw std::runtime_error("Could not load heightmap " + path.string() + ": " +
                                 stbi_failure_reason());
    }

    Heightmap map;
    map.width = width;
    map.height = height;
    map.values.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < map.values.size(); ++i) {
        const stbi_us raw = pixels.get()[i];
        const float metres = raw == 0 ? -config.seaFloorDepth
                                      : static_cast<float>(raw) * config.metresPerUnit;
        map.values[i] = metres * config.verticalExaggeration;
    }
    return map;
}

// Fraction of land per block of factor x factor pixels, then two box-blur passes.
std::vector<unsigned char> buildCoastMask(const Heightmap& map, int factor, int radius,
                                          int& outWidth, int& outHeight)
{
    const int w = (map.width + factor - 1) / factor;
    const int h = (map.height + factor - 1) / factor;
    const auto index = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };

    std::vector<float> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (map.at(x, y) > 0.0f) {
                mask[index(x / factor, y / factor)] += 1.0f / static_cast<float>(factor * factor);
            }
        }
    }

    std::vector<float> temp(mask.size());
    const float norm = 1.0f / static_cast<float>(2 * radius + 1);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    sum += mask[index(std::clamp(x + k, 0, w - 1), y)];
                }
                temp[index(x, y)] = sum * norm;
            }
        }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    sum += temp[index(x, std::clamp(y + k, 0, h - 1))];
                }
                mask[index(x, y)] = sum * norm;
            }
        }
    }

    std::vector<unsigned char> bytes(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        bytes[i] = static_cast<unsigned char>(std::clamp(mask[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    outWidth = w;
    outHeight = h;
    return bytes;
}

} // namespace

TerrainMesh::TerrainMesh(const std::filesystem::path& heightmapPath, const TerrainConfig& config)
    : m_exaggeration(config.verticalExaggeration)
{
    if (config.sampleStep < 1 || config.metresPerPixel <= 0.0f) {
        throw std::invalid_argument("TerrainConfig: sampleStep must be >= 1 and metresPerPixel > 0");
    }

    const Heightmap map = loadHeightmap(heightmapPath, config);
    const int step = config.sampleStep;
    const int cols = (map.width - 1) / step + 1;
    const int rows = (map.height - 1) / step + 1;
    if (cols < 2 || rows < 2) {
        throw std::runtime_error("Heightmap is too small: " + heightmapPath.string());
    }

    const float spacing = config.metresPerPixel;
    m_width = static_cast<float>(map.width - 1) * spacing;
    m_depth = static_cast<float>(map.height - 1) * spacing;
    const float originX = -0.5f * m_width;
    const float originZ = -0.5f * m_depth;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        const int y = std::min(r * step, map.height - 1);
        for (int c = 0; c < cols; ++c) {
            const int x = std::min(c * step, map.width - 1);

            // Central differences over neighbouring samples, in world units.
            const float dx = map.at(x + step, y) - map.at(x - step, y);
            const float dz = map.at(x, y + step) - map.at(x, y - step);
            const float run = 2.0f * static_cast<float>(step) * spacing;

            Vertex v;
            v.position = glm::vec3(originX + static_cast<float>(x) * spacing,
                                   map.at(x, y),
                                   originZ + static_cast<float>(y) * spacing);
            v.normal = glm::normalize(glm::vec3(-dx, run, -dz));
            vertices.push_back(v);
        }
    }

    // Two CCW triangles per cell when viewed from +Y.
    std::vector<GLuint> indices;
    indices.reserve(static_cast<std::size_t>(cols - 1) * static_cast<std::size_t>(rows - 1) * 6);
    for (int r = 0; r + 1 < rows; ++r) {
        for (int c = 0; c + 1 < cols; ++c) {
            const auto topLeft = static_cast<GLuint>(r * cols + c);
            const GLuint topRight = topLeft + 1;
            const auto bottomLeft = static_cast<GLuint>((r + 1) * cols + c);
            const GLuint bottomRight = bottomLeft + 1;
            indices.insert(indices.end(), {topLeft, bottomLeft, topRight,
                                           topRight, bottomLeft, bottomRight});
        }
    }

    m_indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glBindVertexArray(0);

    // About 920 m per texel, blurred over roughly 4-5 km.
    int maskWidth = 0;
    int maskHeight = 0;
    const std::vector<unsigned char> coast = buildCoastMask(map, 4, 2, maskWidth, maskHeight);
    glGenTextures(1, &m_coastMask);
    glBindTexture(GL_TEXTURE_2D, m_coastMask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, maskWidth, maskHeight, 0, GL_RED, GL_UNSIGNED_BYTE,
                 coast.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Outside the heightmap is open sea: border value 0 rather than repeating edge texels.
    const GLfloat border[] = {0.0f, 0.0f, 0.0f, 0.0f};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::printf("Terrain: %dx%d grid, %d triangles\n", cols, rows, m_indexCount / 3);
}

TerrainMesh::~TerrainMesh()
{
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
    glDeleteTextures(1, &m_coastMask);
}

void TerrainMesh::draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void TerrainMesh::bindCoastMask(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_coastMask);
}

} // namespace emerald
