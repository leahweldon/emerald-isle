#pragma once

#include "render/Atmosphere.hpp"
#include "render/Shader.hpp"

#include <filesystem>

namespace emerald {

// Analytic single-scattering sky (sky.frag) drawn as a full-screen background.
class Sky {
public:
    explicit Sky(const std::filesystem::path& shaderDir);
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // invViewProjRotation: inverse of projection * view with translation removed.
    void draw(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere);

private:
    Shader m_shader;
    GLuint m_vao = 0;
};

} // namespace emerald
