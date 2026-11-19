#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>

namespace emerald {

class Shader;

// Height of the ground under the sea around the camera, for water depth: the terrain rendered
// straight down into two depth layers (near and wide), where depth maps linearly to height, so
// the highest surface wins. Each layer is snapped to whole texels in world space, so the
// shoreline does not crawl as the camera moves.
class SeabedMap {
public:
    static constexpr int kLayers = 2;
    static constexpr int kResolution = 1024;
    static constexpr float kHeightRange = 200.0f; // world units either side of sea level

    SeabedMap();
    ~SeabedMap();

    SeabedMap(const SeabedMap&) = delete;
    SeabedMap& operator=(const SeabedMap&) = delete;

    // Places both layers around the camera; their size follows its height above the sea.
    void update(const glm::dvec3& cameraPos);

    // Renders into one layer; end() restores the framebuffer, viewport and state.
    void begin(int layer);
    void end();
    const glm::mat4& viewProjection(int layer) const { return m_viewProj[static_cast<std::size_t>(layer)]; }
    static float lodScale(int layer) { return layer == 0 ? 1.0f : 0.5f; }

    // Binds the layers to unit and sets uSeabed and uSeabedArea[2] (x, z offset, 1 / half size).
    void apply(Shader& shader, GLuint unit) const;

private:
    GLuint m_texture = 0;
    GLuint m_framebuffer = 0;
    std::array<glm::mat4, kLayers> m_viewProj{};
    std::array<glm::vec4, kLayers> m_area{};
    GLint m_savedFramebuffer = 0;
    GLint m_savedViewport[4] = {};
};

} // namespace emerald
