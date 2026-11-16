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
    glm::vec2 mouseDelta() const { return m_mouseDelta; } // pixels (screen coordinates)
    float scrollDelta() const { return m_scrollDelta; }   // wheel notches

    // Forgets the last cursor position so the next move does not register as a jump.
    void resetCursor() { m_haveCursor = false; }

    void onKey(int key, int action);
    void onMouseButton(int button, int action);
    void onCursor(double x, double y);
    void onScroll(double yOffset);

private:
    static constexpr int kKeyCount = 512;
    static constexpr int kButtonCount = 8;

    std::array<bool, kKeyCount> m_down{};
    std::array<bool, kKeyCount> m_pressed{};
    std::array<bool, kButtonCount> m_buttons{};
    glm::vec2 m_mouseDelta{0.0f};
    glm::dvec2 m_lastCursor{0.0};
    bool m_haveCursor = false;
    float m_scrollDelta = 0.0f;
};

} // namespace emerald
