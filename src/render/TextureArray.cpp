#include "render/TextureArray.hpp"

#include <stb_image.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

namespace emerald {

namespace {

constexpr float kMaxAnisotropy = 8.0f;

using Pixels = std::unique_ptr<stbi_uc, void (*)(void*)>;

Pixels loadRgba(const std::filesystem::path& path, int& width, int& height)
{
    // GL textures start at the bottom row; NormalGL maps assume that orientation.
    stbi_set_flip_vertically_on_load(1);
    int channels = 0;
    Pixels pixels(stbi_load(path.string().c_str(), &width, &height, &channels, 4), stbi_image_free);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) {
        throw std::runtime_error("Could not load texture " + path.string() + ": " +
                                 stbi_failure_reason());
    }
    return pixels;
}

} // namespace

TextureArray::TextureArray(const std::vector<std::filesystem::path>& layers, Encoding encoding)
{
    const GLint internalFormat = encoding == Encoding::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;

    if (layers.empty()) {
        throw std::invalid_argument("TextureArray needs at least one layer");
    }

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int width = 0;
    int height = 0;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        int w = 0;
        int h = 0;
        const Pixels pixels = loadRgba(layers[i], w, h);
        if (i == 0) {
            width = w;
            height = h;
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, width, height,
                         static_cast<GLsizei>(layers.size()), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        } else if (w != width || h != height) {
            glDeleteTextures(1, &m_texture);
            throw std::runtime_error("Texture " + layers[i].string() + " is " + std::to_string(w) +
                                     "x" + std::to_string(h) + ", expected " +
                                     std::to_string(width) + "x" + std::to_string(height));
        }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(i), width, height, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Core in GL 4.6, otherwise an ARB or EXT extension; skipped if neither is present.
    if (GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat supported = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &supported);
        m_anisotropy = std::min(supported, kMaxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, m_anisotropy);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

TextureArray::~TextureArray()
{
    glDeleteTextures(1, &m_texture);
}

void TextureArray::bind(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
}

} // namespace emerald
