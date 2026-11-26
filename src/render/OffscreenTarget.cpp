#include "render/OffscreenTarget.hpp"

#include <stdexcept>
#include <string>

namespace emerald {

namespace {

void checkComplete(const char* name)
{
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Offscreen ") + name + " framebuffer incomplete: 0x" +
                                 std::to_string(status));
    }
}

} // namespace

OffscreenTarget::OffscreenTarget(int width, int height, int samples, GLenum colourFormat)
    : m_width(width)
    , m_height(height)
{
    const GLsizei msaa = gl::samplesFor(colourFormat, samples);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    const gl::PixelTransfer transfer = gl::transferFor(colourFormat);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(colourFormat), width, height, 0, transfer.format,
                 transfer.type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, GL_DEPTH_COMPONENT24, width, height);

    glGenFramebuffers(1, &m_drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_drawFbo);
    if (msaa > 1) {
        glGenRenderbuffers(1, &m_msaaColour);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColour);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, colourFormat, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColour);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    checkComplete("draw");

    if (msaa > 1) {
        glGenFramebuffers(1, &m_resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
        checkComplete("resolve");
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    bind();
}

OffscreenTarget::~OffscreenTarget()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &m_drawFbo);
    glDeleteFramebuffers(1, &m_resolveFbo);
    glDeleteRenderbuffers(1, &m_msaaColour);
    glDeleteRenderbuffers(1, &m_depth);
    glDeleteTextures(1, &m_texture);
}

void OffscreenTarget::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_drawFbo);
    glViewport(0, 0, m_width, m_height);
}

void OffscreenTarget::resolve() const
{
    if (!m_resolveFbo) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_drawFbo);
}

void OffscreenTarget::bindTexture(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

} // namespace emerald
