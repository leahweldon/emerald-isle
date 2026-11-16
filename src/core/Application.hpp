#pragma once

#include "core/FrameTimer.hpp"
#include "core/Window.hpp"
#include "render/Atmosphere.hpp"
#include "render/Camera.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace emerald {

class SkyGradient;
class TerrainMesh;
class SeaPlane;
class Shader;

struct AppOptions {
    WindowConfig window;
    // When greater than zero, the app renders this many frames and then exits.
    int frameLimit = 0;
    // When set, the final frame is saved to this PNG path before exiting.
    std::string screenshotPath;
    // Heightmap sampling step; higher is faster and coarser.
    int terrainStep = 2;
    // Fixed camera preset name; empty means the default slow orbit.
    std::string view;
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
    void update(float deltaSeconds);
    void render();
    void updateTitle();
    void setSceneUniforms(Shader& shader);

    AppOptions m_options;
    Window m_window;
    FrameTimer m_timer;
    std::unique_ptr<SkyGradient> m_sky;
    std::unique_ptr<TerrainMesh> m_terrain;
    std::unique_ptr<Shader> m_terrainShader;
    std::unique_ptr<SeaPlane> m_sea;
    std::unique_ptr<Shader> m_seaShader;
    Camera m_camera;
    Atmosphere m_atmosphere;
    int m_frameCount = 0;
};

} // namespace emerald
