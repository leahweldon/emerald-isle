#pragma once

#include "core/Input.hpp"

#include <functional>
#include <string>

struct GLFWwindow;

namespace emerald {

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Emerald Isle";
    bool vsync = true;
    int samples = 4; // MSAA samples of the HDR scene target
    // Hidden window that never takes focus; the final image goes to an OffscreenTarget instead.
    // The framebuffer size is then width x height exactly, with no HiDPI scaling.
    bool headless = false;
    // A left click captures the pointer for mouse look (the web build, where Tab moves focus).
    bool clickToCapture = false;
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
    // macOS throttles presentation for windows in the background, so frame logs record this.
    bool focused() const;
    void requestClose();
    void swapBuffers();
    void pollEvents();

    void setTitle(const std::string& title);

    // Size of the drawable area in pixels (differs from window size on HiDPI displays).
    int framebufferWidth() const { return m_framebufferWidth; }
    int framebufferHeight() const { return m_framebufferHeight; }
    int refreshRate() const { return m_refreshRate; } // primary display, 0 if unknown
    float aspectRatio() const;
    // Device pixels per window unit (2 on a Retina display or a HiDPI browser); 1 headless.
    float pixelRatio() const;

    GLFWwindow* native() const { return m_handle; }

    Input& input() { return m_input; }
    const Input& input() const { return m_input; }

    // With clickToCapture: whether a left click may capture the pointer now (false over the UI).
    void setCaptureFilter(std::function<bool()> filter) { m_captureFilter = std::move(filter); }

    // Hides the cursor and reports unbounded (raw where supported) motion for mouse look.
    void setCursorCaptured(bool captured);
    // In a browser the pointer lock can also end without us (Escape), so this asks GLFW there.
    bool cursorCaptured() const;

private:
    static void onFramebufferResize(GLFWwindow* handle, int width, int height);
    static void onKey(GLFWwindow* handle, int key, int scancode, int action, int mods);
    static void onMouseButton(GLFWwindow* handle, int button, int action, int mods);
    static void onCursor(GLFWwindow* handle, double x, double y);
    static void onScroll(GLFWwindow* handle, double xOffset, double yOffset);

    GLFWwindow* m_handle = nullptr;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    Input m_input;
    bool m_cursorCaptured = false;
#ifdef __EMSCRIPTEN__
    bool m_wasLocked = false; // browser pointer lock at the last poll
#endif
    int m_refreshRate = 0;
    bool m_headless = false;
    bool m_clickToCapture = false;
    std::function<bool()> m_captureFilter;
};

} // namespace emerald
