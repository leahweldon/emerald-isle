#include "render/Rain.hpp"

#include "render/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace emerald {

namespace {

constexpr int kGroundResolution = 128; // texels across the box: ~0.6 m, finer than any tile
// The height map covers the box's height and a little more about its centre, so a cleared map
// (depth 1) lies below the box and nothing lands on it.
constexpr float kGroundRange = 1.05f * Rain::kHalfHeight;
// Above this height over the ground under the camera, no slope within the box reaches it.
constexpr double kGroundMargin = 300.0;
constexpr float kMaxStep = 0.05f;     // seconds; longer frames are simulated slower than real time
constexpr float kStreakTime = 0.05f;  // seconds of motion in each streak, like a camera shutter
constexpr float kDropOpacity = 0.6f;
constexpr float kSplashOpacity = 0.6f;
constexpr int kDropVec4s = 3;         // position, velocity, hit
constexpr int kSplashVec4s = 2;       // position, velocity
// Drops as a 2D texture of RGBA32F texels where buffer textures are missing; matches
// splash_update.vert.
constexpr int kDropTextureWidth = 1024;
constexpr int kDropTextureHeight = Rain::kMaxDrops * kDropVec4s / kDropTextureWidth;
static_assert(Rain::kMaxDrops * kDropVec4s % kDropTextureWidth == 0, "drops fill whole texture rows");

// Gunn and Kinzer as fitted by Atlas; matches rain_common.glsl.
float terminalVelocity(float diameterMm)
{
    return std::max(9.65f - 10.3f * std::exp(-0.6f * diameterMm), 0.5f);
}

void setupAttributes(GLuint vao, GLuint buffer, int vec4s, bool instanced)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const auto stride = static_cast<GLsizei>(static_cast<std::size_t>(vec4s) * 4 * sizeof(float));
    for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(static_cast<std::size_t>(i) * 4 * sizeof(float)));
        glVertexAttribDivisor(i, instanced ? 1 : 0);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace

Rain::Rain(const std::filesystem::path& shaderDir, GLuint groundUnit, GLuint dropUnit)
    : m_dropUpdate(shaderDir / "rain_update.vert", std::vector<std::string>{"oPosition", "oVelocity", "oHit"})
    , m_splashUpdate(shaderDir / "splash_update.vert", std::vector<std::string>{"oPosition", "oVelocity"})
    , m_dropDraw(shaderDir / "rain.vert", shaderDir / "rain.frag")
    , m_splashDraw(shaderDir / "rain.vert", shaderDir / "rain.frag", "#define SPLASH\n")
    , m_groundUnit(groundUnit)
    , m_dropUnit(dropUnit)
{
    // Drops fill the box evenly, falling at their terminal velocity. Sizes follow an exponential
    // (Marshall-Palmer-like) distribution: mostly small drops, a few large ones.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::vec3 size(2.0f * kHalfWidth, 2.0f * kHalfHeight, 2.0f * kHalfWidth);
    std::vector<glm::vec4> drops(static_cast<std::size_t>(kMaxDrops * kDropVec4s), glm::vec4(0.0f));
    for (std::size_t i = 0; i < static_cast<std::size_t>(kMaxDrops); ++i) {
        const float diameter = std::min(0.5f - std::log(1.0f - unit(rng)), 5.0f);
        drops[i * std::size_t{kDropVec4s}] = glm::vec4(unit(rng) * size.x, unit(rng) * size.y, unit(rng) * size.z, diameter);
        drops[i * std::size_t{kDropVec4s} + 1] = glm::vec4(0.0f, -terminalVelocity(diameter), 0.0f, 0.0f);
    }
    const std::vector<glm::vec4> splashes(static_cast<std::size_t>(kSplashDroplets * kSplashVec4s), glm::vec4(0.0f));

    glGenBuffers(2, m_dropBuffers.data());
    glGenBuffers(2, m_splashBuffers.data());
    glGenVertexArrays(2, m_dropUpdateVaos.data());
    glGenVertexArrays(2, m_dropDrawVaos.data());
    glGenVertexArrays(2, m_splashUpdateVaos.data());
    glGenVertexArrays(2, m_splashDrawVaos.data());
    glGenTextures(2, m_dropTextures.data());
    for (std::size_t i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, m_dropBuffers[i]);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(drops.size() * sizeof(glm::vec4)), drops.data(),
                     GL_DYNAMIC_COPY);
        glBindBuffer(GL_ARRAY_BUFFER, m_splashBuffers[i]);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(splashes.size() * sizeof(glm::vec4)),
                     splashes.data(), GL_DYNAMIC_COPY);
        setupAttributes(m_dropUpdateVaos[i], m_dropBuffers[i], kDropVec4s, false);
        setupAttributes(m_dropDrawVaos[i], m_dropBuffers[i], kDropVec4s, true);
        setupAttributes(m_splashUpdateVaos[i], m_splashBuffers[i], kSplashVec4s, false);
        setupAttributes(m_splashDrawVaos[i], m_splashBuffers[i], kSplashVec4s, true);
        if (gl::caps().bufferTextures) {
            glBindTexture(GL_TEXTURE_BUFFER, m_dropTextures[i]);
            gl::bufferTextureRgba32f(m_dropBuffers[i]);
            glBindTexture(GL_TEXTURE_BUFFER, 0);
        }
    }
    if (!gl::caps().bufferTextures) {
        // No buffer textures (WebGL): landings are copied into a 2D texture each step instead.
        glBindTexture(GL_TEXTURE_2D, m_dropTextures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kDropTextureWidth, kDropTextureHeight, 0, GL_RGBA, GL_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &m_groundTexture);
    glBindTexture(GL_TEXTURE_2D, m_groundTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, kGroundResolution, kGroundResolution, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl::depthValueFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl::depthValueFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &m_groundFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_groundFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_groundTexture, 0);
    gl::noColourBuffers();
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Rain ground framebuffer incomplete: 0x" + std::to_string(status));
    }

    for (Shader* shader : {&m_dropUpdate, &m_splashUpdate}) {
        shader->use();
        shader->set("uGround", static_cast<int>(m_groundUnit));
        shader->set("uGroundRange", kGroundRange);
    }
    m_splashUpdate.set("uDrops", static_cast<int>(m_dropUnit));
    m_splashUpdate.set("uGroups", kSplashDroplets / kDropletsPerSplash);
    std::printf("Rain: %d drops, %d splash droplets, %.0f x %.0f x %.0f m box\n", kMaxDrops,
                kSplashDroplets, static_cast<double>(size.x), static_cast<double>(size.y),
                static_cast<double>(size.z));
}

