#include "core/Application.hpp"

#include "core/Screenshot.hpp"
#include "render/Shader.hpp"
#include "render/SkyGradient.hpp"
#include "render/TerrainMesh.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace emerald {

namespace {

// Towards the sun: from the north-west, the usual relief-shading direction.
const glm::vec3 kSunDir = glm::normalize(glm::vec3(-0.5f, 0.7f, -0.5f));
constexpr float kOrbitSpeed = 0.05f; // radians per second
// Caps per-frame motion so startup stalls do not jump the camera.
constexpr float kMaxStepSeconds = 1.0f / 30.0f;

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
        "  --help                Show this message\n",
        EMERALD_VERSION, program);
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
    m_sky = std::make_unique<SkyGradient>(shaderDir);
    m_terrainShader =
        std::make_unique<Shader>(shaderDir / "terrain.vert", shaderDir / "terrain.frag");
    m_terrain = std::make_unique<TerrainMesh>(
        m_options.assetDir / "textures" / "ireland-heightmap.png", TerrainConfig{});

    m_camera.distance = 1100.0f;
    m_camera.elevation = 0.6f;
    m_camera.azimuth = -0.35f;
}

Application::~Application() = default;

int Application::run()
{
    while (!m_window.shouldClose()) {
        const float delta = m_timer.tick();

        m_window.pollEvents();
        update(delta);
        render();
        ++m_frameCount;

        const bool lastFrame = m_options.frameLimit > 0 && m_frameCount >= m_options.frameLimit;
        if (lastFrame && !m_options.screenshotPath.empty()) {
            // Read the back buffer before it is presented, so the capture matches this frame.
            if (!saveFramebufferPng(m_window.framebufferWidth(), m_window.framebufferHeight(),
                                    m_options.screenshotPath)) {
                return EXIT_FAILURE;
            }
        }

        m_window.swapBuffers();

        if (m_timer.hasNewSample()) {
            updateTitle();
        }
        if (lastFrame) {
            m_window.requestClose();
        }
    }

    std::printf("Rendered %d frames, last average %.1f FPS (%.2f ms/frame)\n",
                m_frameCount, m_timer.framesPerSecond(), m_timer.frameTimeMs());
    return EXIT_SUCCESS;
}

void Application::update(float deltaSeconds)
{
    m_camera.orbit(kOrbitSpeed * std::min(deltaSeconds, kMaxStepSeconds));
}

void Application::render()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::vec2 resolution(static_cast<float>(m_window.framebufferWidth()),
                               static_cast<float>(m_window.framebufferHeight()));

    // Sky is a background pass: no depth test or write.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_sky->draw(static_cast<float>(m_timer.elapsedSeconds()), resolution);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    m_terrainShader->use();
    m_terrainShader->set("uView", m_camera.view());
    m_terrainShader->set("uProjection", m_camera.projection(m_window.aspectRatio()));
    m_terrainShader->set("uCameraPos", m_camera.position());
    m_terrainShader->set("uSunDir", kSunDir);
    m_terrainShader->set("uSunColour", glm::vec3(1.0f, 0.95f, 0.85f));
    m_terrainShader->set("uAmbient", glm::vec3(0.30f, 0.33f, 0.38f));
    m_terrainShader->set("uFogStart", 1000.0f);
    m_terrainShader->set("uFogEnd", 2300.0f);
    m_terrainShader->set("uHeightScale", m_terrain->heightScale());
    m_terrainShader->set("uEdgeFade", glm::vec2(0.45f, 0.95f) * m_terrain->width());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_terrain->draw();
    glDisable(GL_BLEND);
}

void Application::updateTitle()
{
    char title[128];
    std::snprintf(title, sizeof(title), "%s  |  %.0f FPS  |  %.2f ms",
                  m_options.window.title.c_str(),
                  m_timer.framesPerSecond(), m_timer.frameTimeMs());
    m_window.setTitle(title);
}

} // namespace emerald
