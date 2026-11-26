#include "core/Application.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

int main(int argc, char** argv)
{
    try {
        emerald::AppOptions options;
        if (!emerald::parseOptions(argc, argv, options)) {
            return EXIT_SUCCESS;
        }

#ifdef __EMSCRIPTEN__
        // The browser drives the frames; the app lives for the life of the page.
        auto* app = new emerald::Application(std::move(options));
        emscripten_set_main_loop_arg([](void* a) { static_cast<emerald::Application*>(a)->frame(); }, app, 0, true);
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
