#include "core/Application.hpp"
#include "platform/Gl.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <string>
#include <vector>

namespace {

emerald::Application* g_app = nullptr;

} // namespace

// For the input test: the app's state as JSON (valid until the next call).
extern "C" EMSCRIPTEN_KEEPALIVE const char* emerald_state()
{
    static std::string state;
    state = g_app ? g_app->stateJson() : "{}";
    return state.c_str();
}

// Called by the page's panel button.
extern "C" EMSCRIPTEN_KEEPALIVE void emerald_toggle_panel()
{
    if (g_app) {
        g_app->toggleUi();
    }
}
#endif

int main(int argc, char** argv)
{
    try {
#ifdef __EMSCRIPTEN__
        // With no options in the page's URL: golden hour over Dingle Bay, slowly orbiting until the
        // visitor takes over, with the time of day paused.
        std::vector<std::string> defaults = {argc > 0 ? argv[0] : "emerald-isle", "--view", "glenbeigh",
                                             "--time", "21:30", "--orbit", "--paused"};
        std::vector<char*> defaultArgv;
        for (std::string& arg : defaults) {
            defaultArgv.push_back(arg.data());
        }
        if (argc <= 1) {
            argc = static_cast<int>(defaultArgv.size());
            argv = defaultArgv.data();
        }
#endif
        emerald::AppOptions options;
        if (!emerald::parseOptions(argc, argv, options)) {
            return EXIT_SUCCESS;
        }
        if (options.webglFallbacks) {
            emerald::gl::requestFallbacks();
        }
#ifdef __EMSCRIPTEN__
        options.window.clickToCapture = true;
#endif

#ifdef __EMSCRIPTEN__
        // The browser drives the frames; the app lives for the life of the page.
        g_app = new emerald::Application(std::move(options));
        emscripten_set_main_loop(
            [] {
                g_app->frame();
                // A run that ends by itself (--frames, --autofly) prints its statistics and stops.
                if (g_app->done()) {
                    g_app->finish();
                    emscripten_cancel_main_loop();
                    emscripten_run_script("if (window.emeraldFinished) emeraldFinished();");
                    return;
                }
                // The page hides its loading screen once the first frame is on screen.
                static bool first = true;
                if (first) {
                    first = false;
                    emscripten_run_script("if (window.emeraldReady) emeraldReady();");
                }
            },
            0, true);
        return EXIT_SUCCESS;
#else
        emerald::Application app(std::move(options));
        return app.run();
#endif
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Fatal error: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
