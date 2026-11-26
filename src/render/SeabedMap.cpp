#include "render/SeabedMap.hpp"

#include "render/Shader.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace emerald {

namespace {

// Half sizes in multiples of the camera's height, within limits (metres).
constexpr float kNearPerHeight = 3.0f;
constexpr float kNearMin = 3e3f;
constexpr float kNearMax = 30e3f;
constexpr float kWidePerHeight = 30.0f;
constexpr float kWideMin = 40e3f;
constexpr float kWideMax = 400e3f;

} // namespace

SeabedMap::SeabedMap()
{
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, kResolution, kResolution, kLayers, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texture, 0, 0);
    gl::noColourBuffers();
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Seabed framebuffer incomplete: 0x" + std::to_string(status));
    }
}

SeabedMap::~SeabedMap()
{
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_texture);
}

void SeabedMap::update(const glm::dvec3& cameraPos)
{
    // Height in half-octave steps, so the layers (and their texels) only resize now and then.
    const float height = std::exp2(
        std::round(std::log2(std::max(static_cast<float>(cameraPos.y), 1.0f)) * 2.0f) * 0.5f);
    const float halfSizes[kLayers] = {std::clamp(kNearPerHeight * height, kNearMin, kNearMax),
                                      std::clamp(kWidePerHeight * height, kWideMin, kWideMax)};
    for (int i = 0; i < kLayers; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const float r = halfSizes[i];
        const double texel = 2.0 * static_cast<double>(r) / kResolution;
        // Offset from the camera to the snapped layer centre, in double, then small enough for float.
        const auto ox = static_cast<float>(cameraPos.x - std::round(cameraPos.x / texel) * texel);
        const auto oz = static_cast<float>(cameraPos.z - std::round(cameraPos.z / texel) * texel);
        // Camera-relative p: x = (p.x + ox) / r, y = (p.z + oz) / r, depth = -(world height) / range.
        glm::mat4 m(0.0f);
        m[0][0] = 1.0f / r;
        m[2][1] = 1.0f / r;
        m[1][2] = -1.0f / kHeightRange;
        m[3][0] = ox / r;
        m[3][1] = oz / r;
        m[3][2] = -static_cast<float>(cameraPos.y) / kHeightRange;
        m[3][3] = 1.0f;
        m_viewProj[index] = m;
        m_area[index] = glm::vec4(ox, oz, 1.0f / r, 0.0f);
    }
}

void SeabedMap::begin(int layer)
{
    if (layer == 0) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_savedFramebuffer);
        glGetIntegerv(GL_VIEWPORT, m_savedViewport);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texture, 0, layer);
    glViewport(0, 0, kResolution, kResolution);
    glClear(GL_DEPTH_BUFFER_BIT); // cleared to 1: the bottom of the range, open deep sea
    glDisable(GL_CULL_FACE);
    gl::setDepthClamp(true); // heights beyond the range clamp to its ends
}

void SeabedMap::end()
{
    gl::setDepthClamp(false);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_savedFramebuffer));
    glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
}

void SeabedMap::apply(Shader& shader, GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    shader.set("uSeabed", static_cast<int>(unit));
    for (int i = 0; i < kLayers; ++i) {
        shader.set("uSeabedArea[" + std::to_string(i) + "]", m_area[static_cast<std::size_t>(i)]);
    }
    shader.set("uSeabedRange", kHeightRange);
}

} // namespace emerald
