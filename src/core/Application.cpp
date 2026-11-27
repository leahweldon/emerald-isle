#include "core/Application.hpp"

#include "core/Screenshot.hpp"
#include "render/GpuTimer.hpp"
#include "render/GpuPassTimer.hpp"
#include "render/OffscreenTarget.hpp"
#include "render/Rain.hpp"
#include "render/Shader.hpp"
#include "render/SeaSurface.hpp"
#include "render/SeabedMap.hpp"
#include "render/ReflectionTarget.hpp"
#include "render/ShadowMaps.hpp"
#include "render/Sky.hpp"
#include "render/Terrain.hpp"
#include "render/TextureArray.hpp"
#include "render/ViewPresets.hpp"
#include "ui/Ui.hpp"
#include "world/HeightField.hpp"

#include "platform/Gl.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace emerald {

namespace {

// Metres. The sea reaches almost to the far plane, so it meets the horizon.
constexpr float kFarPlane = 15000e3f;
constexpr float kSeaRadius = 0.99f * kFarPlane;
constexpr float kAutoflySpeed = 2500.0f; // metres per second, ~150 km a minute

// Material layers, in the order terrain.frag indexes them (kGrass, kSand, kHeath, kRock).
constexpr const char* kMaterials[] = {"Grass004", "Ground054", "Ground037", "Rock058"};
constexpr GLuint kColourUnit = 0;
constexpr GLuint kNormalUnit = 1;
constexpr GLuint kCoastUnit = 2;
constexpr GLuint kHeightUnit = 3;
constexpr GLuint kSkyUnit = 4;
constexpr GLuint kShadowUnit = 5;
constexpr GLuint kHorizonUnit = 6;
constexpr GLuint kRippleUnit = 7;
constexpr GLuint kSeabedUnit = 8;
constexpr GLuint kReflectionUnit = 9;
constexpr GLuint kSceneUnit = 10;
constexpr GLuint kRainGroundUnit = 11;
constexpr GLuint kRainDropUnit = 12;
// Packed floats, 32 bits a pixel like RGBA8: unclamped HDR with ~6-bit mantissas, which the
// tone mapping and dither cannot tell from RGBA16F (at most 3/255 apart, no banding), at a
// quarter of its frame cost with 4x MSAA (0.1-0.3 ms against 1 ms of GPU time at 2560 x 1440).
// Where float render targets are missing (some WebGL), 8-bit, with the scene shaders tone mapping.
GLenum sceneFormat()
{
    return gl::caps().floatTargets ? GL_R11F_G11F_B10F : GL_RGBA8;
}
// Linear scale before each tone curve at --exposure 0, fitted so noon terrain, sea and sky match
// the earlier display transform (linear up to 0.8) as closely as the curve allows.
constexpr float kNeutralExposure = 1.18f;
constexpr float kAgxExposure = 1.16f;
// Eye adaptation: stops of extra exposure at full night (with Atmosphere::night()), so moonlit
// land stays readable through the tone curve's toe.
constexpr float kNightAdaptationEv = 1.0f;
// And under full cloud, which is darker than clear sky with the sun out.
constexpr float kOvercastAdaptationEv = 0.5f;
// Time constant of the ease between rain intensities (seconds).
constexpr float kRainEaseSeconds = 1.2f;
// The ground is soaked by light rain already; it wets over a couple of seconds, dries more slowly.
constexpr float kWettingSeconds = 2.0f;
constexpr float kDryingSeconds = 6.0f;
// Screenshot runs step the rain at this fixed rate, after a warm-up, so captures repeat exactly.
constexpr float kRainFixedStep = 1.0f / 60.0f;
constexpr int kRainWarmUpSteps = 90;
// The reflection is rendered with this level-of-detail scale (< 1 is coarser), and smaller than
// the scene (the quality's divisor): rippled and seen at grazing angles, it does not need more.
constexpr float kReflectionLodScale = 0.5f;
constexpr float kMinReflectionSide = 256.0f; // pixels, along the reflection's shorter side

// MSVC deprecates fopen (C4996, an error with warnings as errors); fopen_s is its replacement.
std::FILE* openForWriting(const std::string& path)
{
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return fopen_s(&file, path.c_str(), "w") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "w");
#endif
}

std::vector<std::filesystem::path> materialFiles(const std::filesystem::path& dir, const char* map)
{
    std::vector<std::filesystem::path> files;
    for (const char* name : kMaterials) {
        files.push_back(dir / (std::string(name) + "_1K-JPG_" + map + ".jpg"));
    }
    return files;
}


} // namespace

