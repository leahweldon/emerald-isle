#pragma once

#include "render/Shader.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <filesystem>

namespace emerald {

class Terrain;

// Light reaching the rain, from the atmosphere and the sky's ambient light.
struct RainLighting {
    glm::vec3 lightDir{0.0f, 1.0f, 0.0f};
    glm::vec3 lightColour{0.0f};
    glm::vec3 ambient{0.0f};
    float night = 0.0f;
    float overcast = 0.0f;
    int skyUnit = 0; // texture unit holding the sky cubemap
};

// GPU rain in a box around the camera. Drops and splash droplets live in vertex buffers advanced
// each frame by transform feedback (two buffers per pool, read one and write the other). Their
// positions are world-fixed modulo the box size, so the box wraps about the camera at any speed
// and anywhere in the world. Drops collide with a small height map of the terrain under the box,
// rendered top down from the streamed tiles, and with the sea; landings on the ground throw
// splash droplets. Both pools are drawn as instanced streaks after the opaque scene.
class Rain {
public:
    static constexpr int kMaxDrops = 3 << 16;
    static constexpr int kSplashDroplets = 1 << 15;
    static constexpr int kDropletsPerSplash = 4;
    static constexpr float kHalfWidth = 40.0f;  // metres, box half extent in x and z
    static constexpr float kHalfHeight = 40.0f; // and in y
    // The box centre sits this far below the camera (always 30 m or more above the ground), so
    // landings and splashes are close enough to see when looking down.
    static constexpr float kCentreDrop = 20.0f;

    // groundUnit and dropUnit are texture units the update passes may use.
    Rain(const std::filesystem::path& shaderDir, GLuint groundUnit, GLuint dropUnit);
    ~Rain();

    Rain(const Rain&) = delete;
    Rain& operator=(const Rain&) = delete;

    // Renders the terrain under the box into the height map, through depthShader (terrain.vert
    // with SHADOW_PASS), unless the camera is too far above the ground for drops to reach it.
    void updateGround(Terrain& terrain, Shader& depthShader, GLuint heightTileUnit,
                      const glm::dvec3& cameraPos, double groundUnderCamera);
    // Advances drops and splashes by dt seconds. intensity (0..1) sets how many drops are drawn.
    void simulate(float dt, const glm::dvec3& cameraPos, const glm::vec3& wind, float intensity);
    // Draws into the bound HDR target with depth testing and no depth writes. view has no
    // translation; cameraVelocity (m/s) stretches the streaks while flying.
    void draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec2& viewport,
              float pixelAngle, const glm::vec3& cameraVelocity, const RainLighting& lighting);

    int activeDrops() const { return m_activeDrops; }
    // Transform feedback steps checked with a query, and those that wrote fewer drops than the pool.
    void enableValidation();
    int checkedSteps() const { return m_checkedSteps; }
    int failedSteps() const { return m_failedSteps; }

private:
    void setBoxUniforms(Shader& shader) const;
    void collectQueries();

    Shader m_dropUpdate;
    Shader m_splashUpdate;
    Shader m_dropDraw;
    Shader m_splashDraw;
    GLuint m_groundUnit;
    GLuint m_dropUnit;

    // Ping-pong pairs: m_current holds the latest state.
    std::array<GLuint, 2> m_dropBuffers{};
    std::array<GLuint, 2> m_splashBuffers{};
    std::array<GLuint, 2> m_dropUpdateVaos{};
    std::array<GLuint, 2> m_dropDrawVaos{};
    std::array<GLuint, 2> m_splashUpdateVaos{};
    std::array<GLuint, 2> m_splashDrawVaos{};
    std::array<GLuint, 2> m_dropTextures{}; // buffer textures over m_dropBuffers
    std::size_t m_current = 0;

    GLuint m_groundTexture = 0;
    GLuint m_groundFramebuffer = 0;
    bool m_groundValid = false;

    glm::vec3 m_boxOrigin{0.0f}; // box centre modulo the box size
    float m_cameraHeight = 0.0f;
    int m_activeDrops = 0;
    int m_frame = 0;

    bool m_validating = false;
    std::array<GLuint, 2> m_queries{};
    std::array<bool, 2> m_queryPending{};
    std::size_t m_nextQuery = 0;
    int m_checkedSteps = 0;
    int m_failedSteps = 0;
};

} // namespace emerald
