#include "core/Options.hpp"

#include "render/ViewPresets.hpp"
#include "world/SolarPosition.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>

namespace emerald {

namespace {

void printUsage(const char* program)
{
    std::printf(
        "Emerald Isle %s\n\n"
        "Usage: %s [options]\n\n"
        "  --width <px>          Window width (default 1280)\n"
        "  --height <px>         Window height (default 720)\n"
        "  --no-vsync            Render as fast as possible, to measure frame rate\n"
        "  --headless            Hidden window, render offscreen at --width x --height; needs\n"
        "                        --frames, --screenshot or --autofly\n"
        "  --frames <n>          Exit after rendering n frames\n"
        "  --screenshot <file>   Save the last frame as a PNG, then exit\n"
        "  --record <dir>        Save every frame as a PNG at a fixed 30 fps timestep (with --frames\n"
        "                        or --autofly), waiting for the terrain before each\n"
        "  --view <name>         Start from a camera preset: %s\n"
        "  --time <HH:MM>        Local clock time, Irish civil time (default 18:00)\n"
        "  --date <MM-DD>        Date (default 06-21, midsummer)\n"
        "  --sun-bearing <deg>   Fix the sun at this compass bearing, 0 = north (default 225)\n"
        "  --sun-elevation <deg> Fix the sun at this elevation (default 22)\n"
        "  --seed <n>            Procedural world seed (default 1)\n"
        "  --autofly <s>         Fly straight ahead for s seconds, print stats, exit\n"
        "  --frame-log <file>    Write per-frame phase timings (CSV)\n"
        "  --exposure <ev>       Exposure offset in stops (default 0)\n"
        "  --tone-mapper <name>  neutral (default, Khronos PBR Neutral) or agx\n"
        "  --rain <0-1>          Rain intensity, 0 dry to 1 heavy (default 0)\n"
        "  --ui                  Show the panel from the start (F1 toggles it)\n"
        "  --orbit               With --view, orbit its target until the first key, click or scroll\n"
        "  --paused              Start with the time of day paused (T resumes it)\n"
        "  --touch               Touch screen: Low quality unless --quality is given, gesture input\n"
        "  --quality <level>     low, medium or high (default high; medium on the web)\n"
        "  --debug <view>        Debug view: lod, cascades, tiles or wireframe (repeatable)\n"
        "  --fxaa <on|off>       FXAA (default: on when the quality has no MSAA)\n"
        "  --gpu-timing          Also measure GPU time per frame (adds ~0.4 ms on macOS)\n"
        "  --validate <tiles>    Check for GL errors and terrain holes, require at least this many\n"
        "                        tiles generated and resident, exit non-zero on failure\n"
        "  --help                Show this message\n",
        EMERALD_VERSION, program, viewPresetNames().c_str());
}

float parseFloat(const char* flag, const char* value, float min, float max)
{
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || *end != '\0' || !(parsed >= min && parsed <= max)) {
        throw std::invalid_argument(std::string("Invalid value for ") + flag + ": " + value);
    }
    return parsed;
}

// Two numbers separated by sep, each within its range, e.g. "21:30" or "06-21".
void parsePair(const char* flag, const char* value, char sep, int maxFirst, int& first, int& second,
               int minSecond, int maxSecond)
{
    char* end = nullptr;
    const long a = std::strtol(value, &end, 10);
    if (end == value || *end != sep) {
        throw std::invalid_argument(std::string("Invalid value for ") + flag + ": " + value);
    }
    const char* rest = end + 1;
    const long b = std::strtol(rest, &end, 10);
    if (end == rest || *end != '\0' || a < 0 || a > maxFirst || b < minSecond || b > maxSecond) {
        throw std::invalid_argument(std::string("Invalid value for ") + flag + ": " + value);
    }
    first = static_cast<int>(a);
    second = static_cast<int>(b);
}

int parsePositive(const char* flag, const char* value)
{
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 16384) {
        throw std::invalid_argument(std::string("Invalid value for ") + flag + ": " + value);
    }
    return static_cast<int>(parsed);
}