Application::Application(AppOptions options)
    : m_options(std::move(options))
    , m_window(m_options.window)
    , m_time(m_options.dayOfYear, m_options.timeMinutes, m_options.timeRunning)
{
    if (m_options.window.headless) {
        // The final image only: multisampling happens in the scene target.
        m_offscreen = std::make_unique<OffscreenTarget>(m_window.framebufferWidth(),
                                                        m_window.framebufferHeight(), 0);
    } else {
        glViewport(0, 0, m_window.framebufferWidth(), m_window.framebufferHeight());
    }
    gl::enableMultisample();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const std::filesystem::path shaderDir = m_options.assetDir / "shaders";
    m_sky = std::make_unique<Sky>(shaderDir);
    m_toneMapShader = std::make_unique<Shader>(shaderDir / "fullscreen.vert", shaderDir / "tonemap.frag");
    glGenVertexArrays(1, &m_fullscreenVao); // the full-screen triangle comes from gl_VertexID
    m_terrainShader =
        std::make_unique<Shader>(shaderDir / "terrain.vert", shaderDir / "terrain.frag");
    m_seaShader = std::make_unique<Shader>(shaderDir / "sea.vert", shaderDir / "sea.frag");
    m_shadowShader = std::make_unique<Shader>(shaderDir / "terrain.vert", shaderDir / "shadow.frag",
                                              "#define SHADOW_PASS\n");
    m_shadows = std::make_unique<ShadowMaps>(kQualities[static_cast<std::size_t>(m_options.quality)].shadowResolution);
    m_seabed = std::make_unique<SeabedMap>();
    m_reflectionShader = std::make_unique<Shader>(shaderDir / "terrain.vert", shaderDir / "terrain.frag",
                                                  "#define REFLECTION_PASS\n");
    m_reflection = std::make_unique<ReflectionTarget>();
    m_rain = std::make_unique<Rain>(shaderDir, kRainGroundUnit, kRainDropUnit);
    if (m_options.validateMinTiles > 0) {
        m_rain->enableValidation();
    }
    m_rainTarget = m_rainIntensity = m_options.rain;
    m_wetness = std::min(m_options.rain / kLightRain, 1.0f);
    m_atmosphere.rain = m_rainIntensity;
    m_atmosphere.wetness = m_wetness;
    m_rainLevel = m_options.rain > 0.0f ? m_options.rain : kLightRain;
    if (m_options.gpuTiming && gl::caps().timerQueries) {
        m_gpuTimer = std::make_unique<GpuTimer>();
    }

    HeightFieldConfig heightConfig;
    heightConfig.seed = m_options.seed;
    m_heightField = std::make_unique<HeightField>(
        m_options.assetDir / "textures" / "ireland-heightmap.png", heightConfig);
    std::printf("World: seed %u\n", m_options.seed);

    m_terrain = std::make_unique<Terrain>(*m_heightField);
    m_sea = std::make_unique<SeaSurface>(kSeaRadius, Wind{}, m_options.assetDir / "textures" / "water");

    const std::filesystem::path materialDir = m_options.assetDir / "textures" / "materials";
    m_materialColour = std::make_unique<TextureArray>(materialFiles(materialDir, "Color"),
                                                      TextureArray::Encoding::Srgb);
    m_materialNormal = std::make_unique<TextureArray>(materialFiles(materialDir, "NormalGL"),
                                                      TextureArray::Encoding::Linear);
    std::printf("Materials: %zu layers, %d mip levels, %s, anisotropy %.0fx\n",
                std::size(kMaterials), m_materialColour->mipLevels(),
                m_materialColour->trilinear() ? "trilinear" : "NOT trilinear",
                static_cast<double>(m_materialColour->anisotropy()));

    for (Shader* shader : {m_terrainShader.get(), m_reflectionShader.get()}) {
        shader->use();
        shader->set("uMaterialColour", static_cast<int>(kColourUnit));
        shader->set("uMaterialNormal", static_cast<int>(kNormalUnit));
    }
    m_seaShader->use();
    m_seaShader->set("uCoastMask", static_cast<int>(kCoastUnit));
    m_seaShader->set("uTerrainSize", m_terrain->demSize());
    m_seaShader->set("uSeaRadius", kSeaRadius);

    if (!m_options.frameLogPath.empty()) {
        m_frameLog = openForWriting(m_options.frameLogPath);
        if (!m_frameLog) {
            throw std::runtime_error("Could not open frame log " + m_options.frameLogPath);
        }
        std::fprintf(m_frameLog, "frame,total_ms,events_ms,update_ms,terrain_ms,render_ms,swap_ms,"
                                 "uploads,outstanding,resident,triangles,draws,x,y,z,focused,gpu_ms\n");
    }
    if (m_options.fixedSun) {
        m_atmosphere.sunDir = sunDirection(m_options.sunBearing, m_options.sunElevation);
    } else {
        updateTime(0.0f);
        const SolarAngles sun = m_time.sun();
        std::printf("Sun: %s, bearing %.1f, elevation %.1f degrees\n", m_time.label().c_str(),
                    sun.bearing, sun.elevation);
    }
    m_camera.farPlane = kFarPlane;
    m_controller = std::make_unique<CameraController>(
        m_camera, [this](double x, double z) { return m_heightField->groundHeight(x, z); });

    // No --view: slow orbit from the overview. With --view: free-fly from that pose, still
    // until there is input, so screenshots are reproducible; with --orbit as well, a slow orbit
    // from that pose until the first input, then free-fly from wherever it has got to.
    if (m_options.view.empty()) {
        const ViewPreset& overview = *findViewPreset("overview");
        m_controller->startOrbit(overview.target, overview.azimuth, overview.elevation,
                                 overview.distance, true);
    } else if (m_options.orbit) {
        const ViewPreset& preset = *findViewPreset(m_options.view);
        applyViewPreset(preset, m_camera);
        m_controller->startOrbit(glm::dvec3(preset.target), preset.azimuth, preset.elevation, preset.distance, true);
        // On a touch screen the orbit stays an orbit, steered by gestures.
        m_orbitingUntilInput = !m_options.touch;
    } else {
        applyViewPreset(*findViewPreset(m_options.view), m_camera);
        m_controller->startFreeFly();
    }
    if (m_options.autoflySeconds > 0.0f) {
        if (m_options.view.empty()) {
            applyViewPreset(*findViewPreset("overview"), m_camera);
        }
        m_controller->startAutofly(kAutoflySpeed);
        std::printf("Autofly: %.0f s at %.0f m/s\n", static_cast<double>(m_options.autoflySeconds),
                    static_cast<double>(kAutoflySpeed));
    }

    m_lastCameraPos = m_camera.position;
    m_debug.view = m_options.debugView;
    m_debug.tileBorders = m_options.debugTiles;
    m_debug.wireframe = m_options.debugWireframe;
    m_passTimer = std::make_unique<GpuPassTimer>();
    m_ui = std::make_unique<Ui>(m_window.native(), m_options.window.headless, m_window.framebufferHeight(),
                                m_options.assetDir / "fonts" / "Roboto-Medium.ttf");
    const bool selfEnding = m_options.frameLimit > 0 || m_options.autoflySeconds > 0.0f;
    // In the browser it starts closed, out of the first view; F1 or the page's button opens it.
    m_ui->setVisible(m_options.ui || (!m_options.window.headless && !selfEnding && !gl::kGles));
    // Clicks on the panel never capture the pointer.
    m_window.setCaptureFilter([this] {
        const glm::dvec2 cursor = m_window.input().cursor();
        return !m_ui->wantsMouse() && !m_ui->covers(cursor.x, cursor.y);
    });
    applyQuality(m_options.quality);
    warmUp();
}

