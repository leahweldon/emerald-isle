cmake_minimum_required(VERSION 3.20)

project(EmeraldIsle
    VERSION 0.1.0
    DESCRIPTION "A real-time terrain renderer of Ireland in modern OpenGL"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EMERALD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Dependencies (fetched at configure time, so no manual installs are needed)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f58f558c120e9b32c217290b80bad1a0729fbb2c
    GIT_SHALLOW    FALSE)

FetchContent_MakeAvailable(glfw glm)

FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# glad: OpenGL 3.3 core loader, generated with glad2 and kept in-tree
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC external/glad/include)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(emerald-isle
    src/main.cpp
    src/core/Application.cpp
    src/core/CameraController.cpp
    src/core/Window.cpp
    src/core/FrameTimer.cpp
    src/core/Input.cpp
    src/core/Screenshot.cpp
    src/core/TimeOfDay.cpp
    src/render/Atmosphere.cpp
    src/render/Camera.cpp
    src/render/GpuTimer.cpp
    src/render/OffscreenTarget.cpp
    src/render/SeaSurface.cpp
    src/render/SeabedMap.cpp
    src/render/Shader.cpp
    src/render/ShadowMaps.cpp
    src/render/Sky.cpp
    src/render/Terrain.cpp
    src/render/TextureArray.cpp
    src/render/ViewPresets.cpp
    src/world/HeightField.cpp
    src/world/Noise.cpp
    src/world/SolarPosition.cpp
    src/world/TileStreamer.cpp)

target_include_directories(emerald-isle PRIVATE src)
target_link_libraries(emerald-isle PRIVATE glfw glad glm::glm stb OpenGL::GL Threads::Threads)
target_compile_definitions(emerald-isle PRIVATE
    GLFW_INCLUDE_NONE
    EMERALD_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(emerald-isle PRIVATE /W4 /permissive-)
    if(EMERALD_WARNINGS_AS_ERRORS)
        target_compile_options(emerald-isle PRIVATE /WX)
    endif()
else()
    target_compile_options(emerald-isle PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(EMERALD_WARNINGS_AS_ERRORS)
        target_compile_options(emerald-isle PRIVATE -Werror)
    endif()
endif()

# Copy runtime assets next to the executable on every build, so shader-only edits are picked up
add_custom_target(emerald-assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:emerald-isle>/assets"
    COMMENT "Copying assets"
    VERBATIM)
add_dependencies(emerald-isle emerald-assets)
