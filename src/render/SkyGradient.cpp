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

void SkyGradient::draw(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere)
{
    m_shader.use();
    m_shader.set("uInvViewProjRotation", invViewProjRotation);
    atmosphere.apply(m_shader);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace emerald
