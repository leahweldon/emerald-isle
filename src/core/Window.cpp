#include "core/Window.hpp"

#include "platform/Gl.hpp"
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#include <GLFW/emscripten_glfw3.h>
#endif

#include <cstdio>
#include <stdexcept>

namespace emerald {

namespace {

void logGlfwError(int code, const char* description)
{
    std::fprintf(stderr, "[glfw] error %d: %s\n", code, description);
}

} // namespace

Window::Window(const WindowConfig& config)
{
    glfwSetErrorCallback(logGlfwError);
    if (config.headless) {
        glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
    }
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialise GLFW");
    }

#ifdef __EMSCRIPTEN__
    // WebGL 2 (OpenGL ES 3.0) on the page's canvas, at the device's pixel density.
    // (The WebGL version comes from the link flags.)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
#ifdef __APPLE__
    // macOS only exposes core profiles through a forward-compatible context.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // The scene is drawn (multisampled) into an HDR target and only the tone-mapped image reaches
    // the default framebuffer, so it needs neither MSAA nor depth.
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    if (config.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    m_handle = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!m_handle) {
        glfwTerminate();
        throw std::runtime_error("Failed to create a window with an OpenGL 3.3 core context");
    }

    glfwMakeContextCurrent(m_handle);
    glfwSwapInterval(config.vsync && !config.headless ? 1 : 0);

#ifdef __EMSCRIPTEN__
    // The canvas follows the browser window.
    emscripten_glfw_make_canvas_resizable(m_handle, "window", nullptr);
#else
    if (gladLoadGL(glfwGetProcAddress) == 0) {
        glfwDestroyWindow(m_handle);
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL functions");
    }
#endif
    gl::initCaps();

    if (config.headless) {
        m_framebufferWidth = config.width;
        m_framebufferHeight = config.height;
    } else {
        glfwGetFramebufferSize(m_handle, &m_framebufferWidth, &m_framebufferHeight);
    }
    glfwSetWindowUserPointer(m_handle, this);
    if (!config.headless) {
        glfwSetFramebufferSizeCallback(m_handle, onFramebufferResize);
    }
    glfwSetKeyCallback(m_handle, onKey);
    glfwSetMouseButtonCallback(m_handle, onMouseButton);
    glfwSetCursorPosCallback(m_handle, onCursor);
    glfwSetScrollCallback(m_handle, onScroll);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    m_refreshRate = mode ? mode->refreshRate : 0;
    std::printf("OpenGL %s | %s | display %d Hz\n",
                reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)), m_refreshRate);
}

Window::~Window()
{
    if (m_handle) {
        glfwDestroyWindow(m_handle);
    }
    glfwTerminate();
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_handle) != 0;
}

bool Window::focused() const
{
    return glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) != 0;
}

void Window::requestClose()
{
    glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
}

void Window::swapBuffers()
{
    // The browser presents each frame when control returns to it.
    if (!gl::kGles) {
        glfwSwapBuffers(m_handle);
    }
}

void Window::pollEvents()
{
    glfwPollEvents();
}

void Window::setTitle(const std::string& title)
{
    glfwSetWindowTitle(m_handle, title.c_str());
}

float Window::aspectRatio() const
{
    return m_framebufferHeight > 0
        ? static_cast<float>(m_framebufferWidth) / static_cast<float>(m_framebufferHeight)
        : 1.0f;
}

void Window::onFramebufferResize(GLFWwindow* handle, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    self->m_framebufferWidth = width;
    self->m_framebufferHeight = height;
    glViewport(0, 0, width, height);
}

void Window::setCursorCaptured(bool captured)
{
    if (captured == m_cursorCaptured) {
        return;
    }
    m_cursorCaptured = captured;
    glfwSetInputMode(m_handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_handle, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
    // The cursor jumps when capture changes; do not report that as movement.
    m_input.resetCursor();
}

void Window::onKey(GLFWwindow* handle, int key, int /*scancode*/, int action, int /*mods*/)
{
    // In a browser there is nothing to close; Escape releases the pointer instead.
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS && !gl::kGles) {
        glfwSetWindowShouldClose(handle, GLFW_TRUE);
    }
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onKey(key, action);
}

void Window::onMouseButton(GLFWwindow* handle, int button, int action, int /*mods*/)
{
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onMouseButton(button, action);
}

void Window::onCursor(GLFWwindow* handle, double x, double y)
{
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onCursor(x, y);
}

void Window::onScroll(GLFWwindow* handle, double xOffset, double yOffset)
{
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onScroll(xOffset, yOffset);
}

} // namespace emerald
