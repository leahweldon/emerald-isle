#pragma once

#include "render/Atmosphere.hpp"
#include "render/Shader.hpp"

#include <filesystem>

namespace emerald {

// Full-screen sky: horizon-to-zenith gradient by view-ray elevation, with a sun disc.
// Placeholder until the cubemap sky.
class SkyGradient {
public:
    explicit SkyGradient(const std::filesystem::path& shaderDir);
    ~SkyGradient();

    SkyGradient(const SkyGradient&) = delete;
    SkyGradient& operator=(const SkyGradient&) = delete;

    // invViewProjRotation: inverse of projection * view with translation removed.
    void draw(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere);

private:
    Shader m_shader;
    GLuint m_vao = 0;
};

} // namespace emerald
