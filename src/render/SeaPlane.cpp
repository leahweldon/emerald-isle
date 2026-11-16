#include "render/SeaPlane.hpp"

namespace emerald {

SeaPlane::SeaPlane(float halfSize)
{
    // Triangle strip, CCW when viewed from +Y.
    const float h = halfSize;
    const float vertices[] = {
        -h, 0.0f, -h,
        -h, 0.0f, h,
        h, 0.0f, -h,
        h, 0.0f, h,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

SeaPlane::~SeaPlane()
{
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

void SeaPlane::draw() const
{
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

} // namespace emerald
