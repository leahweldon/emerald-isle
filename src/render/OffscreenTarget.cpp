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

OffscreenTarget::OffscreenTarget(int width, int height, int samples)
    : m_width(width)
    , m_height(height)
{
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    GLsizei msaa = samples < maxSamples ? samples : maxSamples;
    if (msaa < 2) {
        msaa = 0;
    }

    glGenRenderbuffers(1, &m_colour);
    glBindRenderbuffer(GL_RENDERBUFFER, m_colour);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, GL_RGBA8, width, height);
    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, GL_DEPTH_COMPONENT24, width, height);

    glGenFramebuffers(1, &m_drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_drawFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colour);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    checkComplete("draw");

    if (msaa > 1) {
        glGenRenderbuffers(1, &m_resolveColour);
        glBindRenderbuffer(GL_RENDERBUFFER, m_resolveColour);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        glGenFramebuffers(1, &m_resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  m_resolveColour);
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
    glDeleteRenderbuffers(1, &m_colour);
    glDeleteRenderbuffers(1, &m_depth);
    glDeleteRenderbuffers(1, &m_resolveColour);
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

} // namespace emerald
