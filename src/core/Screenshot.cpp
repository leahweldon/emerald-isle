#include "core/Screenshot.hpp"

#include <glad/gl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace emerald {

bool saveFramebufferPng(unsigned framebuffer, int width, int height, const std::string& path)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
    std::vector<unsigned char> pixels(rowBytes * static_cast<std::size_t>(height));

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    GLint previous = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous));

    // OpenGL's origin is bottom-left; image files expect the first row at the top.
    std::vector<unsigned char> row(rowBytes);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
        unsigned char* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * rowBytes;
        std::memcpy(row.data(), top, rowBytes);
        std::memcpy(top, bottom, rowBytes);
        std::memcpy(bottom, row.data(), rowBytes);
    }

    const bool ok = stbi_write_png(path.c_str(), width, height, 3, pixels.data(),
                                   static_cast<int>(rowBytes)) != 0;
    if (ok) {
        std::printf("Saved screenshot to %s\n", path.c_str());
    } else {
        std::fprintf(stderr, "Could not write screenshot to %s\n", path.c_str());
    }
    return ok;
}

} // namespace emerald
