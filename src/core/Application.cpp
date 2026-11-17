#include "core/Application.hpp"

#include "core/Screenshot.hpp"
#include "render/GpuTimer.hpp"
#include "render/Shader.hpp"
#include "render/SeaPlane.hpp"
#include "render/Sky.hpp"
#include "render/Terrain.hpp"
#include "render/TextureArray.hpp"
#include "render/ViewPresets.hpp"
#include "world/HeightField.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace emerald {

namespace {


// Metres. The sea plane reaches the far plane (corners at ~0.99x far) so it meets the horizon.
constexpr float kFarPlane = 15000e3f;
constexpr float kSeaHalfSize = 0.7f * kFarPlane;
constexpr float kAutoflySpeed = 2500.0f; // metres per second, ~150 km a minute

// Material layers, in the order terrain.frag indexes them (kGrass, kSand, kHeath, kRock).
constexpr const char* kMaterials[] = {"Grass004", "Ground054", "Ground037", "Rock058"};
constexpr GLuint kColourUnit = 0;
constexpr GLuint kNormalUnit = 1;
constexpr GLuint kCoastUnit = 2;
constexpr GLuint kHeightUnit = 3;
constexpr GLuint kSkyUnit = 4;

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
        "  --frames <n>          Exit after rendering n frames\n"
        "  --screenshot <file>   Save the last frame as a PNG, then exit\n"
        "  --view <name>         Start from a camera preset: %s\n"
        "  --sun-bearing <deg>   Sun compass bearing, 0 = north (default 225)\n"
        "  --sun-elevation <deg> Sun elevation above the horizon (default 22)\n"
        "  --seed <n>            Procedural world seed (default 1)\n"
        "  --autofly <s>         Fly straight ahead for s seconds, print stats, exit\n"
        "  --frame-log <file>    Write per-frame phase timings (CSV)\n"
        "  --gpu-timing          Also measure GPU time per frame (adds ~0.4 ms on macOS)\n"
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
        } else if (std::strcmp(arg, "--sun-bearing") == 0 && hasValue) {
            options.sunBearing = parseFloat(arg, argv[++i], -360.0f, 360.0f);
        } else if (std::strcmp(arg, "--sun-elevation") == 0 && hasValue) {
            options.sunElevation = parseFloat(arg, argv[++i], -10.0f, 90.0f);
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
    return true;
}

Application::Application(AppOptions options)
    : m_options(std::move(options))
    , m_window(m_options.window)
{
    glViewport(0, 0, m_window.framebufferWidth(), m_window.framebufferHeight());
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const std::filesystem::path shaderDir = m_options.assetDir / "shaders";
    m_sky = std::make_unique<Sky>(shaderDir);
    m_terrainShader =
        std::make_unique<Shader>(shaderDir / "terrain.vert", shaderDir / "terrain.frag");
    m_seaShader = std::make_unique<Shader>(shaderDir / "sea.vert", shaderDir / "sea.frag");
    if (m_options.gpuTiming) {
        m_gpuTimer = std::make_unique<GpuTimer>();
    }

    HeightFieldConfig heightConfig;
    heightConfig.seed = m_options.seed;
    m_heightField = std::make_unique<HeightField>(
        m_options.assetDir / "textures" / "ireland-heightmap.png", heightConfig);
    std::printf("World: seed %u\n", m_options.seed);

    m_terrain = std::make_unique<Terrain>(*m_heightField);
    m_sea = std::make_unique<SeaPlane>(kSeaHalfSize);

    const std::filesystem::path materialDir = m_options.assetDir / "textures" / "materials";
    m_materialColour = std::make_unique<TextureArray>(materialFiles(materialDir, "Color"),
                                                      TextureArray::Encoding::Srgb);
    m_materialNormal = std::make_unique<TextureArray>(materialFiles(materialDir, "NormalGL"),
                                                      TextureArray::Encoding::Linear);
    std::printf("Materials: %zu layers, %d mip levels, %s, anisotropy %.0fx\n",
                std::size(kMaterials), m_materialColour->mipLevels(),
                m_materialColour->trilinear() ? "trilinear" : "NOT trilinear",
                static_cast<double>(m_materialColour->anisotropy()));

    m_terrainShader->use();
    m_terrainShader->set("uMaterialColour", static_cast<int>(kColourUnit));
    m_terrainShader->set("uMaterialNormal", static_cast<int>(kNormalUnit));
    m_seaShader->use();
    m_seaShader->set("uCoastMask", static_cast<int>(kCoastUnit));
    m_seaShader->set("uTerrainSize", m_terrain->demSize());
    m_seaShader->set("uSeaRadius", kSeaHalfSize);

    if (!m_options.frameLogPath.empty()) {
        m_frameLog = openForWriting(m_options.frameLogPath);
        if (!m_frameLog) {
            throw std::runtime_error("Could not open frame log " + m_options.frameLogPath);
        }
        std::fprintf(m_frameLog, "frame,total_ms,events_ms,update_ms,terrain_ms,render_ms,swap_ms,"
                                 "uploads,outstanding,resident,triangles,draws,x,y,z,focused,gpu_ms\n");
    }
    m_atmosphere.sunDir = sunDirection(m_options.sunBearing, m_options.sunElevation);
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

    warmUp();
}

