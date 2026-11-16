#include "render/SkyGradient.hpp"

namespace emerald {

SkyGradient::SkyGradient(const std::filesystem::path& shaderDir)
    : m_shader(shaderDir / "fullscreen.vert", shaderDir / "sky_gradient.frag")
{
    // The full-screen triangle is generated from gl_VertexID in the vertex shader,
    // but a core profile still requires a vertex array object to be bound when drawing.
    glGenVertexArrays(1, &m_vao);
}

SkyGradient::~SkyGradient()
{
    glDeleteVertexArrays(1, &m_vao);
}

void SkyGradient::draw(float timeSeconds, const glm::vec2& resolution)
{
    m_shader.use();
    m_shader.set("uTime", timeSeconds);
    m_shader.set("uResolution", resolution);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace emerald
