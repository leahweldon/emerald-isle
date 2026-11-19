#pragma once

#include <glad/gl.h>

namespace emerald {

// Colour and depth to render into instead of the window's default framebuffer: the HDR scene
// target (RGBA16F, multisampled) and, for --headless, the final image (RGBA8). With samples > 1
// the scene is drawn multisampled and resolve() averages it into a single-sample texture, which
// is what texture() and readFramebuffer() give.
class OffscreenTarget {
public:
    OffscreenTarget(int width, int height, int samples, GLenum colourFormat = GL_RGBA8);
    ~OffscreenTarget();

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    // Makes this the framebuffer that draws go to, with a viewport covering it.
    void bind() const;
    // Resolves MSAA into the texture; does nothing without MSAA. Leaves this bound.
    void resolve() const;
    GLuint readFramebuffer() const { return m_resolveFbo ? m_resolveFbo : m_drawFbo; }
    void bindTexture(GLuint unit) const;

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    int m_width;
    int m_height;
    GLuint m_drawFbo = 0;
    GLuint m_msaaColour = 0; // multisampled renderbuffer, only with MSAA
    GLuint m_depth = 0;
    GLuint m_resolveFbo = 0; // only with MSAA
    GLuint m_texture = 0;    // single-sample colour: the resolve target, or drawn into directly
};

} // namespace emerald
