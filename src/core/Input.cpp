#include "core/Input.hpp"

#include <GLFW/glfw3.h>

namespace emerald {

namespace {

bool inRange(int code, int count)
{
    return code >= 0 && code < count;
}

} // namespace

void Input::beginFrame()
{
    m_pressed.fill(false);
    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = 0.0f;
}

bool Input::isDown(int key) const
{
    return inRange(key, kKeyCount) && m_down[static_cast<std::size_t>(key)];
}

bool Input::wasPressed(int key) const
{
    return inRange(key, kKeyCount) && m_pressed[static_cast<std::size_t>(key)];
}

bool Input::isMouseDown(int button) const
{
    return inRange(button, kButtonCount) && m_buttons[static_cast<std::size_t>(button)];
}

void Input::onKey(int key, int action)
{
    if (!inRange(key, kKeyCount)) {
        return;
    }
    const auto i = static_cast<std::size_t>(key);
    if (action == GLFW_PRESS) {
        m_down[i] = true;
        m_pressed[i] = true;
    } else if (action == GLFW_RELEASE) {
        m_down[i] = false;
    }
}

void Input::onMouseButton(int button, int action)
{
    if (inRange(button, kButtonCount)) {
        m_buttons[static_cast<std::size_t>(button)] = action == GLFW_PRESS;
    }
}

void Input::onCursor(double x, double y)
{
    const glm::dvec2 cursor(x, y);
    if (m_haveCursor) {
        m_mouseDelta += glm::vec2(cursor - m_lastCursor);
    }
    m_lastCursor = cursor;
    m_haveCursor = true;
}

void Input::onScroll(double yOffset)
{
    m_scrollDelta += static_cast<float>(yOffset);
}

} // namespace emerald
