#pragma once

// OpenGL for both builds: desktop OpenGL 3.3 core through glad, or OpenGL ES 3.0 (WebGL 2) under
// Emscripten. What the two differ in is asked of caps() or goes through the wrappers below, so
// the rest of the renderer has no platform branches of its own.

#ifdef __EMSCRIPTEN__
#define GL_GLEXT_PROTOTYPES
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#else
#include <glad/gl.h>
#endif

#include <string>

// Same values in GL and the GLES extensions that provide them.
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A // desktop only (caps().bufferTextures)
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace emerald::gl {

#ifdef __EMSCRIPTEN__
constexpr bool kGles = true;
#else
constexpr bool kGles = false;
#endif

// What the context supports beyond OpenGL ES 3.0. All true on desktop.
struct Caps {
    bool floatTargets = true;    // rendering to R11G11B10F / RGBA16F / RGBA32F (EXT_color_buffer_float)
    bool floatLinear = true;     // linear filtering of 32-bit float textures (OES_texture_float_linear)
    bool anisotropy = true;      // EXT_texture_filter_anisotropic
    bool timerQueries = true;    // GL_TIME_ELAPSED (EXT_disjoint_timer_query_webgl2)
    bool depthClamp = true;      // GL_DEPTH_CLAMP; without it shaders clamp depth per vertex
    bool bufferTextures = true;  // samplerBuffer; without it rain landings go through a 2D texture
    bool depthLinear = true;     // linear filtering of depth textures sampled as values
    bool wireframe = true;       // glPolygonMode
    int maxSamples = 4;          // multisampling of the HDR target
};

// Probes the current context once; call right after creating it.
void initCaps();
// Before initCaps: act as if every optional WebGL feature were missing (no float targets, float
// filtering, anisotropy or timer queries), to test the fallbacks. No effect on desktop.
void requestFallbacks();
const Caps& caps();

// The shader's first line for this context: "#version 330 core", or "#version 300 es" with
// default precisions, then defines for missing features (GLES, NO_DEPTH_CLAMP, NO_DEPTH_FILTERING,
// NO_BUFFER_TEXTURES, TONEMAP_IN_SCENE), none of them on desktop.
std::string shaderPrelude();

// Query results: whether available, and a GL_TIME_ELAPSED result in nanoseconds.
bool queryAvailable(GLuint query);
GLuint64 queryNanoseconds(GLuint query);
// Points the bound GL_TEXTURE_BUFFER at buffer as RGBA32F texels (only with caps().bufferTextures).
void bufferTextureRgba32f(GLuint buffer);

// Copies a buffer's contents to memory (mapping it, or reading it where mapping is unsupported).
void readBuffer(GLenum target, GLsizeiptr bytes, void* out);
// Mip levels of the bound texture, as the driver allocated them where it can say.
int mipLevels(GLenum target, int width, int height);

// Reads one face of a cubemap at a mip level into the bound GL_PIXEL_PACK_BUFFER at offset; GLES
// reads through scratchFramebuffer, which is left bound.
void readCubeFace(GLuint cubemap, GLenum face, int level, int size, GLenum format, GLenum type, GLintptr offset,
                  GLuint scratchFramebuffer);

// The format and type glTexImage2D needs alongside an internal format (GLES accepts only the
// matching pair; desktop GL any).
struct PixelTransfer {
    GLenum format;
    GLenum type;
};
PixelTransfer transferFor(GLenum internalFormat);
// Most multisample samples for a renderable format, capped at wanted (0 or 1: none).
int samplesFor(GLenum internalFormat, int wanted);

// Filter for a depth texture sampled as values: linear where allowed, else nearest.
GLint depthValueFilter();

void enableSeamlessCubemaps(); // always on in GLES
void enableMultisample(); // the default anyway, where it is not a switch at all (GLES)
void setDepthClamp(bool enabled);
void setWireframe(bool enabled);
// Depth-only framebuffer: no colour reads or writes.
void noColourBuffers();
// Samples outside the texture read colour (a border), or the edge texels where borders are
// unsupported (WebGL); callers keep their edges at that colour.
void clampToBorder(GLenum target, const float colour[4]);

} // namespace emerald::gl
