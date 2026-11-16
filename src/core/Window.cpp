#include "core/Window.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

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
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialise GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    // macOS only exposes core profiles through a forward-compatible context.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_handle = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!m_handle) {
        glfwTerminate();
        throw std::runtime_error("Failed to create a window with an OpenGL 3.3 core context");
    }

    glfwMakeContextCurrent(m_handle);
    glfwSwapInterval(config.vsync ? 1 : 0);

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        glfwDestroyWindow(m_handle);
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL functions");
    }

    glfwGetFramebufferSize(m_handle, &m_framebufferWidth, &m_framebufferHeight);
    glfwSetWindowUserPointer(m_handle, this);
    glfwSetFramebufferSizeCallback(m_handle, onFramebufferResize);
    glfwSetKeyCallback(m_handle, onKey);
    glfwSetMouseButtonCallback(m_handle, onMouseButton);
    glfwSetCursorPosCallback(m_handle, onCursor);
    glfwSetScrollCallback(m_handle, onScroll);

    std::printf("OpenGL %s | %s\n",
                reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
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

void Window::requestClose()
{
    glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
}

void Window::swapBuffers()
{
    glfwSwapBuffers(m_handle);
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
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
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

void Window::onScroll(GLFWwindow* handle, double /*xOffset*/, double yOffset)
{
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onScroll(yOffset);
}

} // namespace emerald
