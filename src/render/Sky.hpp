#pragma once

#include "render/Atmosphere.hpp"
#include "render/Shader.hpp"

#include <array>
#include <filesystem>

namespace emerald {

// Analytic single-scattering sky (sky.frag), cached in a linear HDR cubemap and drawn as a skybox
// at the far plane. The cubemap also provides diffuse environment lighting (order-2 spherical
// harmonics) and the fog colour.
//
// When the sun moves, the new sky is rendered into a second cubemap one face per frame, its
// mip level 3 (32 px faces) is read back asynchronously for the SH, and both are swapped in
// together once the readback is done, so a refresh never stalls a frame.
class Sky {
public:
    explicit Sky(const std::filesystem::path& shaderDir, int faceSize = 256);
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // Renders every face and the SH immediately, waiting for the GPU. For startup.
    void renderNow(const Atmosphere& atmosphere);
    // Advances an amortised refresh by at most one face if the atmosphere has changed.
    void update(const Atmosphere& atmosphere);

    // Background pass: call after opaque geometry. invViewProjRotation is the inverse of
    // projection * view with translation removed; pixelAngle (radians) sizes the stars.
    void drawBackground(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere,
                        float pixelAngle);

    void bindCubemap(GLuint unit) const;
    // Exposure and tone curve for shaders that tone map themselves (TONEMAP_IN_SCENE).
    void setDisplay(float exposure, int toneMapper)
    {
        m_displayExposure = exposure;
        m_displayToneMapper = toneMapper;
    }

    // Sets uSkyCube (bound to unit) and uIrradianceSH[9] for terrain and sea shading.
    void applyEnvironment(Shader& shader, GLuint unit) const;

    // Diffuse sky light averaged over all directions (the constant SH term), for particles.
    glm::vec3 averageIrradiance() const { return m_irradianceSH[0] * 0.282095f; }

    int refreshes() const { return m_refreshes; } // cubemaps swapped in since startup

private:
    enum class Stage { Idle, Faces, Readback };

    void renderFace(std::size_t face);
    void startReadback();
    // Computes the SH and swaps the cubemaps once the readback is done. Returns false if the
    // GPU has not finished and wait is false.
    bool finishReadback(bool wait);
    // texels: six faces at the SH mip level, channels floats per texel.
    void computeIrradiance(const float* texels, std::size_t channels);
    std::size_t readbackBytes() const;

    Shader m_skyShader;
    Shader m_skyboxShader;
    GLuint m_vao = 0;
    std::array<GLuint, 2> m_cubemaps{};
    std::size_t m_front = 0; // the cubemap being displayed; the other one is being refreshed
    GLuint m_framebuffer = 0;
    GLuint m_readback = 0;   // pixel pack buffer for the SH faces
    GLsync m_fence = nullptr;
    int m_faceSize = 0;
    int m_shSize = 0;        // face size of the mip level read back for the SH
    // Cubemap format and how it is read back: RGB floats on desktop; RGBA (GLES cannot render to
    // RGB16F), as bytes where float targets are unsupported.
    GLenum m_internalFormat = GL_RGB16F;
    GLenum m_readFormat = GL_RGB;
    GLenum m_readType = GL_FLOAT;
    std::size_t m_readChannels = 3;

    Stage m_stage = Stage::Idle;
    std::size_t m_nextFace = 0;
    bool m_valid = false;
    Atmosphere m_shown;      // inputs of the displayed cubemap
    Atmosphere m_rendering;  // inputs of the cubemap being refreshed
    int m_refreshes = 0;
    float m_displayExposure = 1.0f;
    int m_displayToneMapper = 0;
    // Diffuse radiance coefficients: evaluate with the SH basis and multiply by albedo.
    std::array<glm::vec3, 9> m_irradianceSH{};
};

} // namespace emerald
