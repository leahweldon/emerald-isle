#pragma once

#include "platform/Gl.hpp"
#include <glm/glm.hpp>

#include <array>

namespace emerald {

class Shader;

// Cascaded shadow maps for the key light (sun or moon): four slices of the view, each in a layer
// of one depth texture array. Every cascade is fitted to the bounding sphere of its slice, so its
// size does not change as the camera turns, and its centre is snapped to whole shadow texels in
// world space, so shadow edges do not shimmer as the camera moves. Casters between a cascade and
// the light are clamped onto its near plane (depth clamp), so the cascade needs no extra depth.
class ShadowMaps {
public:
    static constexpr int kCascades = 4;
    static constexpr int kResolution = 2048;

    ShadowMaps();
    ~ShadowMaps();

    ShadowMaps(const ShadowMaps&) = delete;
    ShadowMaps& operator=(const ShadowMaps&) = delete;

    // Fits the cascades to the view. heightAboveGround sets how far shadows reach: from a hill
    // top they cover tens of kilometres, from high up over a hundred.
    void update(const glm::dvec3& cameraPos, const glm::vec3& forward, float fovY, float aspect,
                float heightAboveGround, const glm::vec3& lightDir);

    // Renders into one cascade: binds its layer, clears it and sets depth-only state. end()
    // restores the framebuffer, viewport and state that were bound before begin().
    void begin(int cascade);
    void end();

    // Camera-relative position to clip space for one cascade (the depth pass transform).
    const glm::mat4& viewProjection(int cascade) const { return m_viewProj[static_cast<std::size_t>(cascade)]; }
    // Finer than the view's level of detail is wasted in the wide cascades.
    static float lodScale(int cascade);

    // Binds the depth array to unit and sets the receiver uniforms used by shadow.glsl.
    void apply(Shader& shader, GLuint unit) const;

    float distance() const { return m_cascadeEnd[kCascades - 1]; }
    // View depth (metres) where each cascade ends, for the panel's legend.
    float cascadeEnd(int cascade) const { return m_cascadeEnd[static_cast<std::size_t>(cascade)]; }

private:
    GLuint m_texture = 0;
    GLuint m_framebuffer = 0;
    std::array<glm::mat4, kCascades> m_viewProj{};
    std::array<float, kCascades> m_cascadeStart{};
    std::array<float, kCascades> m_cascadeEnd{};
    std::array<float, kCascades> m_texelSize{}; // world metres per shadow texel
    glm::vec3 m_forward{0.0f, 0.0f, -1.0f};
    GLint m_savedFramebuffer = 0;
    GLint m_savedViewport[4] = {};
};

} // namespace emerald
