#include "platform/Gl.hpp"

#include <cstdio>
#include <cstring>

namespace emerald::gl {

namespace {

Caps g_caps;

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
    g_caps.floatTargets = hasExtension("EXT_color_buffer_float");
    g_caps.floatLinear = hasExtension("OES_texture_float_linear");
    g_caps.anisotropy = hasExtension("EXT_texture_filter_anisotropic");
    g_caps.timerQueries = hasExtension("EXT_disjoint_timer_query_webgl2");
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
