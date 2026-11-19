#include "render/Sky.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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
// The moon's disc: bright enough to read as the light source, not to its true contrast.
constexpr float kMoonDiscIntensity = 0.6f;

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

// Mip level read back for the SH: 32 px faces are plenty for order-2 harmonics.
constexpr int kShMipLevel = 3;

// The sky shader's inputs: a refresh starts when any of these change.
bool sameSky(const Atmosphere& a, const Atmosphere& b)
{
    return a.sunDir == b.sunDir && a.exposure == b.exposure; // the moon follows the sun
}

} // namespace

Sky::Sky(const std::filesystem::path& shaderDir, int faceSize)
    : m_skyShader(shaderDir / "fullscreen.vert", shaderDir / "sky.frag")
    , m_skyboxShader(shaderDir / "skybox.vert", shaderDir / "skybox.frag")
    , m_faceSize(faceSize)
    , m_shSize(std::max(faceSize >> kShMipLevel, 1))
{
    // Full-screen triangles come from gl_VertexID; core profile still needs a VAO bound.
    glGenVertexArrays(1, &m_vao);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glGenTextures(2, m_cubemaps.data());
    for (GLuint cubemap : m_cubemaps) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
        for (int level = 0; level <= kShMipLevel; ++level) {
            const int size = std::max(m_faceSize >> level, 1);
            for (int face = 0; face < 6; ++face) {
                glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), level, GL_RGB16F,
                             size, size, 0, GL_RGB, GL_FLOAT, nullptr);
            }
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, kShMipLevel);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    glGenFramebuffers(1, &m_framebuffer);
    glGenBuffers(1, &m_readback);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readback);
    glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(readbackBytes()), nullptr, GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

Sky::~Sky()
{
    if (m_fence) {
        glDeleteSync(m_fence);
    }
    glDeleteBuffers(1, &m_readback);
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(2, m_cubemaps.data());
    glDeleteVertexArrays(1, &m_vao);
}

std::size_t Sky::readbackBytes() const
{
    const auto size = static_cast<std::size_t>(m_shSize);
    return kFaces.size() * size * size * 3 * sizeof(float);
}

void Sky::renderNow(const Atmosphere& atmosphere)
{
    if (m_stage == Stage::Readback) {
        finishReadback(true);
    }
    m_rendering = atmosphere;
    for (std::size_t face = 0; face < kFaces.size(); ++face) {
        renderFace(face);
    }
    startReadback();
    finishReadback(true);
    std::printf("Sky: cubemap rendered (%d px faces), ambient SH updated\n", m_faceSize);
}

void Sky::update(const Atmosphere& atmosphere)
{
    switch (m_stage) {
    case Stage::Idle:
        if (m_valid && sameSky(atmosphere, m_shown)) {
            return;
        }
        m_rendering = atmosphere;
        m_nextFace = 0;
        m_stage = Stage::Faces;
        [[fallthrough]];
    case Stage::Faces:
        renderFace(m_nextFace++);
        if (m_nextFace == kFaces.size()) {
            startReadback();
        }
        return;
    case Stage::Readback:
        finishReadback(false);
        return;
    }
}

void Sky::renderFace(std::size_t face)
{
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face),
                           m_cubemaps[1 - m_front], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        throw std::runtime_error("Sky cubemap framebuffer is incomplete");
    }
    glViewport(0, 0, m_faceSize, m_faceSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_skyShader.use();
    m_skyShader.set("uSunDir", m_rendering.sunDir);
    m_skyShader.set("uMoonDir", m_rendering.moonDir());
    m_skyShader.set("uMoonIntensity", m_rendering.moonSkyIntensity());
    m_skyShader.set("uNightSky", m_rendering.nightSky());
    m_skyShader.set("uHaze", m_rendering.haze());
    m_skyShader.set("uExposure", m_rendering.exposure);
    m_skyShader.set("uEncodeSrgb", 0);
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), kFaces[face].forward, kFaces[face].up);
    m_skyShader.set("uInvViewProjRotation", glm::inverse(projection * view));
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Sky::startReadback()
{
    // Box-filtered mips, then an asynchronous copy of the small level into the pack buffer.
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemaps[1 - m_front]);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readback);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const std::size_t faceBytes = readbackBytes() / kFaces.size();
    for (std::size_t face = 0; face < kFaces.size(); ++face) {
        glGetTexImage(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), kShMipLevel, GL_RGB,
                      GL_FLOAT, reinterpret_cast<void*>(face * faceBytes));
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    m_stage = Stage::Readback;
}

