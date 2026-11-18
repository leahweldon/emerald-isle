#include "world/SolarPosition.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace emerald {

namespace {

constexpr std::array<int, 12> kMonthDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegrees = 180.0 / kPi;
constexpr int kSummerTimeStart = 88; // 29 March
constexpr int kSummerTimeEnd = 298;  // 25 October

} // namespace

int dayOfYear(int month, int day)
{
    int total = day;
    for (int m = 1; m < month; ++m) {
        total += kMonthDays[static_cast<std::size_t>(m - 1)];
    }
    return total;
}

void monthAndDay(int dayOfYear, int& month, int& day)
{
    month = 1;
    day = dayOfYear;
    while (month < 12 && day > kMonthDays[static_cast<std::size_t>(month - 1)]) {
        day -= kMonthDays[static_cast<std::size_t>(month - 1)];
        ++month;
    }
}

double irishUtcOffset(int dayOfYear)
{
    return dayOfYear >= kSummerTimeStart && dayOfYear < kSummerTimeEnd ? 1.0 : 0.0;
}

SolarAngles solarPosition(int dayOfYear, double localMinutes, double latitude, double longitude)
{
    const double utcOffset = irishUtcOffset(dayOfYear);
    // Fractional year in radians.
    const double g = 2.0 * kPi / 365.0 * (dayOfYear - 1 + (localMinutes / 60.0 - utcOffset - 12.0) / 24.0);
    const double equationOfTime = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) -
                                            0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    const double declination = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) -
                               0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g) -
                               0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);

    // True solar time in minutes, then the hour angle (0 at solar noon, negative in the morning).
    const double solarMinutes = localMinutes + equationOfTime + 4.0 * longitude - 60.0 * utcOffset;
    const double hourAngle = (solarMinutes / 4.0 - 180.0) / kDegrees;
    const double lat = latitude / kDegrees;

    const double sinElevation = std::sin(lat) * std::sin(declination) +
                                std::cos(lat) * std::cos(declination) * std::cos(hourAngle);
    double elevation = std::asin(std::clamp(sinElevation, -1.0, 1.0)) * kDegrees;
    const double bearing = std::atan2(-std::sin(hourAngle),
                                      std::tan(declination) * std::cos(lat) -
                                          std::sin(lat) * std::cos(hourAngle)) * kDegrees;

    // Refraction (Saemundsson's formula) lifts the sun about half a degree at the horizon.
    if (elevation > -1.0) {
        elevation += 1.02 / std::tan((elevation + 10.3 / (elevation + 5.11)) / kDegrees) / 60.0;
    }
    return {bearing < 0.0 ? bearing + 360.0 : bearing, elevation};
}

} // namespace emerald