Rain::~Rain()
{
    if (m_validating) {
        glDeleteQueries(2, m_queries.data());
    }
    glDeleteFramebuffers(1, &m_groundFramebuffer);
    glDeleteTextures(1, &m_groundTexture);
    glDeleteTextures(2, m_dropTextures.data());
    glDeleteVertexArrays(2, m_splashDrawVaos.data());
    glDeleteVertexArrays(2, m_splashUpdateVaos.data());
    glDeleteVertexArrays(2, m_dropDrawVaos.data());
    glDeleteVertexArrays(2, m_dropUpdateVaos.data());
    glDeleteBuffers(2, m_splashBuffers.data());
    glDeleteBuffers(2, m_dropBuffers.data());
}

void Rain::enableValidation()
{
    if (!m_validating) {
        glGenQueries(2, m_queries.data());
        m_validating = true;
    }
}

void Rain::setBoxUniforms(Shader& shader) const
{
    shader.set("uBoxOrigin", m_boxOrigin);
    shader.set("uBoxCentre", glm::vec3(0.0f, -kCentreDrop, 0.0f));
    shader.set("uHalfSize", glm::vec3(kHalfWidth, kHalfHeight, kHalfWidth));
    shader.set("uCameraHeight", m_cameraHeight);
    shader.set("uGroundValid", m_groundValid ? 1 : 0);
}