bool Sky::finishReadback(bool wait)
{
    const GLuint64 timeout = wait ? 10'000'000'000ull : 0; // 10 s when waiting
    const GLenum status = glClientWaitSync(m_fence, 0, timeout);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
        if (wait) {
            throw std::runtime_error("Timed out reading back the sky cubemap");
        }
        return false;
    }
    glDeleteSync(m_fence);
    m_fence = nullptr;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readback);
    const auto* texels = static_cast<const float*>(glMapBufferRange(
        GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(readbackBytes()), GL_MAP_READ_BIT));
    if (texels) {
        computeIrradiance(texels);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_front = 1 - m_front;
    m_shown = m_rendering;
    m_valid = true;
    m_stage = Stage::Idle;
    ++m_refreshes;
    return true;
}

// Projects the cubemap onto 9 SH coefficients (texels weighted by solid angle), then applies the
// cosine-lobe convolution (Ramamoorthi and Hanrahan) and divides by pi for diffuse radiance.
void Sky::computeIrradiance(const float* texels)
{
    const auto size = static_cast<std::size_t>(m_shSize);
    std::array<glm::vec3, 9> sh{};
    float totalWeight = 0.0f;

    for (std::size_t face = 0; face < kFaces.size(); ++face) {
        const float* faceTexels = texels + face * size * size * 3;
        for (std::size_t y = 0; y < size; ++y) {
            for (std::size_t x = 0; x < size; ++x) {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
                const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
                const float r2 = 1.0f + u * u + v * v;
                const float weight = 4.0f / (static_cast<float>(size * size) * r2 * std::sqrt(r2));
                const glm::vec3 dir = glm::normalize(faceDirection(face, u, v));

                const std::size_t i = (y * size + x) * 3;
                glm::vec3 radiance(faceTexels[i], faceTexels[i + 1], faceTexels[i + 2]);
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

void Sky::drawBackground(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere,
                         float pixelAngle)
{
    m_skyboxShader.use();
    m_skyboxShader.set("uInvViewProjRotation", invViewProjRotation);
    m_skyboxShader.set("uSunDir", atmosphere.sunDir);
    m_skyboxShader.set("uSunDiscColour", kSunDiscIntensity * atmosphere.exposure *
                                             sunTransmittance(atmosphere.sunDir, atmosphere.haze()));
    m_skyboxShader.set("uMoonDir", atmosphere.moonDir());
    // Silver-white: the air dims a low moon but its colour is kept close to neutral.
    const glm::vec3 moonAir = sunTransmittance(atmosphere.moonDir(), atmosphere.haze());
    const float moonDim = 0.6f + 0.4f * glm::dot(moonAir, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    m_skyboxShader.set("uMoonDiscColour", kMoonDiscIntensity * atmosphere.exposure * moonDim *
                                              glm::vec3(0.93f, 0.96f, 1.0f));
    // Stars come out after the sun is 4 degrees down, fully by the end of nautical twilight.
    m_skyboxShader.set("uStarBrightness", glm::smoothstep(-0.07f, -0.2f, atmosphere.sunDir.y));
    m_skyboxShader.set("uStarRotation", atmosphere.starRotation);
    m_skyboxShader.set("uPixelAngle", pixelAngle);
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
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemaps[m_front]);
}

} // namespace emerald