// Assets are copied next to the executable at build time. Fall back to the working
// directory so the app also runs when launched from the repository root.
std::filesystem::path locateAssets(const char* argv0)
{
    std::error_code ec;
    const std::filesystem::path besideExe =
        std::filesystem::absolute(argv0, ec).parent_path() / "assets";
    if (!ec && std::filesystem::exists(besideExe / "shaders")) {
        return besideExe;
    }
    return std::filesystem::current_path() / "assets";
}

} // namespace

bool parseOptions(int argc, char** argv, AppOptions& options)
{
    bool qualityGiven = false;
    options.assetDir = locateAssets(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const bool hasValue = i + 1 < argc;

        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return false;
        } else if (std::strcmp(arg, "--no-vsync") == 0) {
            options.window.vsync = false;
        } else if (std::strcmp(arg, "--headless") == 0) {
            options.window.headless = true;
        } else if (std::strcmp(arg, "--width") == 0 && hasValue) {
            options.window.width = parsePositive(arg, argv[++i]);
        } else if (std::strcmp(arg, "--height") == 0 && hasValue) {
            options.window.height = parsePositive(arg, argv[++i]);
        } else if (std::strcmp(arg, "--frames") == 0 && hasValue) {
            options.frameLimit = parsePositive(arg, argv[++i]);
        } else if (std::strcmp(arg, "--screenshot") == 0 && hasValue) {
            options.screenshotPath = argv[++i];
        } else if (std::strcmp(arg, "--record") == 0 && hasValue) {
            options.recordDir = argv[++i];
        } else if (std::strcmp(arg, "--view") == 0 && hasValue) {
            options.view = argv[++i];
            if (!findViewPreset(options.view)) {
                throw std::invalid_argument("Unknown view '" + options.view +
                                            "' (available: " + viewPresetNames() + ")");
            }
        } else if (std::strcmp(arg, "--time") == 0 && hasValue) {
            int hours = 0;
            int minutes = 0;
            parsePair(arg, argv[++i], ':', 23, hours, minutes, 0, 59);
            options.timeMinutes = hours * 60.0 + minutes;
        } else if (std::strcmp(arg, "--date") == 0 && hasValue) {
            const char* value = argv[++i];
            int month = 0;
            int day = 0;
            parsePair(arg, value, '-', 12, month, day, 1, 31);
            int checkMonth = 0;
            int checkDay = 0;
            monthAndDay(dayOfYear(month, day), checkMonth, checkDay);
            if (month < 1 || checkMonth != month || checkDay != day) {
                throw std::invalid_argument(std::string("Invalid value for --date: ") + value);
            }
            options.dayOfYear = dayOfYear(month, day);
        } else if (std::strcmp(arg, "--sun-bearing") == 0 && hasValue) {
            options.sunBearing = parseFloat(arg, argv[++i], -360.0f, 360.0f);
            options.fixedSun = true;
        } else if (std::strcmp(arg, "--sun-elevation") == 0 && hasValue) {
            options.sunElevation = parseFloat(arg, argv[++i], -10.0f, 90.0f);
            options.fixedSun = true;
        } else if (std::strcmp(arg, "--validate") == 0 && hasValue) {
            options.validateMinTiles = parsePositive(arg, argv[++i]);
        } else if (std::strcmp(arg, "--exposure") == 0 && hasValue) {
            options.exposureEv = parseFloat(arg, argv[++i], -10.0f, 10.0f);
        } else if (std::strcmp(arg, "--rain") == 0 && hasValue) {
            options.rain = parseFloat(arg, argv[++i], 0.0f, 1.0f);
        } else if (std::strcmp(arg, "--webgl-fallbacks") == 0) {
            options.webglFallbacks = true;
        } else if (std::strcmp(arg, "--quality") == 0 && hasValue) {
            const std::string name = argv[++i];
            options.quality = name == "low" ? 0 : name == "medium" ? 1 : name == "high" ? 2 : -1;
            qualityGiven = true;
            if (options.quality < 0) {
                throw std::invalid_argument("Invalid value for --quality: " + name + " (low, medium or high)");
            }
        } else if (std::strcmp(arg, "--orbit") == 0) {
            options.orbit = true;
        } else if (std::strcmp(arg, "--paused") == 0) {
            options.paused = true;
        } else if (std::strcmp(arg, "--touch") == 0) {
            options.touch = true;
        } else if (std::strcmp(arg, "--ui") == 0) {
            options.ui = true;
        } else if (std::strcmp(arg, "--debug") == 0 && hasValue) {
            const std::string view = argv[++i];
            if (view == "lod") {
                options.debugView = 1;
            } else if (view == "cascades") {
                options.debugView = 2;
            } else if (view == "tiles") {
                options.debugTiles = true;
            } else if (view == "wireframe") {
                options.debugWireframe = true;
            } else {
                throw std::invalid_argument("Invalid value for --debug: " + view +
                                            " (lod, cascades, tiles or wireframe)");
            }
        } else if (std::strcmp(arg, "--tone-mapper") == 0 && hasValue) {
            const std::string name = argv[++i];
            if (name != "neutral" && name != "agx") {
                throw std::invalid_argument("Invalid value for --tone-mapper: " + name + " (neutral or agx)");
            }
            options.agx = name == "agx";
        } else if (std::strcmp(arg, "--fxaa") == 0 && hasValue) {
            const std::string value = argv[++i];
            if (value != "on" && value != "off") {
                throw std::invalid_argument("Invalid value for --fxaa: " + value + " (on or off)");
            }
            options.fxaa = value == "on" ? 1 : 0;
        } else if (std::strcmp(arg, "--gpu-timing") == 0) {
            options.gpuTiming = true;
        } else if (std::strcmp(arg, "--frame-log") == 0 && hasValue) {
            options.frameLogPath = argv[++i];
        } else if (std::strcmp(arg, "--autofly") == 0 && hasValue) {
            options.autoflySeconds = parseFloat(arg, argv[++i], 0.1f, 3600.0f);
        } else if (std::strcmp(arg, "--seed") == 0 && hasValue) {
            char* end = nullptr;
            const char* value = argv[++i];
            const unsigned long long seed = std::strtoull(value, &end, 10);
            if (end == value || *end != '\0' || seed > 0xFFFFFFFFull) {
                throw std::invalid_argument(std::string("Invalid value for --seed: ") + value);
            }
            options.seed = static_cast<std::uint32_t>(seed);
        } else {
            throw std::invalid_argument(std::string("Unknown or incomplete option: ") + arg +
                                        " (run with --help for usage)");
        }
    }

    // Phones and tablets start at Low: small GPUs, and very high pixel densities.
    if (options.touch && !qualityGiven) {
        options.quality = 0;
    }
    // A screenshot run needs a few frames so the swap chain has settled.
    if (!options.screenshotPath.empty() && options.frameLimit == 0) {
        options.frameLimit = 10;
    }
    if (!options.recordDir.empty() && options.frameLimit == 0 && options.autoflySeconds <= 0.0f) {
        throw std::invalid_argument("--record needs --frames or --autofly");
    }
    // Runs that end by themselves keep the sun still, so they are reproducible.
    options.timeRunning = options.frameLimit == 0 && options.autoflySeconds <= 0.0f && !options.paused;
    if (options.window.headless) {
        // A hidden window gets no input, so nothing could end the run.
        if (options.frameLimit == 0 && options.autoflySeconds <= 0.0f) {
            throw std::invalid_argument("--headless needs --frames, --screenshot or --autofly");
        }
        // Nothing is presented, so there is no refresh to wait for.
        options.window.vsync = false;
    }
    return true;
}

} // namespace emerald
