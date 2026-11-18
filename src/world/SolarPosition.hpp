#pragma once

namespace emerald {

// The whole world is lit as if it were at the centre of Ireland.
constexpr double kIrelandLatitude = 53.4;   // degrees north
constexpr double kIrelandLongitude = -8.0;  // degrees east

struct SolarAngles {
    double bearing;   // compass degrees, 0 = north, 90 = east
    double elevation; // degrees above the horizon, with refraction
};

// Day of the year (1-365) for a month and day; the calendar has no leap day.
int dayOfYear(int month, int day);
void monthAndDay(int dayOfYear, int& month, int& day);

// Irish civil time offset from UTC in hours: summer time (IST, +1) from the last Sunday in March
// to the last Sunday in October, taken as 29 March and 25 October (2026).
double irishUtcOffset(int dayOfYear);

// Sun position for a day of the year and local clock time (minutes after midnight), from the NOAA
// solar equations (declination and equation of time as Fourier series), plus atmospheric
// refraction near the horizon.
SolarAngles solarPosition(int dayOfYear, double localMinutes, double latitude = kIrelandLatitude,
                          double longitude = kIrelandLongitude);

} // namespace emerald
