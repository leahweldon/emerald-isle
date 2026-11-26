#pragma once

#include "world/SolarPosition.hpp"

#include <string>

namespace emerald {

// Local clock time and date driving the sun. Runs at kRate times real time (times a speed
// multiplier) while not paused; stepping past midnight moves to the next or previous day.
class TimeOfDay {
public:
    static constexpr double kRate = 60.0; // one minute per second

    TimeOfDay(int dayOfYear, double minutes, bool running);

    void advance(double realSeconds);
    void step(double minutes);
    void toggle() { m_running = !m_running; }
    void setRunning(bool running) { m_running = running; }
    void setMinutes(double minutes);
    void setDayOfYear(int day);
    void setSpeed(double speed) { m_speed = speed; }
    double speed() const { return m_speed; }

    bool running() const { return m_running; }
    int dayOfYear() const { return m_day; }
    double minutes() const { return m_minutes; }
    SolarAngles sun() const { return solarPosition(m_day, m_minutes); }
    // "21:05 21 Jun", plus " (paused)" when stopped.
    std::string label() const;

private:
    int m_day;
    double m_minutes; // after local midnight, [0, 1440)
    bool m_running;
    double m_speed = 1.0; // multiplier on kRate
};

} // namespace emerald
