#include "core/Application.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

int main(int argc, char** argv)
{
    try {
        emerald::AppOptions options;
        if (!emerald::parseOptions(argc, argv, options)) {
            return EXIT_SUCCESS;
        }

        emerald::Application app(std::move(options));
        return app.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Fatal error: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
