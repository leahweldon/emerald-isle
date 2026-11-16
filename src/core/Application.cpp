#include "core/Application.hpp"

#include "core/Screenshot.hpp"
#include "render/Shader.hpp"
#include "render/SeaPlane.hpp"
#include "render/Sky.hpp"
#include "render/TerrainMesh.hpp"
#include "render/TextureArray.hpp"
#include "render/ViewPresets.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace emerald {

namespace {

constexpr float kOrbitSpeed = 0.05f; // radians per second

// Metres. The sea plane reaches the far plane (corners at ~0.99x far) so it meets the horizon.
constexpr float kFarPlane = 15000e3f;
constexpr float kSeaHalfSize = 0.7f * kFarPlane;
// Material layers, in the order terrain.frag indexes them (kGrass, kSand, kHeath, kRock).
constexpr const char* kMaterials[] = {"Grass004", "Ground054", "Ground037", "Rock058"};
constexpr GLuint kColourUnit = 0;
constexpr GLuint kNormalUnit = 1;
constexpr GLuint kCoastUnit = 2;
constexpr GLuint kHeightUnit = 3;

std::vector<std::filesystem::path> materialFiles(const std::filesystem::path& dir, const char* map)
{
    std::vector<std::filesystem::path> files;
    for (const char* name : kMaterials) {
        files.push_back(dir / (std::string(name) + "_1K-JPG_" + map + ".jpg"));
    }
    return files;
}

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
        "  --terrain-step <n>    Use every nth heightmap pixel (default 2)\n"
        "  --view <name>         Fixed camera preset: %s\n"
        "  --help                Show this message\n",
        EMERALD_VERSION, program, viewPresetNames().c_str());
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
        } else if (std::strcmp(arg, "--terrain-step") == 0 && hasValue) {
            options.terrainStep = parsePositive(arg, argv[++i]);
        } else if (std::strcmp(arg, "--view") == 0 && hasValue) {
            options.view = argv[++i];
            if (!findViewPreset(options.view)) {
                throw std::invalid_argument("Unknown view '" + options.view +
                                            "' (available: " + viewPresetNames() + ")");
            }
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

    TerrainConfig terrainConfig;
    terrainConfig.sampleStep = m_options.terrainStep;
    m_terrain = std::make_unique<TerrainMesh>(
        m_options.assetDir / "textures" / "ireland-heightmap.png", terrainConfig);
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
    m_terrainShader->set("uHeightTexture", static_cast<int>(kHeightUnit));
    m_terrainShader->set("uHeightTextureSize", m_terrain->heightTextureSize());
    m_terrainShader->set("uTexelSpacing", m_terrain->metresPerPixel());
    m_seaShader->use();
    m_seaShader->set("uCoastMask", static_cast<int>(kCoastUnit));
    m_seaShader->set("uTerrainSize", glm::vec2(m_terrain->width(), m_terrain->depth()));
    m_seaShader->set("uSeaRadius", kSeaHalfSize);

    // Default orbit starts from the overview preset.
    const std::string view = m_options.view.empty() ? "overview" : m_options.view;
    applyViewPreset(*findViewPreset(view), m_camera);
    m_camera.farPlane = kFarPlane;
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
    // Presets stay fixed so screenshots are reproducible.
    if (m_options.view.empty()) {
        m_camera.orbit(kOrbitSpeed * std::min(deltaSeconds, kMaxStepSeconds));
    }
}

void Application::render()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = m_camera.projection(m_window.aspectRatio());
    const glm::mat4 rotationOnly(glm::mat3(m_camera.view()));

    // Sky is a background pass: no depth test or write.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_sky->draw(glm::inverse(projection * rotationOnly), m_atmosphere);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    m_terrainShader->use();
    setSceneUniforms(*m_terrainShader);
    m_terrainShader->set("uExaggeration", m_terrain->verticalExaggeration());
    m_materialColour->bind(kColourUnit);
    m_materialNormal->bind(kNormalUnit);
    m_terrain->bindHeightTexture(kHeightUnit);
    m_terrain->draw();

    // Sea after terrain so it is hidden behind land, pushed back slightly so land wins at the coast.
    m_seaShader->use();
    setSceneUniforms(*m_seaShader);
    m_terrain->bindCoastMask(kCoastUnit);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    m_sea->draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Application::setSceneUniforms(Shader& shader)
{
    shader.set("uView", m_camera.view());
    shader.set("uProjection", m_camera.projection(m_window.aspectRatio()));
    shader.set("uCameraPos", m_camera.position());
    m_atmosphere.apply(shader, m_camera.position().y);
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
