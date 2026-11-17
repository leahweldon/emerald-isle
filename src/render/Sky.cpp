#include "render/Sky.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace emerald {

namespace {

struct Face {
    glm::vec3 forward;
    glm::vec3 up;
};

// GL cubemap face order and orientation (+X, -X, +Y, -Y, +Z, -Z).
const std::array<Face, 6> kFaces = {{
    {{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
    {{0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
}};

// Matches kSunIntensity in sky.frag.
constexpr float kSunDiscIntensity = 20.0f;

// Below the horizon the cubemap repeats the horizon colour; as light from the ground it is scaled
// down to stand in for terrain bounce.
constexpr float kGroundBounce = 0.25f;

// Direction through texel (u, v) in [-1, 1] of a GL cubemap face, before normalisation.
glm::vec3 faceDirection(std::size_t face, float u, float v)
{
    switch (face) {
    case 0: return {1.0f, -v, -u};
    case 1: return {-1.0f, -v, u};
    case 2: return {u, 1.0f, v};
    case 3: return {u, -1.0f, -v};
    case 4: return {u, -v, 1.0f};
    default: return {-u, -v, -1.0f};
    }
}

// Real SH basis up to order 2 (9 terms).
std::array<float, 9> shBasis(const glm::vec3& d)
{
    return {0.282095f,
            0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
            1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
            1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y)};
}

} // namespace

Sky::Sky(const std::filesystem::path& shaderDir, int faceSize)
    : m_skyShader(shaderDir / "fullscreen.vert", shaderDir / "sky.frag")
    , m_skyboxShader(shaderDir / "skybox.vert", shaderDir / "skybox.frag")
    , m_faceSize(faceSize)
{
    // Full-screen triangles come from gl_VertexID; core profile still needs a VAO bound.
    glGenVertexArrays(1, &m_vao);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glGenTextures(1, &m_cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGB16F,
                     m_faceSize, m_faceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    glGenFramebuffers(1, &m_framebuffer);
}

Sky::~Sky()
{
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_cubemap);
    glDeleteVertexArrays(1, &m_vao);
}

bool Sky::update(const Atmosphere& atmosphere)
{
    if (m_valid && atmosphere.sunDir == m_sunDir && atmosphere.exposure == m_exposure) {
        return false;
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_faceSize, m_faceSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_skyShader.use();
    m_skyShader.set("uSunDir", atmosphere.sunDir);
    m_skyShader.set("uExposure", atmosphere.exposure);
    m_skyShader.set("uEncodeSrgb", 0);
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glBindVertexArray(m_vao);
    for (std::size_t face = 0; face < kFaces.size(); ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face),
                               m_cubemap, 0);
        if (face == 0 && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
            throw std::runtime_error("Sky cubemap framebuffer is incomplete");
        }
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), kFaces[face].forward, kFaces[face].up);
        m_skyShader.set("uInvViewProjRotation", glm::inverse(projection * view));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    m_valid = true;
    m_sunDir = atmosphere.sunDir;
    m_exposure = atmosphere.exposure;
    computeIrradiance();
    std::printf("Sky: cubemap rendered (%d px faces), ambient SH updated\n", m_faceSize);
    return true;
}

// Projects the cubemap onto 9 SH coefficients (texels weighted by solid angle), then applies the
// cosine-lobe convolution (Ramamoorthi and Hanrahan) and divides by pi for diffuse radiance.
void Sky::computeIrradiance()
{
    const auto size = static_cast<std::size_t>(m_faceSize);
    std::vector<float> texels(size * size * 3);
    std::array<glm::vec3, 9> sh{};
    float totalWeight = 0.0f;

    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (std::size_t face = 0; face < kFaces.size(); ++face) {
        glGetTexImage(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGB, GL_FLOAT,
                      texels.data());
        for (std::size_t y = 0; y < size; ++y) {
            for (std::size_t x = 0; x < size; ++x) {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
                const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
                const float r2 = 1.0f + u * u + v * v;
                const float weight = 4.0f / (static_cast<float>(size * size) * r2 * std::sqrt(r2));
                const glm::vec3 dir = glm::normalize(faceDirection(face, u, v));

                const std::size_t i = (y * size + x) * 3;
                glm::vec3 radiance(texels[i], texels[i + 1], texels[i + 2]);
                if (dir.y < 0.0f) {
                    radiance *= kGroundBounce;
                }
                const std::array<float, 9> basis = shBasis(dir);
                for (std::size_t k = 0; k < 9; ++k) {
                    sh[k] += radiance * basis[k] * weight;
                }
                totalWeight += weight;
            }
        }
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    // Normalise the discrete sum to exactly 4 pi, then convolve with the clamped cosine.
    const float norm = 4.0f * glm::pi<float>() / totalWeight;
    const std::array<float, 9> band = {1.0f,
                                       2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
                                       0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    for (std::size_t k = 0; k < 9; ++k) {
        m_irradianceSH[k] = sh[k] * norm * band[k];
    }
}

void Sky::applyEnvironment(Shader& shader, GLuint unit) const
{
    bindCubemap(unit);
    shader.set("uSkyCube", static_cast<int>(unit));
    for (std::size_t k = 0; k < m_irradianceSH.size(); ++k) {
        shader.set("uIrradianceSH[" + std::to_string(k) + "]", m_irradianceSH[k]);
    }
}

void Sky::drawBackground(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere)
{
    m_skyboxShader.use();
    m_skyboxShader.set("uInvViewProjRotation", invViewProjRotation);
    m_skyboxShader.set("uSunDir", atmosphere.sunDir);
    m_skyboxShader.set("uSunDiscColour", kSunDiscIntensity * atmosphere.exposure *
                                             sunTransmittance(atmosphere.sunDir));
    m_skyboxShader.set("uSkyCube", 0);
    bindCubemap(0);

    // At the far plane: fills only where terrain and sea left the cleared depth.
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void Sky::bindCubemap(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
}

} // namespace emerald
