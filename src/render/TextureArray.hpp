#pragma once

#include "platform/Gl.hpp"

#include <filesystem>
#include <vector>

namespace emerald {

// GL_TEXTURE_2D_ARRAY of same-sized images, one layer per file, with mipmaps,
// repeat wrapping and anisotropic filtering when the driver supports it.
class TextureArray {
public:
    // Srgb for colour maps (decoded to linear when sampled), Linear for data such as normal maps.
    enum class Encoding { Srgb, Linear };

    TextureArray(const std::vector<std::filesystem::path>& layers, Encoding encoding);
    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    void bind(GLuint unit) const;

    // Read back from the driver after creation, to confirm filtering is active.
    float anisotropy() const { return m_anisotropy; }
    int mipLevels() const { return m_mipLevels; }
    bool trilinear() const { return m_trilinear; }

private:
    GLuint m_texture = 0;
    float m_anisotropy = 1.0f;
    int m_mipLevels = 0;
    bool m_trilinear = false;
};

} // namespace emerald
