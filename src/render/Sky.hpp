#pragma once

#include "render/Atmosphere.hpp"
#include "render/Shader.hpp"

#include <array>
#include <filesystem>

namespace emerald {

// Analytic single-scattering sky (sky.frag), cached in a linear HDR cubemap that is re-rendered
// only when the sun or exposure changes, and drawn as a skybox at the far plane. The cubemap also
// provides diffuse environment lighting (order-2 spherical harmonics) and the fog colour.
class Sky {
public:
    explicit Sky(const std::filesystem::path& shaderDir, int faceSize = 256);
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // Re-renders the cubemap if the atmosphere changed. Returns true when it did.
    bool update(const Atmosphere& atmosphere);

    // Background pass: call after opaque geometry. invViewProjRotation is the inverse of
    // projection * view with translation removed.
    void drawBackground(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere);

    void bindCubemap(GLuint unit) const;

    // Sets uSkyCube (bound to unit) and uIrradianceSH[9] for terrain and sea shading.
    void applyEnvironment(Shader& shader, GLuint unit) const;

private:
    Shader m_skyShader;
    Shader m_skyboxShader;
    GLuint m_vao = 0;
    GLuint m_cubemap = 0;
    GLuint m_framebuffer = 0;
    int m_faceSize = 0;
    bool m_valid = false;
    glm::vec3 m_sunDir{0.0f};
    float m_exposure = 0.0f;
    // Diffuse radiance coefficients: evaluate with the SH basis and multiply by albedo.
    std::array<glm::vec3, 9> m_irradianceSH{};

    void computeIrradiance();
};

} // namespace emerald
