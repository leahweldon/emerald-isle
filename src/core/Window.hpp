#pragma once

#include <string>

struct GLFWwindow;

namespace emerald {

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Emerald Isle";
    bool vsync = true;
    bool visible = true;
};

// Owns the GLFW window and its OpenGL 3.3 core context.
// Creating a Window also initialises GLFW and loads the GL function pointers.
class Window {
public:
    explicit Window(const WindowConfig& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void requestClose();
    void swapBuffers();
    void pollEvents();

    void setTitle(const std::string& title);

    // Size of the drawable area in pixels (differs from window size on HiDPI displays).
    int framebufferWidth() const { return m_framebufferWidth; }
    int framebufferHeight() const { return m_framebufferHeight; }
    float aspectRatio() const;

    GLFWwindow* native() const { return m_handle; }

private:
    static void onFramebufferResize(GLFWwindow* handle, int width, int height);
    static void onKey(GLFWwindow* handle, int key, int scancode, int action, int mods);

    GLFWwindow* m_handle = nullptr;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
};

} // namespace emerald
