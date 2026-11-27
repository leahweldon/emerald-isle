#include "world/SolarPosition.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace emerald;

namespace {

constexpr double kDublinLatitude = 53.3498;
constexpr double kDublinLongitude = -6.2603;

double elevation(int day, double minutes)
{
    return solarPosition(day, minutes, kDublinLatitude, kDublinLongitude).elevation;
}

// Local clock minute when the sun's upper limb crosses the horizon (apparent centre 16' below it),
// between a and b where the elevation is on either side of it.
double crossing(int day, double a, double b)
{
    constexpr double kUpperLimb = -16.0 / 60.0;
    const bool rising = elevation(day, a) < kUpperLimb;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (a + b);
        if ((elevation(day, mid) < kUpperLimb) == rising) {
            a = mid;
        } else {
            b = mid;
        }
    }
    return 0.5 * (a + b);
}

double highest(int day)
{
    double best = -90.0;
    for (double minutes = 0.0; minutes < 24.0 * 60.0; minutes += 0.25) {
        best = std::max(best, elevation(day, minutes));
    }
    return best;
}

double clock(int hours, int minutes)
{
    return hours * 60.0 + minutes;
}

} // namespace

// Published times for Dublin in 2026 (Irish civil time: IST in summer, GMT in winter), and noon
// elevations from 90 - latitude +/- the solstice declination (23.44), plus a little refraction.
TEST_CASE("Dublin at midsummer: sunrise, sunset and noon elevation")
{
    const int day = dayOfYear(6, 21);
    CHECK(std::abs(crossing(day, clock(3, 0), clock(7, 0)) - clock(4, 57)) <= 3.0);
    CHECK(std::abs(crossing(day, clock(19, 0), clock(23, 59)) - clock(21, 57)) <= 3.0);
    CHECK(std::abs(highest(day) - 60.1) <= 0.2);
}

TEST_CASE("Dublin at midwinter: sunrise, sunset and noon elevation")
{
    const int day = dayOfYear(12, 21);
    CHECK(std::abs(crossing(day, clock(7, 0), clock(10, 0)) - clock(8, 38)) <= 3.0);
    CHECK(std::abs(crossing(day, clock(15, 0), clock(18, 0)) - clock(16, 8)) <= 3.0);
    CHECK(std::abs(highest(day) - 13.3) <= 0.2);
}

TEST_CASE("Irish summer time and the calendar")
{
    CHECK(irishUtcOffset(dayOfYear(6, 21)) == 1.0);
    CHECK(irishUtcOffset(dayOfYear(12, 21)) == 0.0);
    int month = 0;
    int day = 0;
    monthAndDay(dayOfYear(12, 31), month, day);
    CHECK(month == 12);
    CHECK(day == 31);
}
