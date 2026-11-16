#pragma once

#include "core/FrameTimer.hpp"
#include "core/Window.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace emerald {

class SkyGradient;

struct AppOptions {
    WindowConfig window;
    // When greater than zero, the app renders this many frames and then exits.
    int frameLimit = 0;
    // When set, the final frame is saved to this PNG path before exiting.
    std::string screenshotPath;
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

    AppOptions m_options;
    Window m_window;
    FrameTimer m_timer;
    std::unique_ptr<SkyGradient> m_sky;
    int m_frameCount = 0;
};

} // namespace emerald
