#pragma once

#include "core/CameraController.hpp"
#include "core/FrameTimer.hpp"
#include "core/Options.hpp"
#include "core/Quality.hpp"
#include "core/TimeOfDay.hpp"
#include "core/Window.hpp"
#include "render/Atmosphere.hpp"
#include "render/Camera.hpp"
#include "render/GpuPassTimer.hpp"

#include <array>
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
class SeaSurface;
class SeabedMap;
class ReflectionTarget;
class Shader;
class GpuTimer;
class ShadowMaps;
class OffscreenTarget;
class TextureArray;
class Rain;
class Ui;


class Application {
public:
    explicit Application(AppOptions options);
    ~Application();

    // Runs frames until the window closes, then finish().
    int run();
    // One iteration of the frame loop, for hosts that drive it themselves (the browser).
    void frame();
    // Prints the run's statistics and validation; returns the exit code.
    int finish();
    // True once a run that ends by itself (--frames, --autofly) has finished.
    bool done() const;
    // Camera, orbit, panel and pointer state as JSON, for the web build's input test.
    std::string stateJson() const;
    // Touch gestures from the web page, applied at the next update: drags in CSS pixels, a pinch
    // as a scale.
    void touchDrag(float dx, float dy);
    void touchPinch(float scale);
    void touchPan(float dx, float dy);
    // The touch overlay's buttons: each steps through its choices and returns the new label.
    const char* nextView();
    const char* cycleTime();
    const char* cycleRain();
    // Shows or hides the panel (F1 does the same), for the web page's button.
    void toggleUi();

private:
    // Rain levels for Shift+R and the panel's buttons.
    static constexpr float kLightRain = 0.3f;
    static constexpr float kHeavyRain = 1.0f;

    void warmUp();
    void update(float deltaSeconds);
    void updateTime(float deltaSeconds);
    void updateWeather(float deltaSeconds);
    void renderRain(); // simulation; drawing happens after the opaque scene
    void render();
    void renderShadows(const glm::mat4& projection);
    void renderSeabed();
    void renderReflection(const glm::mat4& projection);
    // Resolves the HDR scene and tone maps it into the window (or the headless target).
    void toneMap();
    void applyQuality(int quality);
    int sceneWidth() const;  // the HDR scene's size: the framebuffer, or less under a pixel-ratio cap
    int sceneHeight() const;
    float displayExposure() const; // linear scale before the tone curve
    void setTerrainUniforms(Shader& shader);
    void updateTitle();
    void recordFrameStats(float frameMs, float cpuMs);
    float m_lastWorkMs = 0.0f; // CPU work of the last frame (all but the swap), ms
    void printRunStats() const;
    void flushFrameLog(bool all);
    void validateFrame();
    // Starts the UI frame and builds the panel (Panel.cpp), and keeps its input from the camera.
    void updateUi();
    void buildPanel();
    void panelPerformance();
    void panelTime();
    void panelWeather();
    void panelRendering();
    void panelCamera();
    void panelDebug();
    void panelHelp();
    // Starts a GPU timing segment for pass while the panel is open.
    void markPass(GpuPassTimer::Pass pass);
    void collectPassTimes();
    void markScenePart(GpuPassTimer::Pass pass); // only when the scene pass is split
    // Reads every pending GL error into count (recording the first), for --validate.
    static void drainGlErrors(int& count, unsigned& first);
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
    std::unique_ptr<SeaSurface> m_sea;
    std::unique_ptr<Shader> m_seaShader;
    std::unique_ptr<Shader> m_shadowShader;
    std::unique_ptr<ShadowMaps> m_shadows;
    std::unique_ptr<SeabedMap> m_seabed;
    std::unique_ptr<Shader> m_reflectionShader;
    std::unique_ptr<ReflectionTarget> m_reflection;
    std::unique_ptr<GpuTimer> m_gpuTimer;
    std::unique_ptr<OffscreenTarget> m_offscreen; // --headless only: the final image
    std::unique_ptr<OffscreenTarget> m_scene;     // linear HDR, multisampled
    std::unique_ptr<Shader> m_toneMapShader;
    std::unique_ptr<Rain> m_rain;
    std::unique_ptr<Ui> m_ui;
    // Features the panel can switch off, to see what each contributes.
    struct Features {
        bool shadows = true;
        bool horizonShadows = true;
        bool reflections = true;
        bool rainParticles = true;
    } m_features;
    // Debug views: they show how the systems work.
    struct DebugViews {
        int view = 0; // terrain colouring: 0 off, 1 level of detail, 2 shadow cascades
        bool tileBorders = false;
        bool wireframe = false;
        bool reflectionPreview = false;
    } m_debug;
    // Per-pass GPU timing while the panel is open (it then stands in for m_gpuTimer).
    std::unique_ptr<GpuPassTimer> m_passTimer;
    bool m_passTiming = false;
    bool m_splitScenePass = false;
    int m_quality = 0;
    bool fxaaEnabled() const;
    float m_maxPixelRatio = 16.0f;
    int m_reflectionDivisor = 2; // time terrain, sea, sky and rain drops separately
    GpuPassTimer::Times m_passMs{}; // smoothed for display
    float m_passTotalMs = 0.0f;
    std::array<float, 480> m_frameHistory{}; // recent frame times for the panel's graph, a ring
    std::size_t m_historyNext = 0;
    double m_copiedAt = -10.0; // when the panel last copied the pose, seconds
    unsigned m_fullscreenVao = 0; // GL vertex array name
    Camera m_camera;
    std::unique_ptr<CameraController> m_controller;
    Atmosphere m_atmosphere;
    // Rain intensity eases towards the target over a few seconds; R toggles the target between
    // 0 and the level, Shift+R switches the level between light and heavy.
    float m_rainTarget = 0.0f;
    float m_rainIntensity = 0.0f;
    float m_rainLevel = 0.0f;
    float m_wetness = 0.0f;
    float m_deltaSeconds = 0.0f;
    bool m_rainWarmedUp = false; // screenshot runs: rain settled before the frames counted
    bool m_orbitingUntilInput = false; // --orbit, until the first input
    glm::vec2 m_touchDrag{0.0f};       // gestures since the last update
    glm::vec2 m_touchPan{0.0f};
    float m_touchPinch = 1.0f;
    int m_touchView = 0;  // overlay choices, as indices into their lists
    int m_touchTime = 0;
    int m_touchRain = 0;
    glm::dvec3 m_lastCameraPos{0.0};
    glm::vec3 m_cameraVelocity{0.0f};
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
    bool m_finished = false;
    int m_exitCode = 0; // the run reached its frame limit or autofly duration
    bool m_terrainReady = false;
    int m_validatedFrames = 0;
    int m_glErrorCount = 0;
    unsigned m_firstGlError = 0;
    int m_holeFrames = 0;
    int m_uncoveredFrames = 0;
    int m_shadowGlErrorCount = 0; // raised inside the shadow passes
    unsigned m_firstShadowGlError = 0;
    int m_shadowFrames = 0;       // frames that rendered the shadow passes
    int m_shadowCasterFrames = 0; // ... and drew at least one caster
};

} // namespace emerald
