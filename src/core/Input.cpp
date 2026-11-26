#include "core/Input.hpp"

#include <algorithm>

#include <GLFW/glfw3.h>

namespace emerald {

namespace {

bool inRange(int code, int count)
{
    return code >= 0 && code < count;
}

} // namespace

void Input::endFrame()
{
    m_pressed.fill(false);
    m_buttonPressed.fill(false);
    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = glm::vec2(0.0f);
}

bool Input::isDown(int key) const
{
    return !m_keyboardBlocked && inRange(key, kKeyCount) && m_down[static_cast<std::size_t>(key)];
}

bool Input::wasPressed(int key) const
{
    return !m_keyboardBlocked && inRange(key, kKeyCount) && m_pressed[static_cast<std::size_t>(key)];
}

bool Input::isMouseDown(int button) const
{
    return !m_mouseBlocked && inRange(button, kButtonCount) && m_buttons[static_cast<std::size_t>(button)];
}

bool Input::mousePressed(int button) const
{
    return !m_mouseBlocked && inRange(button, kButtonCount) && m_buttonPressed[static_cast<std::size_t>(button)];
}

bool Input::anyActivity() const
{
    const auto any = [](const auto& flags) {
        return std::any_of(flags.begin(), flags.end(), [](bool f) { return f; });
    };
    return (!m_keyboardBlocked && any(m_pressed)) ||
           (!m_mouseBlocked && (any(m_buttonPressed) || m_scrollDelta != glm::vec2(0.0f)));
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
        m_buttonPressed[static_cast<std::size_t>(button)] =
            m_buttonPressed[static_cast<std::size_t>(button)] || action == GLFW_PRESS;
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

void Input::onScroll(double xOffset, double yOffset)
{
    m_scrollDelta += glm::vec2(static_cast<float>(xOffset), static_cast<float>(yOffset));
}

} // namespace emerald
