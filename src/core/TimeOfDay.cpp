#include "core/TimeOfDay.hpp"

#include <cmath>
#include <cstdio>

namespace emerald {

namespace {

constexpr double kMinutesPerDay = 1440.0;
constexpr int kDaysPerYear = 365;
constexpr const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

} // namespace

TimeOfDay::TimeOfDay(int dayOfYear, double minutes, bool running)
    : m_day(dayOfYear)
    , m_minutes(minutes)
    , m_running(running)
{
    step(0.0);
}

void TimeOfDay::advance(double realSeconds)
{
    if (m_running) {
        step(realSeconds * kRate / 60.0);
    }
}

void TimeOfDay::step(double minutes)
{
    m_minutes += minutes;
    const double days = std::floor(m_minutes / kMinutesPerDay);
    m_minutes -= days * kMinutesPerDay;
    m_day = static_cast<int>(((m_day - 1 + static_cast<long>(days)) % kDaysPerYear + kDaysPerYear) %
                             kDaysPerYear) + 1;
}

std::string TimeOfDay::label() const
{
    int month = 0;
    int day = 0;
    monthAndDay(m_day, month, day);
    const int total = static_cast<int>(m_minutes);
    char text[32];
    std::snprintf(text, sizeof(text), "%02d:%02d %d %s%s", total / 60, total % 60, day,
                  kMonthNames[month - 1], m_running ? "" : " (paused)");
    return text;
}

} // namespace emerald
