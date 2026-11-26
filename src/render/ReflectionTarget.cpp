#include "render/ReflectionTarget.hpp"

#include <stdexcept>
#include <string>

namespace emerald {

ReflectionTarget::~ReflectionTarget()
{
    release();
}

void ReflectionTarget::release()
{
    glDeleteFramebuffers(1, &m_framebuffer);
    glDeleteTextures(1, &m_colour);
    glDeleteRenderbuffers(1, &m_depth);
    m_framebuffer = m_colour = m_depth = 0;
}

void ReflectionTarget::resize(int width, int height)
{
    if (width == m_width && height == m_height && m_framebuffer != 0) {
        return;
    }
    release();
    m_width = width;
    m_height = height;

    glGenTextures(1, &m_colour);
    glBindTexture(GL_TEXTURE_2D, m_colour);
    // Linear HDR; 8-bit (clamped) where float render targets are missing.
    if (gl::caps().floatTargets) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colour, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Reflection framebuffer incomplete: 0x" + std::to_string(status));
    }
}

void ReflectionTarget::begin()
{
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_savedFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_savedViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFrontFace(GL_CW); // mirroring reverses the winding
}

void ReflectionTarget::end()
{
    glFrontFace(GL_CCW);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_savedFramebuffer));
    glViewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
}

void ReflectionTarget::bindTexture(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_colour);
}

} // namespace emerald
