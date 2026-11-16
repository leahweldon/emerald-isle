#pragma once

#include <glad/gl.h>

namespace emerald {

// Flat quad at y = 0. sea.vert centres it on the camera. Placeholder until the water pass.
class SeaPlane {
public:
    explicit SeaPlane(float halfSize);
    ~SeaPlane();

    SeaPlane(const SeaPlane&) = delete;
    SeaPlane& operator=(const SeaPlane&) = delete;

    void draw() const;

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
};

} // namespace emerald