void Application::warmUp()
{
    // One-off work that otherwise lands in the first frame (~100-200 ms): macOS finishing the
    // window on the first event poll, the sky cubemap and its SH readback, first-use pipeline
    // compilation and the first present. Done here, before the frame loop and its timer start.
    const auto start = std::chrono::steady_clock::now();
    m_window.pollEvents();
    m_sky->update(m_atmosphere);
    render();
    m_window.swapBuffers();
    glFinish();
    std::printf("Warm-up: %.0f ms before the first frame\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

Application::~Application()
{
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
            if (!saveFramebufferPng(m_window.framebufferWidth(), m_window.framebufferHeight(),
                                    m_options.screenshotPath)) {
                return EXIT_FAILURE;
            }
        }

        m_window.swapBuffers();
        const auto afterSwap = Clock::now();

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
            m_window.requestClose();
        }
    }

    std::printf("Rendered %d frames, last average %.1f FPS (%.2f ms/frame)\n",
                m_frameCount, m_timer.framesPerSecond(), m_timer.frameTimeMs());
    printRunStats();
    return EXIT_SUCCESS;
}

void Application::update(float deltaSeconds)
{
    m_controller->update(m_window, deltaSeconds);
}

void Application::render()
{
    // Re-renders the sky cubemap only when the sun has changed.
    m_sky->update(m_atmosphere);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = m_camera.projection(m_window.aspectRatio());
    const glm::mat4 rotationOnly(glm::mat3(m_camera.view()));
    m_terrain->update(m_camera.position, projection * m_camera.view());

    m_terrainShader->use();
    setSceneUniforms(*m_terrainShader);
    m_terrainShader->set("uExaggeration", m_heightField->verticalExaggeration());
    m_materialColour->bind(kColourUnit);
    m_materialNormal->bind(kNormalUnit);
    m_sky->applyEnvironment(*m_terrainShader, kSkyUnit);
    m_terrain->draw(*m_terrainShader, kHeightUnit);

    // Sea after terrain so it is hidden behind land, pushed back slightly so land wins at the coast.
    m_seaShader->use();
    setSceneUniforms(*m_seaShader);
    m_terrain->bindCoastMask(kCoastUnit);
    m_sky->applyEnvironment(*m_seaShader, kSkyUnit);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    m_sea->draw();
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Skybox last, at the far plane, so it is only shaded where nothing else was drawn.
    m_sky->drawBackground(glm::inverse(projection * rotationOnly), m_atmosphere);
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
    char title[256];
    std::snprintf(title, sizeof(title), "%s  |  %.0f FPS  |  %.2f ms  |  %.2fM tris  |  %d draws  |  %d tiles%s  |  %s",
                  m_options.window.title.c_str(), m_timer.framesPerSecond(), m_timer.frameTimeMs(),
                  static_cast<double>(stats.triangles) / 1e6, stats.drawCalls, stats.residentTiles,
                  streaming, speed);
    m_window.setTitle(title);
}

} // namespace emerald
