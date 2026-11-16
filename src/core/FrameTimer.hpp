#pragma once

namespace emerald {

// Measures per-frame delta time and reports an averaged frame rate.
// Averaging over a short window keeps the displayed FPS readable instead of flickering.
class FrameTimer {
public:
    explicit FrameTimer(double sampleWindowSeconds = 0.5);

    // Call once per frame. Returns the time since the previous call, in seconds.
    float tick();

    double elapsedSeconds() const { return m_elapsed; }
    double framesPerSecond() const { return m_fps; }
    double frameTimeMs() const { return m_fps > 0.0 ? 1000.0 / m_fps : 0.0; }

    // True on the frame where a new FPS average became available.
    bool hasNewSample() const { return m_newSample; }

private:
    double m_sampleWindow;
    double m_lastTime = -1.0;
    double m_elapsed = 0.0;
    double m_windowStart = 0.0;
    int m_framesInWindow = 0;
    double m_fps = 0.0;
    bool m_newSample = false;
};

} // namespace emerald
