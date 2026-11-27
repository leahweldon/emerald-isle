#pragma once

#include <glm/glm.hpp>

#include <array>

namespace emerald {

// Keyboard, mouse button, cursor and scroll state collected from GLFW callbacks (see Window).
// Key and button codes are GLFW's. Per-frame values reset in endFrame().
class Input {
public:
    // Call once per frame after the frame has read its input. Events arriving afterwards (polled
    // at the start of the next frame on desktop, delivered between frames by a browser) count
    // towards the next frame.
    void endFrame();

    bool isDown(int key) const;
    bool wasPressed(int key) const; // went down since the last endFrame()
    bool isMouseDown(int button) const;
    bool mousePressed(int button) const; // went down since the last endFrame()
    // Any key or button pressed, or any scroll, this frame (pointer movement alone does not count).
    bool anyActivity() const;
    // Last cursor position in window coordinates (unaffected by blocking).
    glm::dvec2 cursor() const { return m_lastCursor; }
    glm::vec2 mouseDelta() const { return m_mouseBlocked ? glm::vec2(0.0f) : m_mouseDelta; } // pixels
    // Wheel notches or trackpad scroll units: x horizontal, y vertical (GLFW's sign convention).
    glm::vec2 scrollDelta() const { return m_mouseBlocked ? glm::vec2(0.0f) : m_scrollDelta; }

    // While blocked (the UI is using them), keys or mouse read as idle. Set once per frame.
    void setBlocked(bool keyboard, bool mouse)
    {
        m_keyboardBlocked = keyboard;
        m_mouseBlocked = mouse;
    }

    // Forgets the last cursor position so the next move does not register as a jump.
    void resetCursor() { m_haveCursor = false; }
    // Relative motion reported directly, as from browser pointer lock (pixels).
    void addMouseDelta(const glm::vec2& delta) { m_mouseDelta += delta; }

    void onKey(int key, int action);
    void onMouseButton(int button, int action);
    void onCursor(double x, double y);
    void onScroll(double xOffset, double yOffset);

private:
    static constexpr int kKeyCount = 512;
    static constexpr int kButtonCount = 8;

    std::array<bool, kKeyCount> m_down{};
    std::array<bool, kKeyCount> m_pressed{};
    std::array<bool, kButtonCount> m_buttons{};
    std::array<bool, kButtonCount> m_buttonPressed{};
    glm::vec2 m_mouseDelta{0.0f};
    glm::dvec2 m_lastCursor{0.0};
    bool m_haveCursor = false;
    bool m_keyboardBlocked = false;
    bool m_mouseBlocked = false;
    glm::vec2 m_scrollDelta{0.0f};
};

// What the camera controller needs from a window: its input and the pointer capture.
class InputSource {
public:
    virtual ~InputSource() = default;
    virtual const Input& input() const = 0;
    virtual bool cursorCaptured() const = 0;
    virtual void setCursorCaptured(bool captured) = 0;
};

} // namespace emerald
