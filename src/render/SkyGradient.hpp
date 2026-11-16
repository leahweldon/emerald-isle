#pragma once

#include "render/Shader.hpp"

#include <filesystem>

namespace emerald {

// Draws a full-screen sky gradient with a soft sun glow.
// It is a placeholder background for the first milestone and is replaced by a cubemap sky later.
class SkyGradient {
public:
    explicit SkyGradient(const std::filesystem::path& shaderDir);
    ~SkyGradient();

    SkyGradient(const SkyGradient&) = delete;
    SkyGradient& operator=(const SkyGradient&) = delete;

    void draw(float timeSeconds, const glm::vec2& resolution);

private:
    Shader m_shader;
    GLuint m_vao = 0;
};

} // namespace emerald