void Application::warmUp()
{
    // One-off work that otherwise lands in the first frame (~100-200 ms): macOS finishing the
    // window on the first event poll, the sky cubemap and its SH readback, first-use pipeline
    // compilation and the first present. Done here, before the frame loop and its timer start.
    const auto start = std::chrono::steady_clock::now();
    m_window.pollEvents();
    m_sky->renderNow(m_atmosphere);
    render();
    m_window.swapBuffers();
    glFinish();
    std::printf("Warm-up: %.0f ms before the first frame\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

Application::~Application()
{
    glDeleteVertexArrays(1, &m_fullscreenVao);
    flushFrameLog(true);
    if (m_frameLog) {
        std::fclose(m_frameLog);
    }
}

int Application::run()
{
    while (!m_window.shouldClose()) {
        frame();
    }
    return finish();
}

void Application::frame()
{
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto frameStart = Clock::now();
    float delta = m_timer.tick();
    // Recording: a fixed timestep per saved frame. The clock stands still while tiles are still
    // streaming in, so every saved frame has its terrain complete.
    const bool recording = !m_options.recordDir.empty();
    if (recording) {
        delta = m_recordAdvance ? kRecordStep : 0.0f;
        m_recordSeconds += static_cast<double>(delta);
    }

    m_window.pollEvents();
    const auto afterEvents = Clock::now();
    updateUi();
    update(delta);
    const auto afterUpdate = Clock::now();
    ++m_frameCount;
    // Pass timing replaces whole-frame timing while the panel is open: the queries cannot nest.
    // Every frame: timing only some of them made those frames 2-7 ms slower on macOS.
    m_passTiming = m_ui->visible() && gl::caps().timerQueries;
    if (m_passTiming) {
        m_passTimer->beginFrame(m_frameCount);
    } else if (m_gpuTimer) {
        m_gpuTimer->begin(m_frameCount);
    }
    render();
    if (m_passTiming) {
        m_passTimer->endFrame();
    } else if (m_gpuTimer) {
        m_gpuTimer->end();
    }
    const auto afterRender = Clock::now();

    // Screenshots count only frames with the terrain fully streamed in (or give up after 60 s),
    // so they never capture half-loaded tiles. Plain --frames runs count every frame.
    if (m_terrain->settled()) {
        ++m_settledFrames;
    }
    const bool screenshot = !m_options.screenshotPath.empty();
    const bool timedOut = screenshot && m_timer.elapsedSeconds() > 60.0;
    if (timedOut && m_settledFrames < m_options.frameLimit) {
        std::fprintf(stderr, "Terrain still streaming after 60 s; capturing anyway\n");
    }
    if (recording && !recordFrame()) {
        m_exitCode = EXIT_FAILURE;
        m_window.requestClose();
        return;
    }
    const int counted = recording ? m_recordedFrames : screenshot ? m_settledFrames : m_frameCount;
    const double runSeconds = recording ? m_recordSeconds : m_timer.elapsedSeconds();
    const bool autoflyDone = m_options.autoflySeconds > 0.0f &&
                             runSeconds >= static_cast<double>(m_options.autoflySeconds) && (!recording || m_recordAdvance);
    const bool lastFrame = autoflyDone ||
                           (m_options.frameLimit > 0 && (counted >= m_options.frameLimit || timedOut));
    if (lastFrame && !m_options.screenshotPath.empty()) {
        // Read the back buffer before it is presented, so the capture matches this frame.
        const unsigned source = m_offscreen ? m_offscreen->readFramebuffer() : 0;
        if (!saveFramebufferPng(source, m_window.framebufferWidth(), m_window.framebufferHeight(),
                                m_options.screenshotPath)) {
            m_exitCode = EXIT_FAILURE;
            m_window.requestClose();
            return;
        }
    }

    m_window.swapBuffers();
    const auto afterSwap = Clock::now();
    if (m_options.validateMinTiles > 0) {
        validateFrame();
    }

    if (m_timer.hasNewSample()) {
        updateTitle();
    }

    // Timed per loop iteration, so every phase belongs to the frame it is reported with.
    const double totalMs = ms(frameStart, Clock::now());
    const double swapMs = ms(afterRender, afterSwap);
    recordFrameStats(static_cast<float>(totalMs), static_cast<float>(totalMs - swapMs));
    m_lastWorkMs = static_cast<float>(totalMs - swapMs);
    collectPassTimes();
    if (m_gpuTimer) {
        for (const GpuTimer::Result& r : m_gpuTimer->collect()) {
            m_gpuMs[static_cast<std::size_t>(r.frame - 1)] = static_cast<float>(r.ms);
        }
    }
    if (m_frameLog) {
        const Terrain::Stats& st = m_terrain->stats();
        char row[256];
        std::snprintf(row, sizeof(row),
                      "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%zu,%d,%zu,%d,%.1f,%.1f,%.1f,%d,",
                      m_frameCount, totalMs, ms(frameStart, afterEvents), ms(afterEvents, afterUpdate),
                      st.updateMs, ms(afterUpdate, afterRender) - st.updateMs, swapMs,
                      st.uploadsThisFrame, st.outstandingTiles, st.residentTiles, st.triangles,
                      st.drawCalls, m_camera.position.x, m_camera.position.y, m_camera.position.z,
                      m_window.focused() ? 1 : 0);
        m_pendingLogRows.emplace_back(m_frameCount, row);
        flushFrameLog(false);
    }
    if (lastFrame) {
        m_finished = true;
        m_window.requestClose();
    }
    m_window.input().endFrame();
}

int Application::finish()
{
    if (m_exitCode != EXIT_SUCCESS) {
        return m_exitCode;
    }
    std::printf("Rendered %d frames, last average %.1f FPS (%.2f ms/frame)\n",
                m_frameCount, m_timer.framesPerSecond(), m_timer.frameTimeMs());
    printRunStats();
    if (!m_options.recordDir.empty()) {
        std::printf("Recorded %d frames (%.2f s at %.0f fps) to %s\n", m_recordedFrames, m_recordSeconds,
                    1.0 / static_cast<double>(kRecordStep), m_options.recordDir.c_str());
    }
    if (m_options.validateMinTiles > 0 && !reportValidation()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

bool Application::recordFrame()
{
    // Waits for the terrain (up to kRecordMaxWait frames), then saves this frame.
    ++m_recordWait;
    m_recordAdvance = false;
    if (!m_terrain->settled() && m_recordWait < kRecordMaxWait) {
        return true;
    }
    if (!m_terrain->settled()) {
        std::fprintf(stderr, "Terrain still streaming after %d frames; recording anyway\n", kRecordMaxWait);
    }
    if (m_recordedFrames == 0) {
        std::error_code ec;
        std::filesystem::create_directories(m_options.recordDir, ec);
    }
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%05d.png", m_recordedFrames);
    const std::string path = (std::filesystem::path(m_options.recordDir) / name).string();
    const unsigned source = m_offscreen ? m_offscreen->readFramebuffer() : 0;
    if (!saveFramebufferPng(source, m_window.framebufferWidth(), m_window.framebufferHeight(), path, false)) {
        return false;
    }
    ++m_recordedFrames;
    m_recordWait = 0;
    m_recordAdvance = true;
    return true;
}

void Application::update(float deltaSeconds)
{
    Input& input = m_window.input();
    if (m_orbitingUntilInput && input.anyActivity()) {
        m_orbitingUntilInput = false;
        m_controller->startFreeFly();
    }
    // Portrait: tilt the orbit down, fully at 9:16 or narrower.
    constexpr float kPortraitTilt = 0.2f;
    const float portrait = std::clamp((1.0f - m_window.aspectRatio()) / (1.0f - 9.0f / 16.0f), 0.0f, 1.0f);
    m_controller->setOrbitTilt(kPortraitTilt * portrait);
    // Gestures since the last frame (each one also stops an orbit's automatic rotation).
    if (m_touchDrag != glm::vec2(0.0f)) {
        m_controller->touchDrag(m_touchDrag);
    }
    if (m_touchPinch != 1.0f) {
        m_controller->touchPinch(m_touchPinch);
    }
    if (m_touchPan != glm::vec2(0.0f)) {
        m_controller->touchPan(m_touchPan);
    }
    m_touchDrag = m_touchPan = glm::vec2(0.0f);
    m_touchPinch = 1.0f;
    m_controller->update(m_window, deltaSeconds);
    updateTime(deltaSeconds);
    updateWeather(deltaSeconds);
    if (deltaSeconds > 0.0f) {
        m_cameraVelocity = glm::vec3((m_camera.position - m_lastCameraPos) / static_cast<double>(deltaSeconds));
    }
    m_lastCameraPos = m_camera.position;
    m_deltaSeconds = deltaSeconds;
}

std::string Application::stateJson() const
{
    const Terrain::Stats& terrain = m_terrain->stats();
    char json[512];
    std::snprintf(json, sizeof(json),
                  "{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"yaw\":%.5f,\"pitch\":%.5f,\"orbiting\":%s,"
                  "\"panel\":%s,\"captured\":%s,\"frames\":%d,\"view\":\"%s\",\"minutes\":%.1f,"
                  "\"rain\":%.2f,\"quality\":\"%s\",\"touch\":%s,\"workMs\":%.3f,\"terrainMs\":%.3f}",
                  m_camera.position.x, m_camera.position.y, m_camera.position.z, static_cast<double>(m_camera.yaw),
                  static_cast<double>(m_camera.pitch),
                  m_controller->mode() == CameraController::Mode::Orbit ? "true" : "false",
                  m_ui->visible() ? "true" : "false", m_window.cursorCaptured() ? "true" : "false", m_frameCount,
                  m_options.view.c_str(), m_time.minutes(), static_cast<double>(m_rainTarget),
                  kQualities[static_cast<std::size_t>(m_quality)].name, m_options.touch ? "true" : "false",
                  static_cast<double>(m_lastWorkMs), terrain.updateMs);
    return json;
}

void Application::touchDrag(float dx, float dy)
{
    m_touchDrag += glm::vec2(dx, dy);
}

void Application::touchPinch(float scale)
{
    m_touchPinch *= scale;
}

void Application::touchPan(float dx, float dy)
{
    m_touchPan += glm::vec2(dx, dy);
}

const char* Application::nextView()
{
    // Each view orbits slowly, as the first one does, until touched.
    static constexpr const char* kViews[] = {"glenbeigh", "ridges", "archipelago", "kerry", "shore", "kerry-south",
                                             "wicklow", "overview"};
    static constexpr const char* kLabels[] = {"Glenbeigh", "Ridges", "Archipelago", "Kerry", "Shore", "Kerry south",
                                              "Wicklow", "Ireland"};
    m_touchView = (m_touchView + 1) % static_cast<int>(std::size(kViews));
    const ViewPreset& preset = *findViewPreset(kViews[m_touchView]);
    m_options.view = preset.name;
    applyViewPreset(preset, m_camera);
    m_controller->startOrbit(glm::dvec3(preset.target), preset.azimuth, preset.elevation, preset.distance, true);
    return kLabels[m_touchView];
}

const char* Application::cycleTime()
{
    static constexpr double kMinutes[] = {21.5 * 60.0, 13.0 * 60.0, 1.5 * 60.0};
    static constexpr const char* kLabels[] = {"Golden hour", "Noon", "Night"};
    m_touchTime = (m_touchTime + 1) % static_cast<int>(std::size(kMinutes));
    m_options.fixedSun = false;
    m_time.setMinutes(kMinutes[m_touchTime]);
    return kLabels[m_touchTime];
}

const char* Application::cycleRain()
{
    static constexpr float kLevels[] = {0.0f, kLightRain, kHeavyRain};
    static constexpr const char* kLabels[] = {"Dry", "Light rain", "Heavy rain"};
    m_touchRain = (m_touchRain + 1) % static_cast<int>(std::size(kLevels));
    m_rainTarget = kLevels[m_touchRain];
    m_rainLevel = m_rainTarget > 0.0f ? m_rainTarget : m_rainLevel;
    return kLabels[m_touchRain];
}

bool Application::done() const
{
    return m_window.shouldClose();
}

void Application::toggleUi()
{
    m_ui->toggle();
}

void Application::updateWeather(float deltaSeconds)
{
    const Input& input = m_window.input();
    if (input.wasPressed(GLFW_KEY_R)) {
        const bool shift = input.isDown(GLFW_KEY_LEFT_SHIFT) || input.isDown(GLFW_KEY_RIGHT_SHIFT);
        if (shift) {
            m_rainLevel = m_rainLevel < kHeavyRain ? kHeavyRain : kLightRain;
            m_rainTarget = m_rainLevel;
        } else {
            m_rainTarget = m_rainTarget > 0.0f ? 0.0f : m_rainLevel;
        }
    }
    const float ease = 1.0f - std::exp(-deltaSeconds / kRainEaseSeconds);
    m_rainIntensity += (m_rainTarget - m_rainIntensity) * ease;
    if (std::abs(m_rainTarget - m_rainIntensity) < 1e-3f) {
        m_rainIntensity = m_rainTarget;
    }
    const float wetTarget = std::min(m_rainIntensity / kLightRain, 1.0f);
    const float wetEase = 1.0f - std::exp(-deltaSeconds / (wetTarget > m_wetness ? kWettingSeconds : kDryingSeconds));
    m_wetness += (wetTarget - m_wetness) * wetEase;
    if (std::abs(wetTarget - m_wetness) < 1e-3f) {
        m_wetness = wetTarget;
    }
    m_atmosphere.rain = m_rainIntensity;
    m_atmosphere.wetness = m_wetness;
}

void Application::renderRain()
{
    if (m_rainIntensity <= 0.0f || !m_features.rainParticles) {
        return;
    }
    const glm::dvec3& eye = m_camera.position;
    m_rain->updateGround(*m_terrain, *m_shadowShader, kHeightUnit, eye, m_heightField->groundHeight(eye.x, eye.z));
    const Wind& wind = m_sea->wind();
    const float bearing = glm::radians(wind.bearing);
    const glm::vec3 windVelocity = wind.speed * glm::vec3(std::sin(bearing), 0.0f, -std::cos(bearing));
    if (m_options.screenshotPath.empty()) {
        m_rain->simulate(m_deltaSeconds, eye, windVelocity, m_rainIntensity);
        return;
    }
    // Screenshots: still until the terrain has loaded, then a fixed-step warm-up so splashes are
    // under way, then fixed steps, so the same frames are captured every run.
    if (!m_terrain->settled()) {
        m_rain->simulate(0.0f, eye, windVelocity, m_rainIntensity);
        return;
    }
    const int steps = m_rainWarmedUp ? 1 : kRainWarmUpSteps;
    m_rainWarmedUp = true;
    for (int i = 0; i < steps; ++i) {
        m_rain->simulate(kRainFixedStep, eye, windVelocity, m_rainIntensity);
    }
}

void Application::updateTime(float deltaSeconds)
{
    if (m_options.fixedSun) {
        return;
    }
    const Input& input = m_window.input();
    if (input.wasPressed(GLFW_KEY_T)) {
        m_time.toggle();
    }
    const bool shift = input.isDown(GLFW_KEY_LEFT_SHIFT) || input.isDown(GLFW_KEY_RIGHT_SHIFT);
    const double step = shift ? 60.0 : 15.0; // minutes per press
    if (input.wasPressed(GLFW_KEY_COMMA)) {
        m_time.step(-step);
    }
    if (input.wasPressed(GLFW_KEY_PERIOD)) {
        m_time.step(step);
    }
    m_time.advance(static_cast<double>(deltaSeconds));
    const SolarAngles sun = m_time.sun();
    m_atmosphere.sunDir = sunDirection(static_cast<float>(sun.bearing), static_cast<float>(sun.elevation));

    // The stars turn once a sidereal day about the celestial pole (north, at the latitude's
    // elevation); the year term keeps the same stars from showing at the same clock time all year.
    const double turns = m_time.minutes() / 1436.07 + m_time.dayOfYear() / 365.25;
    const glm::vec3 pole = sunDirection(0.0f, static_cast<float>(kIrelandLatitude));
    m_atmosphere.starRotation = glm::mat3(glm::rotate(glm::mat4(1.0f),
                                                      static_cast<float>(turns * 2.0 * glm::pi<double>()), pole));
}

void Application::render()
{
    // The scene goes into a linear HDR target that follows the drawable size (or less, under the
    // quality's pixel-ratio cap; the tone mapping then scales it up).
    const int width = sceneWidth();
    const int height = sceneHeight();
    if (!m_scene || m_scene->width() != width || m_scene->height() != height ||
        m_scene->samples() != gl::samplesFor(sceneFormat(), m_options.window.samples)) {
        m_scene = std::make_unique<OffscreenTarget>(width, height, m_options.window.samples, sceneFormat());
        if (width != m_window.framebufferWidth()) {
            m_scene->setFilter(GL_LINEAR);
        }
    }
    m_scene->bind();

    // Refreshes the sky one cubemap face per frame while the sun moves.
    markPass(GpuPassTimer::Sky);
    m_sky->update(m_atmosphere);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = m_camera.projection(m_window.aspectRatio());
    const glm::mat4 rotationOnly(glm::mat3(m_camera.view()));
    markPass(GpuPassTimer::Other); // tile uploads
    m_terrain->update(m_camera.position, projection * m_camera.view());
    markPass(GpuPassTimer::Shadows);
    renderShadows(projection);
    markPass(GpuPassTimer::Reflection);
    renderSeabed();
    renderReflection(projection);
    markPass(GpuPassTimer::Rain);
    renderRain();

    // Within the main pass, timer queries split the GPU's render pass (costly on macOS), so its
    // parts are timed separately only when the panel asks.
    markPass(m_splitScenePass ? GpuPassTimer::Terrain : GpuPassTimer::Scene);
    m_terrainShader->use();
    setTerrainUniforms(*m_terrainShader);
    m_terrainShader->set("uDebugView", m_debug.view);
    m_terrainShader->set("uTileBorders", m_debug.tileBorders ? 1 : 0);
    if (m_debug.wireframe) {
        gl::setWireframe(true);
    }
    m_terrain->draw(*m_terrainShader, kHeightUnit, kHorizonUnit);

    // Sea after terrain so it is hidden behind land, pushed back slightly so land wins at the coast.
    markScenePart(GpuPassTimer::Sea);
    m_seaShader->use();
    setSceneUniforms(*m_seaShader);
    // Screenshots freeze the waves, so they are reproducible.
    const double waveTime = !m_options.recordDir.empty()       ? m_recordSeconds
                            : m_options.screenshotPath.empty() ? m_timer.elapsedSeconds()
                                                               : 0.0;
    m_sea->apply(*m_seaShader, m_camera.position, waveTime, kRippleUnit);
    m_seabed->apply(*m_seaShader, kSeabedUnit);
    m_reflection->bindTexture(kReflectionUnit);
    m_seaShader->set("uReflection", static_cast<int>(kReflectionUnit));
    m_seaShader->set("uViewportSize", glm::vec2(static_cast<float>(width), static_cast<float>(height)));
    m_seaShader->set("uExaggeration", m_heightField->verticalExaggeration());
    m_shadows->apply(*m_seaShader, kShadowUnit);
    m_terrain->bindCoastMask(kCoastUnit);
    m_sky->applyEnvironment(*m_seaShader, kSkyUnit);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    m_sea->draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    gl::setWireframe(false);

    // Skybox last, at the far plane, so it is only shaded where nothing else was drawn.
    const float pixelAngle = m_camera.verticalFov(m_window.aspectRatio()) / static_cast<float>(height);
    markScenePart(GpuPassTimer::Sky);
    m_sky->setDisplay(displayExposure(), m_options.agx ? 1 : 0);
    m_sky->drawBackground(glm::inverse(projection * rotationOnly), m_atmosphere, pixelAngle);

    // Rain after everything opaque, blended into the HDR target.
    markScenePart(GpuPassTimer::Rain);
    if (m_rainIntensity > 0.0f && m_features.rainParticles) {
        RainLighting lighting;
        lighting.lightDir = m_atmosphere.lightDir();
        lighting.lightColour = m_atmosphere.lightColour();
        lighting.ambient = m_sky->averageIrradiance();
        lighting.night = m_atmosphere.night();
        lighting.overcast = m_atmosphere.overcast();
        lighting.skyUnit = static_cast<int>(kSkyUnit);
        lighting.displayExposure = displayExposure();
        lighting.displayToneMapper = m_options.agx ? 1 : 0;
        m_sky->bindCubemap(kSkyUnit);
        m_rain->draw(m_camera.view(), projection, glm::vec2(static_cast<float>(width), static_cast<float>(height)),
                     pixelAngle, m_cameraVelocity, lighting);
    }

    markPass(GpuPassTimer::ToneMap);
    toneMap();
    markPass(GpuPassTimer::Ui);
    m_ui->draw(); // into the final framebuffer, after tone mapping
}

void Application::markPass(GpuPassTimer::Pass pass)
{
    if (m_passTiming) {
        m_passTimer->mark(pass);
    }
}

void Application::markScenePart(GpuPassTimer::Pass pass)
{
    if (m_splitScenePass) {
        markPass(pass);
    }
}

void Application::collectPassTimes()
{
    for (const GpuPassTimer::Result& r : m_passTimer->collect()) {
        // Smoothed over a few dozen frames, so the readout is steady enough to read.
        constexpr float kSmoothing = 0.05f;
        for (std::size_t i = 0; i < m_passMs.size(); ++i) {
            m_passMs[i] += (r.ms[i] - m_passMs[i]) * kSmoothing;
        }
        m_passTotalMs += (r.total - m_passTotalMs) * kSmoothing;
        if (m_gpuTimer) {
            m_gpuMs[static_cast<std::size_t>(r.frame - 1)] = r.total;
        }
    }
}

void Application::toneMap()
{
    m_scene->resolve();
    if (m_offscreen) {
        m_offscreen->bind();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_window.framebufferWidth(), m_window.framebufferHeight());
    }
    glDisable(GL_DEPTH_TEST);
    m_toneMapShader->use();
    m_scene->bindTexture(kSceneUnit);
    m_toneMapShader->set("uScene", static_cast<int>(kSceneUnit));
    m_toneMapShader->set("uExposure", displayExposure());
    m_toneMapShader->set("uToneMapper", m_options.agx ? 1 : 0);
    m_toneMapShader->set("uFxaa", fxaaEnabled() ? 1 : 0);
    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

bool Application::fxaaEnabled() const
{
    if (m_options.fxaa >= 0) {
        return m_options.fxaa == 1;
    }
    return !m_scene || m_scene->samples() <= 1;
}

void Application::applyQuality(int quality)
{
    m_quality = quality;
    const Quality& q = kQualities[static_cast<std::size_t>(quality)];
    m_maxPixelRatio = q.maxPixelRatio;
    m_reflectionDivisor = q.reflectionDivisor;
    m_options.window.samples = q.msaa;
    if (m_shadows->resolution() != q.shadowResolution) {
        m_shadows = std::make_unique<ShadowMaps>(q.shadowResolution);
    }
    m_rain->setDropFraction(q.rainDrops);
    m_terrain->setLodRange(q.lodRange);
    m_terrain->setGenerationBudget(q.tileBudgetMs);
    m_terrain->setSlowDevice(quality == 0 || m_options.touch);
}

int Application::sceneWidth() const
{
    const float scale = std::min(1.0f, m_maxPixelRatio / m_window.pixelRatio());
    return std::max(static_cast<int>(std::lround(static_cast<float>(m_window.framebufferWidth()) * scale)), 1);
}

int Application::sceneHeight() const
{
    const float scale = std::min(1.0f, m_maxPixelRatio / m_window.pixelRatio());
    return std::max(static_cast<int>(std::lround(static_cast<float>(m_window.framebufferHeight()) * scale)), 1);
}

float Application::displayExposure() const
{
    const float base = m_options.agx ? kAgxExposure : kNeutralExposure;
    const float ev = m_options.exposureEv + kNightAdaptationEv * m_atmosphere.night() +
                     kOvercastAdaptationEv * m_atmosphere.overcast() * (1.0f - m_atmosphere.night());
    return base * std::exp2(ev);
}

void Application::renderShadows(const glm::mat4& projection)
{
    // Twilight has no direct light to shadow; the maps keep their last contents meanwhile.
    const glm::vec3 light = m_atmosphere.lightColour();
    if (!m_features.shadows || std::max(light.r, std::max(light.g, light.b)) < 1e-4f) {
        return;
    }
    const glm::dvec3& eye = m_camera.position;
    const double ground = m_heightField->groundHeight(eye.x, eye.z);
    m_shadows->update(eye, m_camera.forward(), m_camera.verticalFov(m_window.aspectRatio()),
                      projection[1][1] / projection[0][0],
                      static_cast<float>(eye.y - ground), m_atmosphere.lightDir());

    // Errors from earlier in the frame are counted by validateFrame(), not against the shadows.
    const bool validating = m_options.validateMinTiles > 0;
    if (validating) {
        drainGlErrors(m_glErrorCount, m_firstGlError);
    }
    m_shadowShader->use();
    m_shadowShader->set("uCameraHeight", static_cast<float>(eye.y));
    for (int cascade = 0; cascade < ShadowMaps::kCascades; ++cascade) {
        m_shadows->begin(cascade);
        m_shadowShader->set("uShadowViewProj", m_shadows->viewProjection(cascade));
        m_terrain->drawShadowCasters(*m_shadowShader, kHeightUnit, m_shadows->viewProjection(cascade),
                                     ShadowMaps::lodScale(cascade));
    }
    m_shadows->end();
    if (validating) {
        drainGlErrors(m_shadowGlErrorCount, m_firstShadowGlError);
        ++m_shadowFrames;
        m_shadowCasterFrames += m_terrain->stats().shadowDrawCalls > 0 ? 1 : 0;
    }
}

void Application::setTerrainUniforms(Shader& shader)
{
    setSceneUniforms(shader);
    m_shadows->apply(shader, kShadowUnit);
    shader.set("uExaggeration", m_heightField->verticalExaggeration());
    m_materialColour->bind(kColourUnit);
    m_materialNormal->bind(kNormalUnit);
    m_sky->applyEnvironment(shader, kSkyUnit);
}

void Application::renderReflection(const glm::mat4& projection)
{
    // A fraction of the scene, but never so small that coasts mirrored in it turn to steps
    // (a phone's scene at Low quality is only about 400 pixels across).
    const float shorter = static_cast<float>(std::min(sceneWidth(), sceneHeight()));
    const float scale = std::max(1.0f / static_cast<float>(m_reflectionDivisor),
                                 std::min(kMinReflectionSide / shorter, 1.0f));
    m_reflection->resize(std::max(static_cast<int>(std::lround(static_cast<float>(sceneWidth()) * scale)), 1),
                         std::max(static_cast<int>(std::lround(static_cast<float>(sceneHeight()) * scale)), 1));
    // Camera-relative position to its mirror image in the sea (y = 0): y -> -y - 2 * camera height.
    glm::mat4 mirror(1.0f);
    mirror[1][1] = -1.0f;
    mirror[3][1] = -2.0f * static_cast<float>(m_camera.position.y);

    m_reflection->begin(); // clears: with reflections off, the sea reflects only the sky
    if (!m_features.reflections) {
        m_reflection->end();
        return;
    }
    m_reflectionShader->use();
    setTerrainUniforms(*m_reflectionShader);
    m_reflectionShader->set("uMirror", mirror);
    m_terrain->drawReflection(*m_reflectionShader, kHeightUnit, kHorizonUnit,
                              projection * m_camera.view() * mirror, kReflectionLodScale);
    m_reflection->end();
}

void Application::renderSeabed()
{
    m_seabed->update(m_camera.position);
    m_shadowShader->use();
    m_shadowShader->set("uCameraHeight", static_cast<float>(m_camera.position.y));
    for (int layer = 0; layer < SeabedMap::kLayers; ++layer) {
        m_seabed->begin(layer);
        m_shadowShader->set("uShadowViewProj", m_seabed->viewProjection(layer));
        m_terrain->drawShadowCasters(*m_shadowShader, kHeightUnit, m_seabed->viewProjection(layer),
                                     SeabedMap::lodScale(layer));
    }
    m_seabed->end();
}

void Application::setSceneUniforms(Shader& shader)
{
    // Camera-relative rendering: the view has no translation and geometry arrives relative to the
    // camera, so precision does not degrade far from the origin.
    shader.set("uView", m_camera.view());
    shader.set("uProjection", m_camera.projection(m_window.aspectRatio()));
    shader.set("uCameraOffset", glm::vec3(m_camera.position));
    shader.set("uCameraXZ", glm::vec2(m_camera.position.x, m_camera.position.z));
    shader.set("uCameraHeight", static_cast<float>(m_camera.position.y));
    m_atmosphere.apply(shader, static_cast<float>(m_camera.position.y));
    shader.set("uShadowsOff", m_features.shadows ? 0 : 1);
    shader.set("uDisplayExposure", displayExposure()); // shaders that tone map themselves
    shader.set("uDisplayToneMapper", m_options.agx ? 1 : 0);
    shader.set("uHorizonShadowsOff", m_features.horizonShadows ? 0 : 1);
}

void Application::recordFrameStats(float frameMs, float cpuMs)
{
    const Terrain::Stats& stats = m_terrain->stats();
    m_frameMs.push_back(frameMs);
    m_frameHistory[m_historyNext] = frameMs;
    m_historyNext = (m_historyNext + 1) % m_frameHistory.size();
    m_cpuMs.push_back(cpuMs);
    m_gpuMs.push_back(-1.0f);
    m_terrainMs.push_back(static_cast<float>(stats.updateMs));
    m_triangleSum += static_cast<double>(stats.triangles);
    m_drawCallSum += stats.drawCalls;
    m_peakResidentTiles = std::max(m_peakResidentTiles, stats.residentTiles);
}

void Application::printRunStats() const
{
    const Terrain::Stats& stats = m_terrain->stats();
    std::printf("Terrain: avg %.2fM triangles, %.0f draw calls; peak %d resident tiles, %llu generated\n",
                m_triangleSum / std::max(m_frameCount, 1) / 1e6, m_drawCallSum / std::max(m_frameCount, 1),
                m_peakResidentTiles, static_cast<unsigned long long>(stats.tilesGenerated));
    // Skip the first second (startup tile flood, sky cubemap, shader warm-up) so the numbers
    // reflect steady state, including streaming while moving.
    std::size_t skip = 0;
    for (float elapsed = 0.0f; skip < m_frameMs.size() && elapsed < 1000.0f; ++skip) {
        elapsed += m_frameMs[skip];
    }
    if (m_frameMs.size() <= skip) {
        return;
    }
    std::vector<float> times(m_frameMs.begin() + static_cast<std::ptrdiff_t>(skip), m_frameMs.end());

    // Spikes are counted against fixed 120 Hz and 60 Hz budgets: a ratio to the recent median
    // means little when the median over open sea is ~2 ms.
    const auto over = [&times](float budgetMs) {
        return static_cast<long>(
            std::count_if(times.begin(), times.end(), [budgetMs](float t) { return t > budgetMs; }));
    };
    const long over120Hz = over(8.3f);
    const long over60Hz = over(16.7f);
    double totalMs = 0.0;
    for (float t : times) {
        totalMs += static_cast<double>(t);
    }

    std::sort(times.begin(), times.end());
    const auto percentile = [&times](double p) {
        return times[std::min(times.size() - 1, static_cast<std::size_t>(p * static_cast<double>(times.size())))];
    };
    std::printf("Frame times after the first second (%zu frames): median %.2f ms, p99 %.2f ms, "
                "max %.2f ms; %ld over 8.3 ms, %ld over 16.7 ms\n",
                times.size(), static_cast<double>(percentile(0.5)), static_cast<double>(percentile(0.99)),
                static_cast<double>(times.back()), over120Hz, over60Hz);
    // With vsync, swaps can return early and then block for two refreshes, so single frames look
    // slow while every refresh still gets a new frame. The average rate shows dropped refreshes.
    // Not in a browser, which presents after frame() returns and reports no refresh rate.
    if (m_options.window.vsync && m_window.refreshRate() > 0) {
        std::printf("Presentation: %.1f frames per second on average (display %d Hz)\n",
                    static_cast<double>(times.size()) * 1000.0 / totalMs, m_window.refreshRate());
    }

    // Time before the swap (CPU work plus any driver back-pressure) and, if timed, GPU work; the
    // rest of a slow frame is the swap waiting to present.
    const auto summary = [skip](const std::vector<float>& values, const char* label) {
        std::vector<float> known;
        for (std::size_t i = skip; i < values.size(); ++i) {
            if (values[i] >= 0.0f) {
                known.push_back(values[i]);
            }
        }
        if (known.empty()) {
            std::printf("%s: no samples\n", label);
            return;
        }
        std::sort(known.begin(), known.end());
        const auto at = [&known](double p) {
            const auto index = static_cast<std::size_t>(p * static_cast<double>(known.size()));
            return static_cast<double>(known[std::min(known.size() - 1, index)]);
        };
        std::printf("%s: median %.2f ms, p99 %.2f ms, max %.2f ms\n", label, at(0.5), at(0.99),
                    static_cast<double>(known.back()));
    };
    summary(m_cpuMs, "Before swap (CPU work and driver waits)");
    if (m_gpuTimer) {
        summary(m_gpuMs, "GPU time (timer queries)");
    }

    std::vector<float> terrain(m_terrainMs.begin() + static_cast<std::ptrdiff_t>(skip), m_terrainMs.end());
    double terrainSum = 0.0;
    for (float t : terrain) {
        terrainSum += static_cast<double>(t);
    }
    std::printf("Terrain main-thread work (uploads and selection): avg %.3f ms, max %.3f ms\n",
                terrainSum / static_cast<double>(terrain.size()),
                static_cast<double>(*std::max_element(terrain.begin(), terrain.end())));
}

void Application::drainGlErrors(int& count, unsigned& first)
{
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        if (count++ == 0) {
            first = error;
        }
    }
}

void Application::validateFrame()
{
    drainGlErrors(m_glErrorCount, m_firstGlError);
    const Terrain::Stats& stats = m_terrain->stats();
    m_terrainReady = m_terrainReady || (stats.missingNodes == 0 && stats.cameraCovered);
    if (!m_terrainReady) {
        return;
    }
    ++m_validatedFrames;
    m_holeFrames += stats.missingNodes > 0 ? 1 : 0;
    m_uncoveredFrames += stats.cameraCovered ? 0 : 1;
}

bool Application::reportValidation() const
{
    const Terrain::Stats& stats = m_terrain->stats();
    const auto minTiles = static_cast<std::uint64_t>(m_options.validateMinTiles);
    struct Check {
        const char* name;
        bool passed;
    };
    const Check checks[] = {
        {"run completed", m_finished},
        {"no GL errors", m_glErrorCount == 0},
        {"no GL errors in shadow passes", m_shadowGlErrorCount == 0},
        {"shadow casters drawn", m_shadowCasterFrames > 0},
        {"initial terrain loaded", m_terrainReady && m_validatedFrames > 0},
        {"no frames with holes in view", m_holeFrames == 0},
        {"ground under the camera always loaded", m_uncoveredFrames == 0},
        {"enough tiles generated", stats.tilesGenerated >= minTiles},
        {"enough tiles resident", m_peakResidentTiles >= m_options.validateMinTiles},
        // Only with --rain: every simulation step checked by a query wrote the whole drop pool.
        {"rain feedback wrote every drop",
         m_options.rain <= 0.0f || (m_rain->checkedSteps() > 0 && m_rain->failedSteps() == 0)},
    };
    std::printf("Validation (%d frames checked after the initial load): %d GL errors (first 0x%04X), "
                "%d frames with holes, %d frames without ground under the camera, %llu tiles generated, "
                "peak %d resident, minimum %d; shadow passes in %d frames (%d with casters), "
                "%d GL errors in them (first 0x%04X)\n",
                m_validatedFrames, m_glErrorCount, m_firstGlError, m_holeFrames, m_uncoveredFrames,
                static_cast<unsigned long long>(stats.tilesGenerated), m_peakResidentTiles,
                m_options.validateMinTiles, m_shadowFrames, m_shadowCasterFrames, m_shadowGlErrorCount,
                m_firstShadowGlError);
    if (m_options.rain > 0.0f) {
        std::printf("Rain: %d drops drawn; %d simulation steps checked, %d short\n", m_rain->activeDrops(),
                    m_rain->checkedSteps(), m_rain->failedSteps());
    }
    bool passed = true;
    for (const Check& check : checks) {
        std::printf("  %s %s\n", check.passed ? "PASS" : "FAIL", check.name);
        passed = passed && check.passed;
    }
    return passed;
}

void Application::flushFrameLog(bool all)
{
    if (!m_frameLog) {
        return;
    }
    // A row is written once its GPU time is known, or left blank if the query never completed.
    constexpr int kMaxWaitFrames = 16;
    while (!m_pendingLogRows.empty()) {
        const auto& [frame, row] = m_pendingLogRows.front();
        const float gpu = m_gpuMs[static_cast<std::size_t>(frame - 1)];
        if (m_gpuTimer && gpu < 0.0f && !all && frame > m_frameCount - kMaxWaitFrames) {
            break;
        }
        if (gpu >= 0.0f) {
            std::fprintf(m_frameLog, "%s%.3f\n", row.c_str(), static_cast<double>(gpu));
        } else {
            std::fprintf(m_frameLog, "%s\n", row.c_str());
        }
        m_pendingLogRows.pop_front();
    }
}

void Application::updateTitle()
{
    char speed[32];
    if (m_controller->autoflying()) {
        std::snprintf(speed, sizeof(speed), "autofly");
    } else if (m_controller->mode() == CameraController::Mode::FreeFly) {
        const double v = static_cast<double>(m_controller->effectiveSpeed());
        if (v >= 1000.0) {
            std::snprintf(speed, sizeof(speed), "%.1f km/s", v / 1000.0);
        } else {
            std::snprintf(speed, sizeof(speed), "%.0f m/s", v);
        }
    } else {
        std::snprintf(speed, sizeof(speed), "orbit");
    }
    const Terrain::Stats& stats = m_terrain->stats();
    char streaming[48] = ""; // " (+N streaming)" is at most 35 bytes
    if (stats.outstandingTiles > 0) {
        std::snprintf(streaming, sizeof(streaming), " (+%zu streaming)", stats.outstandingTiles);
    }
    std::string time = m_options.fixedSun ? "fixed sun" : m_time.label();
    if (m_rainIntensity > 0.0f) {
        time += "  |  rain " + std::to_string(static_cast<int>(std::lround(m_rainIntensity * 100.0f))) + "%";
    }
    char title[320];
    std::snprintf(title, sizeof(title), "%s  |  %s  |  %.0f FPS  |  %.2f ms  |  %.2fM tris  |  %d draws  |  %d tiles%s  |  %s",
                  m_options.window.title.c_str(), time.c_str(), m_timer.framesPerSecond(),
                  m_timer.frameTimeMs(), static_cast<double>(stats.triangles) / 1e6, stats.drawCalls,
                  stats.residentTiles, streaming, speed);
    m_window.setTitle(title);
}

} // namespace emerald
