#pragma once

#include <glad/gl.h>

namespace emerald {

// Render target for the sea's planar reflection: linear HDR colour (alpha marks where terrain was
// drawn; elsewhere the sea falls back to the sky cubemap) and depth, at a fraction of the screen.
class ReflectionTarget {
public:
    ReflectionTarget() = default;
    ~ReflectionTarget();

    ReflectionTarget(const ReflectionTarget&) = delete;
    ReflectionTarget& operator=(const ReflectionTarget&) = delete;

    // (Re)creates the buffers if the size changed.
    void resize(int width, int height);

    // Binds, sets the viewport and clears to transparent; end() restores what was bound before.
    void begin();
    void end();

    void bindTexture(GLuint unit) const;
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    void release();

    int m_width = 0;
    int m_height = 0;
    GLuint m_framebuffer = 0;
    GLuint m_colour = 0;
    GLuint m_depth = 0;
    GLint m_savedFramebuffer = 0;
    GLint m_savedViewport[4] = {};
};

} // namespace emerald
