#pragma once

#include "core/Window.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#ifdef __EMSCRIPTEN__
#define EMERALD_DEFAULT_QUALITY 1
#else
#define EMERALD_DEFAULT_QUALITY 2
#endif

namespace emerald {

struct AppOptions {
    WindowConfig window;
    // When greater than zero, the app renders this many frames and then exits.
    int frameLimit = 0;
    // When set, the final frame is saved to this PNG path before exiting.
    std::string screenshotPath;
    // Fixed camera preset name; empty means the default slow orbit.
    std::string view;
    // Local clock time and date driving the sun (default 18:00 at midsummer). Time runs in
    // interactive sessions and is paused for --frames, --screenshot and --autofly runs.
    int dayOfYear = 172; // 21 June
    double timeMinutes = 18.0 * 60.0;
    bool timeRunning = true;
    // Exposure offset in stops (EV) on top of the default, for the final tone mapping.
    float exposureEv = 0.0f;
    // Final tone curve: false for Khronos PBR Neutral, true for AgX.
    bool agx = false;
    // FXAA in the tone mapping: -1 whenever the scene has no MSAA, 0 off, 1 on.
    int fxaa = -1;
    // Show the panel from the start. Otherwise it starts open only in interactive sessions (not
    // in runs that end by themselves); F1 toggles it.
    bool ui = false;
    // Web build: use every WebGL fallback even where the browser has the feature, to test them.
    bool webglFallbacks = false;
    bool orbit = false;  // --orbit: orbit the --view target until the first input
    bool paused = false; // --paused: time of day starts paused
    // --touch (set by the web page on touch screens): Low quality unless --quality is given, no
    // pointer capture, and touch gestures steer the camera, keeping an orbit an orbit.
    bool touch = false;
    // Index into kQualities: High on desktop, Medium on the web.
    int quality = EMERALD_DEFAULT_QUALITY;
    // --debug: terrain colouring (0 off, 1 level of detail, 2 shadow cascades), node outlines and
    // wireframe, as in the panel's debug views.
    int debugView = 0;
    bool debugTiles = false;
    bool debugWireframe = false;
    // Starting rain intensity, 0 (dry) to 1 (heavy); R and Shift+R change it while running.
    float rain = 0.0f;
    // --sun-bearing or --sun-elevation fix the sun in place of the time of day (degrees).
    bool fixedSun = false;
    float sunBearing = 225.0f;
    float sunElevation = 22.0f;
    // Procedural world seed; the same seed always gives the same islands.
    std::uint32_t seed = 1;
    // When greater than zero, fly straight ahead for this many seconds, then exit.
    float autoflySeconds = 0.0f;
    // When set, a CSV row of per-phase timings is written for every frame.
    std::string frameLogPath;
    // Measure GPU time per frame with timer queries (adds overhead on macOS).
    bool gpuTiming = false;
    // When greater than zero, check correctness during the run (GL errors, holes in the terrain,
    // tiles streamed) and exit with failure if any check fails. The value is the minimum number
    // of tiles that must be generated and resident.
    int validateMinTiles = 0;
    std::filesystem::path assetDir;
};

// Parses command-line flags. Returns false (after printing usage) if the app should not start;
// throws std::invalid_argument for an unknown flag or a bad value.
bool parseOptions(int argc, char** argv, AppOptions& options);

} // namespace emerald
