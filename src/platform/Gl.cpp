#include "platform/Gl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
// WebGL 2's getBufferSubData: provided by Emscripten, not declared in its GLES 3 header.
extern "C" void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data);
#endif

namespace emerald::gl {

namespace {

Caps g_caps;
bool g_forceFallbacks = false;

#ifdef __EMSCRIPTEN__
bool hasExtension(const char* name)
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        // Emscripten reports WebGL extensions with and without a "GL_" prefix.
        if (extension && (std::strcmp(extension, name) == 0 ||
                          (std::strncmp(extension, "GL_", 3) == 0 && std::strcmp(extension + 3, name) == 0))) {
            return true;
        }
    }
    return false;
}
#endif

} // namespace

void initCaps()
{
#ifdef __EMSCRIPTEN__
    g_caps.floatTargets = !g_forceFallbacks && hasExtension("EXT_color_buffer_float");
    g_caps.floatLinear = !g_forceFallbacks && hasExtension("OES_texture_float_linear");
    g_caps.anisotropy = !g_forceFallbacks && hasExtension("EXT_texture_filter_anisotropic");
    g_caps.timerQueries = !g_forceFallbacks && hasExtension("EXT_disjoint_timer_query_webgl2");
    g_caps.depthClamp = false;
    g_caps.bufferTextures = false;
    g_caps.depthLinear = false;
    g_caps.wireframe = false;
    GLint samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    g_caps.maxSamples = samples;
    std::printf("WebGL 2: float targets %s, float filtering %s, anisotropy %s, timer queries %s, MSAA %d\n",
                g_caps.floatTargets ? "yes" : "no", g_caps.floatLinear ? "yes" : "no",
                g_caps.anisotropy ? "yes" : "no", g_caps.timerQueries ? "yes" : "no", samples);
#else
    g_caps.anisotropy = GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic;
#endif
}

void requestFallbacks()
{
    g_forceFallbacks = true;
}

const Caps& caps()
{
    return g_caps;
}

std::string shaderPrelude()
{
#ifdef __EMSCRIPTEN__
    std::string prelude = "#version 300 es\n"
                          "precision highp float;\n"
                          "precision highp int;\n"
                          "precision highp sampler2D;\n"
                          "precision highp sampler2DArray;\n"
                          "precision highp sampler2DArrayShadow;\n"
                          "precision highp samplerCube;\n"
                          "#define GLES\n";
    if (!g_caps.depthClamp) {
        prelude += "#define NO_DEPTH_CLAMP\n";
    }
    if (!g_caps.bufferTextures) {
        prelude += "#define NO_BUFFER_TEXTURES\n";
    }
    if (!g_caps.floatTargets) {
        prelude += "#define TONEMAP_IN_SCENE\n";
    }
    return prelude;
#else
    return "#version 330 core\n";
#endif
}

bool queryAvailable(GLuint query)
{
    GLuint available = GL_FALSE;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
    return available != GL_FALSE;
}

GLuint64 queryNanoseconds(GLuint query)
{
    GLuint64 ns = 0;
#ifdef __EMSCRIPTEN__
    glGetQueryObjectui64vEXT(query, GL_QUERY_RESULT, &ns);
#else
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
#endif
    return ns;
}

void bufferTextureRgba32f([[maybe_unused]] GLuint buffer)
{
#ifndef __EMSCRIPTEN__
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffer);
#endif
}

void readBuffer(GLenum target, GLsizeiptr bytes, void* out)
{
#ifdef __EMSCRIPTEN__
    glGetBufferSubData(target, 0, bytes, out);
#else
    if (const void* data = glMapBufferRange(target, 0, bytes, GL_MAP_READ_BIT)) {
        std::memcpy(out, data, static_cast<std::size_t>(bytes));
        glUnmapBuffer(target);
    }
#endif
}

int mipLevels([[maybe_unused]] GLenum target, int width, int height)
{
#ifdef __EMSCRIPTEN__
    // GLES 3.0 cannot be asked; glGenerateMipmap makes the full chain.
    return static_cast<int>(std::floor(std::log2(std::max(width, height)))) + 1;
#else
    (void)width;
    (void)height;
    int levels = 0;
    for (GLint level = 0;; ++level) {
        GLint levelWidth = 0;
        glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &levelWidth);
        if (levelWidth == 0) {
            break;
        }
        levels = level + 1;
        if (levelWidth == 1) {
            break;
        }
    }
    return levels;
#endif
}

void readCubeFace(GLuint cubemap, GLenum face, int level, [[maybe_unused]] int size, GLenum format, GLenum type,
                  GLintptr offset, [[maybe_unused]] GLuint scratchFramebuffer)
{
#ifdef __EMSCRIPTEN__
    glBindFramebuffer(GL_FRAMEBUFFER, scratchFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, face, cubemap, level);
    glReadPixels(0, 0, size, size, format, type, reinterpret_cast<void*>(offset));
#else
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    glGetTexImage(face, level, format, type, reinterpret_cast<void*>(offset));
#endif
}

PixelTransfer transferFor(GLenum internalFormat)
{
    switch (internalFormat) {
    case GL_R11F_G11F_B10F: return {GL_RGB, GL_FLOAT};
    case GL_RGB16F: return {GL_RGB, GL_FLOAT};
    case GL_RGBA16F: return {GL_RGBA, GL_FLOAT};
    case GL_RGBA32F: return {GL_RGBA, GL_FLOAT};
    case GL_R32F: return {GL_RED, GL_FLOAT};
    case GL_R16F: return {GL_RED, GL_FLOAT};
    default: return {GL_RGBA, GL_UNSIGNED_BYTE};
    }
}

int samplesFor([[maybe_unused]] GLenum internalFormat, int wanted)
{
    GLint most = 0;
#ifdef __EMSCRIPTEN__
    glGetInternalformativ(GL_RENDERBUFFER, internalFormat, GL_SAMPLES, 1, &most);
#else
    glGetIntegerv(GL_MAX_SAMPLES, &most);
#endif
    const int samples = std::min(wanted, static_cast<int>(most));
    return samples < 2 ? 0 : samples;
}

GLint depthValueFilter()
{
    return g_caps.depthLinear ? GL_LINEAR : GL_NEAREST;
}

void enableSeamlessCubemaps()
{
#ifndef __EMSCRIPTEN__
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif
}

void enableMultisample()
{
#ifndef __EMSCRIPTEN__
    glEnable(GL_MULTISAMPLE);
#endif
}

void setDepthClamp([[maybe_unused]] bool enabled)
{
#ifndef __EMSCRIPTEN__
    if (enabled) {
        glEnable(GL_DEPTH_CLAMP);
    } else {
        glDisable(GL_DEPTH_CLAMP);
    }
#endif
}

void setWireframe([[maybe_unused]] bool enabled)
{
#ifndef __EMSCRIPTEN__
    glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
#endif
}

void noColourBuffers()
{
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
}

void clampToBorder(GLenum target, [[maybe_unused]] const float colour[4])
{
#ifdef __EMSCRIPTEN__
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, colour);
#endif
}

} // namespace emerald::gl
