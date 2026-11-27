#include "core/Options.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

using namespace emerald;

namespace {

// Parses the flags as a command line would pass them.
bool parse(std::initializer_list<std::string> flags, AppOptions& options)
{
    std::vector<std::string> storage{"emerald-isle"};
    storage.insert(storage.end(), flags);
    std::vector<char*> argv;
    for (std::string& arg : storage) {
        argv.push_back(arg.data());
    }
    return parseOptions(static_cast<int>(argv.size()), argv.data(), options);
}

// The error message for flags that must fail, or "" if they parse.
std::string error(std::initializer_list<std::string> flags)
{
    AppOptions options;
    try {
        parse(flags, options);
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "";
}

} // namespace

TEST_CASE("Options: valid flags parse")
{
    AppOptions options;
    REQUIRE(parse({"--width", "800", "--height", "600", "--view", "kerry", "--time", "21:30", "--date", "12-21",
                   "--quality", "low", "--rain", "0.5", "--seed", "42", "--fxaa", "off", "--tone-mapper", "agx",
                   "--debug", "lod", "--debug", "tiles", "--exposure", "-1.5", "--sun-elevation", "10"},
                  options));
    CHECK(options.window.width == 800);
    CHECK(options.window.height == 600);
    CHECK(options.view == "kerry");
    CHECK(options.timeMinutes == 21 * 60 + 30);
    CHECK(options.dayOfYear == 355);
    CHECK(options.quality == 0);
    CHECK(options.rain == 0.5f);
    CHECK(options.seed == 42u);
    CHECK(options.fxaa == 0);
    CHECK(options.agx);
    CHECK(options.debugView == 1);
    CHECK(options.debugTiles);
    CHECK(options.exposureEv == -1.5f);
    CHECK(options.fixedSun);
    CHECK(options.sunElevation == 10.0f);
    CHECK(options.timeRunning); // nothing ends the run, so the clock runs
}

TEST_CASE("Options: defaults and implied settings")
{
    AppOptions plain;
    REQUIRE(parse({}, plain));
    CHECK(plain.frameLimit == 0);
    CHECK(plain.timeRunning);
    CHECK(plain.fxaa == -1);

    AppOptions shot;
    REQUIRE(parse({"--headless", "--screenshot", "out.png"}, shot));
    CHECK(shot.frameLimit == 10); // a screenshot needs a few frames
    CHECK_FALSE(shot.timeRunning);
    CHECK_FALSE(shot.window.vsync);

    AppOptions touch;
    REQUIRE(parse({"--touch"}, touch));
    CHECK(touch.quality == 0); // Low unless chosen
    AppOptions touchHigh;
    REQUIRE(parse({"--touch", "--quality", "high"}, touchHigh));
    CHECK(touchHigh.quality == 2);

    AppOptions record;
    REQUIRE(parse({"--headless", "--autofly", "9", "--record", "frames"}, record));
    CHECK(record.recordDir == "frames");
    CHECK_FALSE(record.timeRunning);

    AppOptions paused;
    REQUIRE(parse({"--paused"}, paused));
    CHECK_FALSE(paused.timeRunning);
}

TEST_CASE("Options: --help stops before starting")
{
    AppOptions options;
    CHECK_FALSE(parse({"--help"}, options));
}

TEST_CASE("Options: invalid flags fail with an error naming them")
{
    CHECK(error({"--width", "0"}).find("--width") != std::string::npos);
    CHECK(error({"--width", "wide"}).find("--width") != std::string::npos);
    CHECK(error({"--time", "25:00"}).find("--time") != std::string::npos);
    CHECK(error({"--time", "9"}).find("--time") != std::string::npos);
    CHECK(error({"--date", "02-30"}).find("--date") != std::string::npos);
    CHECK(error({"--quality", "ultra"}).find("--quality") != std::string::npos);
    CHECK(error({"--rain", "2"}).find("--rain") != std::string::npos);
    CHECK(error({"--seed", "-1"}).find("--seed") != std::string::npos);
    CHECK(error({"--fxaa", "maybe"}).find("--fxaa") != std::string::npos);
    CHECK(error({"--debug", "everything"}).find("--debug") != std::string::npos);
    CHECK(error({"--view", "atlantis"}).find("atlantis") != std::string::npos);
    CHECK(error({"--bogus"}).find("--bogus") != std::string::npos);
    CHECK(error({"--width"}).find("--width") != std::string::npos); // missing value
    CHECK(error({"--headless"}).find("--headless") != std::string::npos); // nothing would end it
    CHECK(error({"--record", "frames"}).find("--record") != std::string::npos); // nor here
}
