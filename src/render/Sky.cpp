#include "render/Sky.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdio>
#include <stdexcept>

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
    std::printf("Sky: cubemap rendered (%d px faces)\n", m_faceSize);
    return true;
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
