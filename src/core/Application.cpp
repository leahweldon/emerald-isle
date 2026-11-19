#include "core/Application.hpp"

#include "core/Screenshot.hpp"
#include "render/GpuTimer.hpp"
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
#include "world/HeightField.hpp"

#include <glad/gl.h>
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
constexpr GLenum kSceneFormat = GL_R11F_G11F_B10F;
// Linear scale before each tone curve at --exposure 0, fitted so noon terrain, sea and sky match
// the earlier display transform (linear up to 0.8) as closely as the curve allows.
constexpr float kNeutralExposure = 1.18f;
constexpr float kAgxExposure = 1.16f;
// Eye adaptation: stops of extra exposure at full night (with Atmosphere::night()), so moonlit
// land stays readable through the tone curve's toe.
constexpr float kNightAdaptationEv = 1.0f;
// And under full cloud, which is darker than clear sky with the sun out.
constexpr float kOvercastAdaptationEv = 0.5f;
// Rain levels for Shift+R, and the time constant of the ease between intensities (seconds).
constexpr float kLightRain = 0.3f;
constexpr float kHeavyRain = 1.0f;
constexpr float kRainEaseSeconds = 1.2f;
// The ground is soaked by light rain already; it wets over a couple of seconds, dries more slowly.
constexpr float kWettingSeconds = 2.0f;
constexpr float kDryingSeconds = 6.0f;
// Screenshot runs step the rain at this fixed rate, after a warm-up, so captures repeat exactly.
constexpr float kRainFixedStep = 1.0f / 60.0f;
constexpr int kRainWarmUpSteps = 90;
// The reflection is rendered at this fraction of the screen size, and with this level-of-detail
// scale (< 1 is coarser): rippled and seen at grazing angles, it does not need more.
constexpr int kReflectionDivisor = 2;
constexpr float kReflectionLodScale = 0.5f;

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
        } else if (std::strcmp(arg, "--tone-mapper") == 0 && hasValue) {
            const std::string name = argv[++i];
            if (name != "neutral" && name != "agx") {
                throw std::invalid_argument("Invalid value for --tone-mapper: " + name + " (neutral or agx)");
            }
            options.agx = name == "agx";
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

    // A screenshot run needs a few frames so the swap chain has settled.
    if (!options.screenshotPath.empty() && options.frameLimit == 0) {
        options.frameLimit = 10;
    }
    // Runs that end by themselves keep the sun still, so they are reproducible.
    options.timeRunning = options.frameLimit == 0 && options.autoflySeconds <= 0.0f;
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
    glEnable(GL_MULTISAMPLE);
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
    m_shadows = std::make_unique<ShadowMaps>();
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
    if (m_options.gpuTiming) {
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
    // until there is input, so screenshots are reproducible.
    if (m_options.view.empty()) {
        const ViewPreset& overview = *findViewPreset("overview");
        m_controller->startOrbit(overview.target, overview.azimuth, overview.elevation,
                                 overview.distance, true);
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
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    while (!m_window.shouldClose()) {
        const auto frameStart = Clock::now();
        const float delta = m_timer.tick();

        m_window.input().beginFrame();
        m_window.pollEvents();
        const auto afterEvents = Clock::now();
        update(delta);
        const auto afterUpdate = Clock::now();
        ++m_frameCount;
        if (m_gpuTimer) {
            m_gpuTimer->begin(m_frameCount);
        }
        render();
        if (m_gpuTimer) {
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
        const int counted = screenshot ? m_settledFrames : m_frameCount;
        const bool autoflyDone = m_options.autoflySeconds > 0.0f &&
                                 m_timer.elapsedSeconds() >= static_cast<double>(m_options.autoflySeconds);
        const bool lastFrame = autoflyDone ||
                               (m_options.frameLimit > 0 && (counted >= m_options.frameLimit || timedOut));
        if (lastFrame && !m_options.screenshotPath.empty()) {
            // Read the back buffer before it is presented, so the capture matches this frame.
            const unsigned source = m_offscreen ? m_offscreen->readFramebuffer() : 0;
            if (!saveFramebufferPng(source, m_window.framebufferWidth(), m_window.framebufferHeight(),
                                    m_options.screenshotPath)) {
                return EXIT_FAILURE;
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
    }

    std::printf("Rendered %d frames, last average %.1f FPS (%.2f ms/frame)\n",
                m_frameCount, m_timer.framesPerSecond(), m_timer.frameTimeMs());
    printRunStats();
    if (m_options.validateMinTiles > 0 && !reportValidation()) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void Application::update(float deltaSeconds)
{
    m_controller->update(m_window, deltaSeconds);
    updateTime(deltaSeconds);
    updateWeather(deltaSeconds);
    if (deltaSeconds > 0.0f) {
        m_cameraVelocity = glm::vec3((m_camera.position - m_lastCameraPos) / static_cast<double>(deltaSeconds));
    }
    m_lastCameraPos = m_camera.position;
    m_deltaSeconds = deltaSeconds;
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
    if (m_rainIntensity <= 0.0f) {
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
    // The scene goes into a linear HDR target that follows the drawable size.
    const int width = m_window.framebufferWidth();
    const int height = m_window.framebufferHeight();
    if (!m_scene || m_scene->width() != width || m_scene->height() != height) {
        m_scene = std::make_unique<OffscreenTarget>(width, height, m_options.window.samples, kSceneFormat);
    }
    m_scene->bind();

    // Refreshes the sky one cubemap face per frame while the sun moves.
    m_sky->update(m_atmosphere);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = m_camera.projection(m_window.aspectRatio());
    const glm::mat4 rotationOnly(glm::mat3(m_camera.view()));
    m_terrain->update(m_camera.position, projection * m_camera.view());
    renderShadows(projection);
    renderSeabed();
    renderReflection(projection);
    renderRain();

    m_terrainShader->use();
    setTerrainUniforms(*m_terrainShader);
    m_terrain->draw(*m_terrainShader, kHeightUnit, kHorizonUnit);

    // Sea after terrain so it is hidden behind land, pushed back slightly so land wins at the coast.
    m_seaShader->use();
    setSceneUniforms(*m_seaShader);
    // Screenshots freeze the waves, so they are reproducible.
    const double waveTime = m_options.screenshotPath.empty() ? m_timer.elapsedSeconds() : 0.0;
    m_sea->apply(*m_seaShader, m_camera.position, waveTime, kRippleUnit);
    m_seabed->apply(*m_seaShader, kSeabedUnit);
    m_reflection->bindTexture(kReflectionUnit);
    m_seaShader->set("uReflection", static_cast<int>(kReflectionUnit));
    m_seaShader->set("uViewportSize", glm::vec2(static_cast<float>(m_window.framebufferWidth()),
                                                static_cast<float>(m_window.framebufferHeight())));
    m_seaShader->set("uExaggeration", m_heightField->verticalExaggeration());
    m_shadows->apply(*m_seaShader, kShadowUnit);
    m_terrain->bindCoastMask(kCoastUnit);
    m_sky->applyEnvironment(*m_seaShader, kSkyUnit);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    m_sea->draw();
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Skybox last, at the far plane, so it is only shaded where nothing else was drawn.
    const float pixelAngle = m_camera.fovY / static_cast<float>(m_window.framebufferHeight());
    m_sky->drawBackground(glm::inverse(projection * rotationOnly), m_atmosphere, pixelAngle);

    // Rain after everything opaque, blended into the HDR target.
    if (m_rainIntensity > 0.0f) {
        RainLighting lighting;
        lighting.lightDir = m_atmosphere.lightDir();
        lighting.lightColour = m_atmosphere.lightColour();
        lighting.ambient = m_sky->averageIrradiance();
        lighting.night = m_atmosphere.night();
        lighting.overcast = m_atmosphere.overcast();
        m_rain->draw(m_camera.view(), projection, glm::vec2(static_cast<float>(width), static_cast<float>(height)),
                     pixelAngle, m_cameraVelocity, lighting);
    }

    toneMap();
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
    const float base = m_options.agx ? kAgxExposure : kNeutralExposure;
    const float ev = m_options.exposureEv + kNightAdaptationEv * m_atmosphere.night() +
                     kOvercastAdaptationEv * m_atmosphere.overcast() * (1.0f - m_atmosphere.night());
    m_toneMapShader->set("uExposure", base * std::exp2(ev));
    m_toneMapShader->set("uToneMapper", m_options.agx ? 1 : 0);
    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Application::renderShadows(const glm::mat4& projection)
{
    // Twilight has no direct light to shadow; the maps keep their last contents meanwhile.
    const glm::vec3 light = m_atmosphere.lightColour();
    if (std::max(light.r, std::max(light.g, light.b)) < 1e-4f) {
        return;
    }
    const glm::dvec3& eye = m_camera.position;
    const double ground = m_heightField->groundHeight(eye.x, eye.z);
    m_shadows->update(eye, m_camera.forward(), m_camera.fovY, projection[1][1] / projection[0][0],
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
    m_reflection->resize(std::max(m_window.framebufferWidth() / kReflectionDivisor, 1),
                         std::max(m_window.framebufferHeight() / kReflectionDivisor, 1));
    // Camera-relative position to its mirror image in the sea (y = 0): y -> -y - 2 * camera height.
    glm::mat4 mirror(1.0f);
    mirror[1][1] = -1.0f;
    mirror[3][1] = -2.0f * static_cast<float>(m_camera.position.y);

    m_reflection->begin();
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
}

void Application::recordFrameStats(float frameMs, float cpuMs)
{
    const Terrain::Stats& stats = m_terrain->stats();
    m_frameMs.push_back(frameMs);
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
    if (m_options.window.vsync) {
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
