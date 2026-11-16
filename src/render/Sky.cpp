#include "render/Sky.hpp"

namespace emerald {

Sky::Sky(const std::filesystem::path& shaderDir)
    : m_shader(shaderDir / "fullscreen.vert", shaderDir / "sky.frag")
{
    // Full-screen triangle comes from gl_VertexID; core profile still needs a VAO bound.
    glGenVertexArrays(1, &m_vao);
}

Sky::~Sky()
{
    glDeleteVertexArrays(1, &m_vao);
}

void Sky::draw(const glm::mat4& invViewProjRotation, const Atmosphere& atmosphere)
{
    m_shader.use();
    m_shader.set("uInvViewProjRotation", invViewProjRotation);
    m_shader.set("uSunDir", atmosphere.sunDir);
    m_shader.set("uExposure", atmosphere.exposure);
    m_shader.set("uEncodeSrgb", 1);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace emerald
