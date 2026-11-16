#include "core/FrameTimer.hpp"

#include <GLFW/glfw3.h>

namespace emerald {

FrameTimer::FrameTimer(double sampleWindowSeconds)
    : m_sampleWindow(sampleWindowSeconds)
{
}

float FrameTimer::tick()
{
    const double now = glfwGetTime();
    if (m_lastTime < 0.0) {
        m_lastTime = now;
        m_windowStart = now;
    }

    const double delta = now - m_lastTime;
    m_lastTime = now;
    m_elapsed += delta;

    ++m_framesInWindow;
    m_newSample = false;
    const double windowLength = now - m_windowStart;
    if (windowLength >= m_sampleWindow) {
        m_fps = m_framesInWindow / windowLength;
        m_framesInWindow = 0;
        m_windowStart = now;
        m_newSample = true;
    }

    return static_cast<float>(delta);
}

} // namespace emerald
