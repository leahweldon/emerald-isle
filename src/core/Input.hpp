#pragma once

#include <glm/glm.hpp>

#include <array>

namespace emerald {

// Keyboard, mouse button, cursor and scroll state collected from GLFW callbacks (see Window).
// Key and button codes are GLFW's. Per-frame values reset in beginFrame().
class Input {
public:
    // Call once per frame before polling events.
    void beginFrame();

    bool isDown(int key) const;
    bool wasPressed(int key) const; // went down since the last beginFrame()
    bool isMouseDown(int button) const;
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
    glm::vec2 m_mouseDelta{0.0f};
    glm::dvec2 m_lastCursor{0.0};
    bool m_haveCursor = false;
    bool m_keyboardBlocked = false;
    bool m_mouseBlocked = false;
    glm::vec2 m_scrollDelta{0.0f};
};

} // namespace emerald
