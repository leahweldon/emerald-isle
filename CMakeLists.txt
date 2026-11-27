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
# Off on the web: threads there need cross-origin isolation headers that static hosts cannot send.
option(EMERALD_THREADS "Generate terrain tiles on a worker thread" ON)
if(EMSCRIPTEN)
    set(EMERALD_THREADS OFF CACHE BOOL "" FORCE)
endif()

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

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE)

if(EMSCRIPTEN)
    # The browser build gets GLFW from Emscripten's contrib port (emscripten-glfw), which handles
    # HiDPI canvases, resizing and pointer lock; GL comes from WebGL 2, so there is no loader.
    set(EMERALD_WEB_GLFW "--use-port=contrib.glfw3")
    FetchContent_MakeAvailable(glm imgui)
else()
    FetchContent_MakeAvailable(glfw glm imgui)
endif()

FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# Dear ImGui with the GLFW and OpenGL 3 backends (its own GL loader), for the in-app panel
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(EMSCRIPTEN)
    target_compile_options(imgui PRIVATE ${EMERALD_WEB_GLFW})
else()
    # GLFW's headers only: the executable links GLFW itself.
    target_include_directories(imgui SYSTEM PRIVATE $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

if(NOT EMSCRIPTEN)
    # glad: OpenGL 3.3 core loader, generated with glad2 and kept in-tree
    add_library(glad STATIC external/glad/src/gl.c)
    target_include_directories(glad SYSTEM PUBLIC external/glad/include)

    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
endif()

# Compiler warnings for the project's own targets.
function(emerald_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(EMERALD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
        if(EMERALD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Core: logic with no GL or window (world generation, tile cache and CDLOD selection, camera
# control, sun position, options), shared by the app and the unit tests
# ---------------------------------------------------------------------------
add_library(emerald-core STATIC
    src/core/CameraController.cpp
    src/core/Input.cpp
    src/core/Options.cpp
    src/render/Camera.cpp
    src/render/ViewPresets.cpp
    src/world/Cdlod.cpp
    src/world/HeightField.cpp
    src/world/Noise.cpp
    src/world/SolarPosition.cpp
    src/world/TileCache.cpp
    src/world/TileStreamer.cpp)
target_include_directories(emerald-core PUBLIC src)
target_link_libraries(emerald-core PUBLIC glm::glm PRIVATE stb)
target_compile_definitions(emerald-core PUBLIC
    GLFW_INCLUDE_NONE
    EMERALD_THREADS=$<BOOL:${EMERALD_THREADS}>
    PRIVATE EMERALD_VERSION="${PROJECT_VERSION}")
if(EMSCRIPTEN)
    target_compile_options(emerald-core PRIVATE ${EMERALD_WEB_GLFW} -fwasm-exceptions)
else()
    # GLFW's headers only, for key codes.
    target_include_directories(emerald-core SYSTEM PRIVATE $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(emerald-core PUBLIC Threads::Threads)
endif()
emerald_warnings(emerald-core)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(emerald-isle
    src/main.cpp
    src/platform/Gl.cpp
    src/core/Application.cpp
    src/core/Panel.cpp
    src/core/Window.cpp
    src/core/FrameTimer.cpp
    src/core/Screenshot.cpp
    src/core/TimeOfDay.cpp
    src/render/Atmosphere.cpp
    src/render/GpuTimer.cpp
    src/render/GpuPassTimer.cpp
    src/render/OffscreenTarget.cpp
    src/render/Rain.cpp
    src/render/ReflectionTarget.cpp
    src/render/SeaSurface.cpp
    src/render/SeabedMap.cpp
    src/render/Shader.cpp
    src/render/ShadowMaps.cpp
    src/render/Sky.cpp
    src/render/Terrain.cpp
    src/render/TextureArray.cpp
    src/ui/Ui.cpp)

target_include_directories(emerald-isle PRIVATE src)
if(EMSCRIPTEN)
    target_link_libraries(emerald-isle PRIVATE emerald-core glm::glm stb imgui)
else()
    target_link_libraries(emerald-isle PRIVATE emerald-core glfw glad glm::glm stb imgui OpenGL::GL Threads::Threads)
endif()
target_compile_definitions(emerald-isle PRIVATE
    GLFW_INCLUDE_NONE
    EMERALD_THREADS=$<BOOL:${EMERALD_THREADS}>
    EMERALD_VERSION="${PROJECT_VERSION}")
emerald_warnings(emerald-isle)

if(EMSCRIPTEN)
    # The page (web/shell.html) plus index.js, index.wasm and index.data: the assets, prepared for
    # download by tools/make_web_assets.py (materials re-encoded, the panel font added).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(EMERALD_WEB_ASSETS "${CMAKE_BINARY_DIR}/web-assets")
    add_custom_target(emerald-web-assets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/make_web_assets.py"
                "${CMAKE_SOURCE_DIR}/assets" "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf"
                "${EMERALD_WEB_ASSETS}"
        COMMENT "Preparing web assets"
        VERBATIM)
    add_dependencies(emerald-isle emerald-web-assets)
    set_target_properties(emerald-isle PROPERTIES OUTPUT_NAME index SUFFIX ".html"
                          LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")
    target_compile_options(emerald-isle PRIVATE ${EMERALD_WEB_GLFW} -fwasm-exceptions)
    target_link_options(emerald-isle PRIVATE
        ${EMERALD_WEB_GLFW}
        -fwasm-exceptions
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=128MB
        -sSTACK_SIZE=1MB
        -sEXPORTED_RUNTIME_METHODS=ccall
        -sEXPORTED_FUNCTIONS=_main,_emerald_toggle_panel,_emerald_state,_emerald_touch_drag,_emerald_touch_pinch,_emerald_touch_pan,_emerald_next_view,_emerald_cycle_time,_emerald_cycle_rain
        "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
        "--preload-file=${EMERALD_WEB_ASSETS}@/assets")
else()
# Copy runtime assets next to the executable on every build, so shader-only edits are picked up
add_custom_target(emerald-assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:emerald-isle>/assets"
    # The panel's font ships with Dear ImGui (Roboto, Apache 2.0).
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf"
            "$<TARGET_FILE_DIR:emerald-isle>/assets/fonts/Roboto-Medium.ttf"
    COMMENT "Copying assets"
    VERBATIM)
add_dependencies(emerald-isle emerald-assets)
endif()
