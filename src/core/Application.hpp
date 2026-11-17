#pragma once

#include "core/CameraController.hpp"
#include "core/FrameTimer.hpp"
#include "core/Window.hpp"
#include "render/Atmosphere.hpp"
#include "render/Camera.hpp"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>

namespace emerald {

class Sky;
class Terrain;
class HeightField;
class SeaPlane;
class Shader;
class TextureArray;

struct AppOptions {
    WindowConfig window;
    // When greater than zero, the app renders this many frames and then exits.
    int frameLimit = 0;
    // When set, the final frame is saved to this PNG path before exiting.
    std::string screenshotPath;
    // Fixed camera preset name; empty means the default slow orbit.
    std::string view;
    // Sun position in degrees (compass bearing, elevation); defaults to the Atmosphere default.
    float sunBearing = 225.0f;
    float sunElevation = 22.0f;
    // Procedural world seed; the same seed always gives the same islands.
    std::uint32_t seed = 1;
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
    std::unique_ptr<Sky> m_sky;
    std::unique_ptr<HeightField> m_heightField;
    std::unique_ptr<Terrain> m_terrain;
    std::unique_ptr<Shader> m_terrainShader;
    std::unique_ptr<TextureArray> m_materialColour;
    std::unique_ptr<TextureArray> m_materialNormal;
    std::unique_ptr<SeaPlane> m_sea;
    std::unique_ptr<Shader> m_seaShader;
    Camera m_camera;
    std::unique_ptr<CameraController> m_controller;
    Atmosphere m_atmosphere;
    int m_frameCount = 0;
    int m_settledFrames = 0; // frames rendered with all wanted terrain tiles loaded
};

} // namespace emerald
