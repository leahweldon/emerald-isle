#include "render/ShadowMaps.hpp"

#include "render/Shader.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace emerald {

namespace {

// Shadows reach this many times the camera's height above the ground, within these limits.
constexpr float kReachPerHeight = 10.0f;
constexpr float kMinReach = 30e3f;
constexpr float kMaxReach = 150e3f;
// Split scheme: mostly logarithmic (even texel density on screen), partly uniform.
constexpr float kLogWeight = 0.85f;
// Bias for the depth pass, in polygon offset units (slope factor, constant).
constexpr float kSlopeBias = 2.0f;
constexpr float kConstantBias = 2.0f;

} // namespace

ShadowMaps::ShadowMaps(int resolution)
    : m_resolution(resolution)
{
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_resolution, m_resolution, kCascades, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    // Hardware comparison with bilinear filtering: each lookup is already a 2 x 2 PCF.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    // Outside a cascade is lit.
    const GLfloat border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    gl::clampToBorder(GL_TEXTURE_2D_ARRAY, border);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texture, 0, 0);
    gl::noColourBuffers();
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
        throw std::runtime_error("Shadow map framebuffer incomplete: 0x" + std::to_string(status));
    }
    // Start fully lit, for frames that skip the passes (no direct light) before any are drawn.
    for (int i = 0; i < kCascades; ++i) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texture, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
}

ShadowMaps::~ShadowMaps()
{
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_texture);
}

float ShadowMaps::lodScale(int cascade)
{
    constexpr std::array<float, kCascades> kScale = {1.0f, 1.0f, 0.5f, 0.25f};
    return kScale[static_cast<std::size_t>(cascade)];
}

void ShadowMaps::update(const glm::dvec3& cameraPos, const glm::vec3& forward, float fovY, float aspect,
                        float heightAboveGround, const glm::vec3& lightDir)
{
    m_forward = forward;

    // Height in half-octave steps, so the splits (and texel sizes) only change now and then.
    const float height = std::exp2(std::round(std::log2(std::max(heightAboveGround, 1.0f)) * 2.0f) * 0.5f);
    const float nearest = 0.5f * height;
    const float farthest = nearest + std::clamp(kReachPerHeight * height, kMinReach, kMaxReach);

    // Light basis: L towards the light, R and U across the shadow map.
    const glm::vec3 L = glm::normalize(lightDir);
    const glm::vec3 upHint = std::abs(L.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 R = glm::normalize(glm::cross(upHint, L));
    const glm::vec3 U = glm::cross(L, R);

    const float tanY = std::tan(0.5f * fovY);
    const float k2 = tanY * tanY * (1.0f + aspect * aspect); // squared slope of the frustum corners
    for (int i = 0; i < kCascades; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const float t0 = static_cast<float>(i) / kCascades;
        const float t1 = static_cast<float>(i + 1) / kCascades;
        const auto split = [&](float t) {
            return kLogWeight * nearest * std::pow(farthest / nearest, t) +
                   (1.0f - kLogWeight) * (nearest + (farthest - nearest) * t);
        };
        const float n = i == 0 ? nearest : split(t0);
        const float f = split(t1);
        m_cascadeStart[index] = n;
        m_cascadeEnd[index] = f;

        // Smallest sphere through the slice's eight corners: centred on the view axis.
        const float centreDepth = std::min(0.5f * (n + f) * (1.0f + k2), f);
        const float radius = std::sqrt((f - centreDepth) * (f - centreDepth) + f * f * k2);
        const glm::vec3 centre = forward * centreDepth;
        const double texel = 2.0 * static_cast<double>(radius) / m_resolution;
        m_texelSize[index] = static_cast<float>(texel);

        // Snap the centre to the texel grid in world space; the offsets stay small in float.
        const glm::dvec3 dR(R);
        const glm::dvec3 dU(U);
        const double worldX = glm::dot(cameraPos, dR) + static_cast<double>(glm::dot(centre, R));
        const double worldY = glm::dot(cameraPos, dU) + static_cast<double>(glm::dot(centre, U));
        const auto offsetX = static_cast<float>(glm::dot(cameraPos, dR) - std::round(worldX / texel) * texel);
        const auto offsetY = static_cast<float>(glm::dot(cameraPos, dU) - std::round(worldY / texel) * texel);

        // Rows: x = (p.R + offsetX) / r, y = (p.U + offsetY) / r, z = -(p - centre).L / r.
        glm::mat4 m(0.0f);
        for (int c = 0; c < 3; ++c) {
            m[c][0] = R[c] / radius;
            m[c][1] = U[c] / radius;
            m[c][2] = -L[c] / radius;
        }
        m[3][0] = offsetX / radius;
        m[3][1] = offsetY / radius;
        m[3][2] = glm::dot(centre, L) / radius;
        m[3][3] = 1.0f;
        m_viewProj[index] = m;
    }
}

void ShadowMaps::begin(int cascade)
{
    if (cascade == 0) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_savedFramebuffer);
        glGetIntegerv(GL_VIEWPORT, m_savedViewport);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texture, 0, cascade);
    glViewport(0, 0, m_resolution, m_resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
    // Both faces: a heightfield seen from the light shows its back on slopes facing away.
    glDisable(GL_CULL_FACE);
    gl::setDepthClamp(true);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(kSlopeBias, kConstantBias);
}

void ShadowMaps::end()
{
    glDisable(GL_POLYGON_OFFSET_FILL);
    gl::setDepthClamp(false);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_savedFramebuffer));
    glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
}

void ShadowMaps::apply(Shader& shader, GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    shader.set("uShadowMap", static_cast<int>(unit));
    // Clip space to texture coordinates and depth in [0, 1].
    const glm::mat4 bias(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f,
                         0.5f, 0.5f, 0.5f, 1.0f);
    for (int i = 0; i < kCascades; ++i) {
        shader.set("uShadowMatrices[" + std::to_string(i) + "]", bias * m_viewProj[static_cast<std::size_t>(i)]);
    }
    shader.set("uCascadeStart", glm::vec4(m_cascadeStart[0], m_cascadeStart[1], m_cascadeStart[2], m_cascadeStart[3]));
    shader.set("uCascadeEnd", glm::vec4(m_cascadeEnd[0], m_cascadeEnd[1], m_cascadeEnd[2], m_cascadeEnd[3]));
    shader.set("uShadowTexel", glm::vec4(m_texelSize[0], m_texelSize[1], m_texelSize[2], m_texelSize[3]));
    shader.set("uShadowResolution", static_cast<float>(m_resolution));
    shader.set("uViewForward", m_forward);
}

} // namespace emerald
