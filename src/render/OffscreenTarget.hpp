#pragma once

#include <glad/gl.h>

namespace emerald {

// Colour and depth renderbuffers to render into instead of the window's default framebuffer, for
// --headless. With samples > 1 the scene is drawn multisampled and resolve() blits it into a
// single-sample colour buffer, which is what readFramebuffer() returns for screenshots.
class OffscreenTarget {
public:
    OffscreenTarget(int width, int height, int samples);
    ~OffscreenTarget();

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    // Makes this the framebuffer that draws go to, with a viewport covering it.
    void bind() const;
    // Resolves MSAA into the read framebuffer; does nothing without MSAA. Leaves this bound.
    void resolve() const;
    GLuint readFramebuffer() const { return m_resolveFbo ? m_resolveFbo : m_drawFbo; }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    int m_width;
    int m_height;
    GLuint m_drawFbo = 0;
    GLuint m_colour = 0;
    GLuint m_depth = 0;
    GLuint m_resolveFbo = 0; // only with MSAA
    GLuint m_resolveColour = 0;
};

} // namespace emerald
