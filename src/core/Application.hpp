#pragma once

#include "core/CameraController.hpp"
#include "core/FrameTimer.hpp"
#include "core/TimeOfDay.hpp"
#include "core/Window.hpp"
#include "render/Atmosphere.hpp"
#include "render/Camera.hpp"

#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>
#include <string>

namespace emerald {

class Sky;
class Terrain;
class HeightField;
class SeaPlane;
class Shader;
class GpuTimer;
class ShadowMaps;
class OffscreenTarget;
class TextureArray;

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

// Parses command-line flags. Returns false (after printing usage) if the app should not start.
bool parseOptions(int argc, char** argv, AppOptions& options);

class Application {
public:
    explicit Application(AppOptions options);
    ~Application();

    int run();

private:
    void warmUp();
    void update(float deltaSeconds);
    void updateTime(float deltaSeconds);
    void render();
    void renderShadows(const glm::mat4& projection);
    void updateTitle();
    void recordFrameStats(float frameMs, float cpuMs);
    void printRunStats() const;
    void flushFrameLog(bool all);
    void validateFrame();
    bool reportValidation() const;
    void setSceneUniforms(Shader& shader);

    AppOptions m_options;
    Window m_window;
    FrameTimer m_timer;
    TimeOfDay m_time;
    std::unique_ptr<Sky> m_sky;
    std::unique_ptr<HeightField> m_heightField;
    std::unique_ptr<Terrain> m_terrain;
    std::unique_ptr<Shader> m_terrainShader;
    std::unique_ptr<TextureArray> m_materialColour;
    std::unique_ptr<TextureArray> m_materialNormal;
    std::unique_ptr<SeaPlane> m_sea;
    std::unique_ptr<Shader> m_seaShader;
    std::unique_ptr<Shader> m_shadowShader;
    std::unique_ptr<ShadowMaps> m_shadows;
    std::unique_ptr<GpuTimer> m_gpuTimer;
    std::unique_ptr<OffscreenTarget> m_offscreen; // --headless only
    Camera m_camera;
    std::unique_ptr<CameraController> m_controller;
    Atmosphere m_atmosphere;
    int m_frameCount = 0;
    int m_settledFrames = 0; // frames rendered with all wanted terrain tiles loaded

    // Run statistics, printed on exit.
    std::vector<float> m_frameMs;   // whole loop iteration, per frame
    std::vector<float> m_terrainMs; // terrain update on the main thread, per frame
    std::vector<float> m_cpuMs;     // loop iteration minus the swap wait
    std::vector<float> m_gpuMs;     // GPU time from timer queries; negative until known
    std::FILE* m_frameLog = nullptr;
    std::deque<std::pair<int, std::string>> m_pendingLogRows; // waiting for their GPU time
    double m_triangleSum = 0.0;
    double m_drawCallSum = 0.0;
    int m_peakResidentTiles = 0;

    // --validate counters. Hole checks start once the initial load has covered the view, which
    // does not need streaming to catch up completely (it may never, on a software renderer).
    bool m_finished = false; // the run reached its frame limit or autofly duration
    bool m_terrainReady = false;
    int m_validatedFrames = 0;
    int m_glErrorCount = 0;
    unsigned m_firstGlError = 0;
    int m_holeFrames = 0;
    int m_uncoveredFrames = 0;
};

} // namespace emerald
