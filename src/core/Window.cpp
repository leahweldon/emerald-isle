#include "core/Window.hpp"

#include "platform/Gl.hpp"
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#include <GLFW/emscripten_glfw3.h>
#include <emscripten/em_js.h>
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

#ifdef __EMSCRIPTEN__
// Mouse look under pointer lock. The GLFW port reports each event's movementX/Y as the cursor
// position, so differences between positions lose the motion. Instead the page sums movementX/Y
// as events arrive and the app takes the sum once per frame. Module.mouseLook.onMove and onTake
// are hooks for the page's measurements (?lookstats).
EM_JS(void, installPointerLockMovement, (), {
    var look = Module.mouseLook = { x: 0, y: 0, onMove: null, onTake: null };
    document.addEventListener('mousemove', function (e) {
        if (!document.pointerLockElement) return;
        look.x += e.movementX;
        look.y += e.movementY;
        if (look.onMove) look.onMove(e);
    });
});

EM_JS(void, takePointerLockMovement, (float* out), {
    var look = Module.mouseLook;
    HEAPF32[out >> 2] = look.x;
    HEAPF32[(out >> 2) + 1] = look.y;
    look.x = look.y = 0;
    if (look.onTake) look.onTake();
});
#endif

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

    m_headless = config.headless;
    m_clickToCapture = config.clickToCapture;
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
#ifdef __EMSCRIPTEN__
    installPointerLockMovement();
#endif

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

float Window::pixelRatio() const
{
    int width = 0;
    int height = 0;
    glfwGetWindowSize(m_handle, &width, &height);
    if (m_headless || width <= 0) {
        return 1.0f;
    }
    return static_cast<float>(m_framebufferWidth) / static_cast<float>(width);
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
#ifdef __EMSCRIPTEN__
    float movement[2];
    takePointerLockMovement(movement);
    const bool locked = cursorCaptured();
    if (locked) {
        m_input.addMouseDelta(glm::vec2(movement[0], movement[1]));
    }
    // Locked or released by the browser (Escape): the next cursor position is not a move.
    if (locked != m_wasLocked) {
        m_input.resetCursor();
        m_wasLocked = locked;
    }
#endif
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

bool Window::cursorCaptured() const
{
    if (gl::kGles) {
        return glfwGetInputMode(m_handle, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }
    return m_cursorCaptured;
}

void Window::setCursorCaptured(bool captured)
{
    if (captured == cursorCaptured()) {
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
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    window->m_input.onMouseButton(button, action);
    // Browsers grant pointer lock only from inside an input event handler, which is where the
    // port calls this from.
    if (window->m_clickToCapture && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS &&
        !window->cursorCaptured() && (!window->m_captureFilter || window->m_captureFilter())) {
        window->setCursorCaptured(true);
    }
}

void Window::onCursor(GLFWwindow* handle, double x, double y)
{
#ifdef __EMSCRIPTEN__
    if (static_cast<Window*>(glfwGetWindowUserPointer(handle))->cursorCaptured()) {
        return; // taken from movementX/Y in pollEvents()
    }
#endif
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onCursor(x, y);
}

void Window::onScroll(GLFWwindow* handle, double xOffset, double yOffset)
{
    static_cast<Window*>(glfwGetWindowUserPointer(handle))->m_input.onScroll(xOffset, yOffset);
}

} // namespace emerald