void Rain::updateGround(Terrain& terrain, Shader& depthShader, GLuint heightTileUnit,
                        const glm::dvec3& cameraPos, double groundUnderCamera)
{
    m_groundValid = cameraPos.y - groundUnderCamera < static_cast<double>(kHalfHeight + kCentreDrop) + kGroundMargin;
    if (!m_groundValid) {
        return;
    }
    // Camera-relative p: x = p.x / half width, y = p.z / half width, depth = -(p.y + drop) / range,
    // so depth maps linearly to height about the box centre and the highest surface wins.
    glm::mat4 viewProj(0.0f);
    viewProj[0][0] = 1.0f / kHalfWidth;
    viewProj[2][1] = 1.0f / kHalfWidth;
    viewProj[1][2] = -1.0f / kGroundRange;
    viewProj[3][2] = -kCentreDrop / kGroundRange;
    viewProj[3][3] = 1.0f;

    GLint previous = 0;
    GLint viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, m_groundFramebuffer);
    glViewport(0, 0, kGroundResolution, kGroundResolution);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    gl::setDepthClamp(true); // ground above or below the range clamps to its ends
    depthShader.use();
    depthShader.set("uCameraHeight", static_cast<float>(cameraPos.y));
    depthShader.set("uShadowViewProj", viewProj);
    terrain.drawShadowCasters(depthShader, heightTileUnit, viewProj, 1.0f);
    gl::setDepthClamp(false);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void Rain::simulate(float dt, const glm::dvec3& cameraPos, const glm::vec3& wind, float intensity)
{
    m_activeDrops = static_cast<int>(std::lround(static_cast<double>(kMaxDrops) * std::clamp(intensity, 0.0f, 1.0f)));
    const glm::dvec3 size(2.0 * kHalfWidth, 2.0 * kHalfHeight, 2.0 * kHalfWidth);
    const glm::dvec3 centre = cameraPos - glm::dvec3(0.0, kCentreDrop, 0.0);
    m_boxOrigin = glm::vec3(centre - glm::floor(centre / size) * size);
    m_cameraHeight = static_cast<float>(cameraPos.y);
    // A still step changes nothing, and must not advance the seeds: screenshot runs hold the rain
    // for however many frames the terrain takes to load, and still capture the same image.
    if (m_activeDrops == 0 || dt <= 0.0f) {
        return;
    }
    ++m_frame;
    const float step = std::min(dt, kMaxStep);
    const std::size_t next = 1 - m_current;
    if (m_validating) {
        collectQueries();
    }

    glEnable(GL_RASTERIZER_DISCARD);
    glActiveTexture(GL_TEXTURE0 + m_groundUnit);
    glBindTexture(GL_TEXTURE_2D, m_groundTexture);

    m_dropUpdate.use();
    setBoxUniforms(m_dropUpdate);
    m_dropUpdate.set("uDt", step);
    m_dropUpdate.set("uWind", wind);
    m_dropUpdate.set("uActiveDrops", m_activeDrops);
    m_dropUpdate.set("uFrame", m_frame);
    glBindVertexArray(m_dropUpdateVaos[m_current]);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, m_dropBuffers[next]);
    const bool query = m_validating && !m_queryPending[m_nextQuery];
    if (query) {
        glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, m_queries[m_nextQuery]);
    }
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, kMaxDrops);
    glEndTransformFeedback();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    if (query) {
        glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
        m_queryPending[m_nextQuery] = true;
        m_nextQuery = 1 - m_nextQuery;
    }
    // Without buffer textures, a GPU-side copy of the drops into a texture; only when something
    // can land, which is also the only time the splash pass reads them.
    const GLenum dropTarget = gl::caps().bufferTextures ? GL_TEXTURE_BUFFER : GL_TEXTURE_2D;
    const GLuint dropTexture = gl::caps().bufferTextures ? m_dropTextures[next] : m_dropTextures[0];
    if (!gl::caps().bufferTextures && m_groundValid) {
        glBindTexture(GL_TEXTURE_2D, dropTexture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_dropBuffers[next]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kDropTextureWidth, kDropTextureHeight, GL_RGBA, GL_FLOAT, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    // Splashes read this step's landings from the drops just written.
    m_splashUpdate.use();
    setBoxUniforms(m_splashUpdate);
    m_splashUpdate.set("uDt", step);
    m_splashUpdate.set("uActiveDrops", m_activeDrops);
    m_splashUpdate.set("uFrame", m_frame);
    glActiveTexture(GL_TEXTURE0 + m_dropUnit);
    glBindTexture(dropTarget, dropTexture);
    glBindVertexArray(m_splashUpdateVaos[m_current]);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, m_splashBuffers[next]);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, kSplashDroplets);
    glEndTransformFeedback();

    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glBindTexture(dropTarget, 0);
    glBindVertexArray(0);
    glDisable(GL_RASTERIZER_DISCARD);
    m_current = next;
}

void Rain::collectQueries()
{
    for (std::size_t i = 0; i < 2; ++i) {
        if (!m_queryPending[i]) {
            continue;
        }
        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(m_queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint written = 0;
            glGetQueryObjectuiv(m_queries[i], GL_QUERY_RESULT, &written);
            ++m_checkedSteps;
            m_failedSteps += written == static_cast<GLuint>(kMaxDrops) ? 0 : 1;
            m_queryPending[i] = false;
        }
    }
}

void Rain::draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec2& viewport,
                float pixelAngle, const glm::vec3& cameraVelocity, const RainLighting& lighting)
{
    if (m_activeDrops == 0) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // drops pass light on to the eye; they never darken the scene
    glDepthMask(GL_FALSE);
    // Drops nearer than the scene's near plane (often hundreds of metres from a high camera) are
    // clamped onto it rather than clipped, so they pass the depth test in front of everything.
    gl::setDepthClamp(true);
    glDisable(GL_CULL_FACE);

    struct Pass {
        Shader* shader;
        GLuint vao;
        int count;
        float opacity;
    };
    const Pass passes[] = {{&m_dropDraw, m_dropDrawVaos[m_current], m_activeDrops, kDropOpacity},
                           {&m_splashDraw, m_splashDrawVaos[m_current], kSplashDroplets, kSplashOpacity}};
    for (const Pass& pass : passes) {
        Shader& shader = *pass.shader;
        shader.use();
        setBoxUniforms(shader);
        shader.set("uView", view);
        shader.set("uProjection", projection);
        shader.set("uViewportSize", viewport);
        shader.set("uPixelAngle", pixelAngle);
        shader.set("uTanHalfFov", glm::vec2(1.0f / projection[0][0], 1.0f / projection[1][1]));
        shader.set("uCameraVelocity", cameraVelocity);
        shader.set("uStreakTime", kStreakTime);
        shader.set("uOpacity", pass.opacity);
        shader.set("uLightDir", lighting.lightDir);
        shader.set("uLightColour", lighting.lightColour);
        shader.set("uAmbient", lighting.ambient);
        shader.set("uNight", lighting.night);
        shader.set("uOvercast", lighting.overcast);
        shader.set("uDisplayExposure", lighting.displayExposure);
        shader.set("uDisplayToneMapper", lighting.displayToneMapper);
        shader.set("uSkyCube", lighting.skyUnit);
        glBindVertexArray(pass.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, pass.count);
    }
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    gl::setDepthClamp(false);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace emerald
