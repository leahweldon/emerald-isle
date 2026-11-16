# Emerald Isle

[![Build](https://github.com/leahweldon/emerald-isle/actions/workflows/build.yml/badge.svg)](https://github.com/leahweldon/emerald-isle/actions/workflows/build.yml)

A real-time terrain renderer of Ireland, written in C++17 and modern shader-based OpenGL.

> **Work in progress.** This is a ground-up rebuild of my Year 4 Computer Graphics final project at Trinity College Dublin, *Toward a Futuristic Emerald Isle*. The original used legacy fixed-function OpenGL; this version is built on a programmable pipeline, runs on macOS, Windows and Linux, and implements the features the original only approximated.

![MacGillycuddy's Reeks, Kerry, from the east, with blended materials, sky lighting and height fog (--view kerry)](docs/images/hero.png)

## Roadmap

- [x] **1. Foundations:** cross-platform build, OpenGL 3.3 core context, shader loading, FPS display, CI
- [x] **2. Terrain:** mesh generated from an Ireland heightmap, with per-vertex normals and directional lighting
- [x] **3. Surface materials:** grass, rock and sand blended by height and slope
- [x] **4. Sky and camera:** cubemap sky and a free-fly camera
- [ ] **5. Infinite world:** terrain chunks streamed around the camera, with level of detail
- [ ] **6. Shadows:** shadow mapping from a moving sun, with a day/night cycle
- [ ] **7. Water:** planar reflections and animated waves
- [ ] **8. Weather:** GPU-instanced rain
- [ ] **9. Tools:** in-app panel for toggling features and viewing frame timings
- [ ] **10. Web:** WebGL build with a live demo

## Screenshots

Each image is reproducible with `--view <name> --screenshot <file>`, plus the sun options where shown.

![Milestone 4: low sun over Dingle Bay from the Glenbeigh hills, with the atmospheric sky and sun glint (--view glenbeigh --sun-bearing 290 --sun-elevation 4)](docs/images/milestone-04.png)

![The whole island from the south-east (--view overview)](docs/images/milestone-03.png)

## Building

Requirements: a C++17 compiler, CMake 3.20+ and Git. All libraries are downloaded automatically when you configure.

```bash
git clone https://github.com/leahweldon/emerald-isle.git
cd emerald-isle
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Linux, install the X11 and OpenGL headers first:

```bash
sudo apt-get install libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev
```

## Running

```bash
./build/emerald-isle                 # macOS / Linux
build\Release\emerald-isle.exe       # Windows (Visual Studio generator)
```

| Option | Description |
|---|---|
| `--width <px>`, `--height <px>` | Window size (default 1280 × 720) |
| `--no-vsync` | Render as fast as possible, to measure frame rate |
| `--frames <n>` | Exit after rendering *n* frames |
| `--screenshot <file>` | Save the final frame as a PNG, then exit |
| `--terrain-step <n>` | Use every *n*th heightmap pixel (default 2); higher is faster |
| `--view <name>` | Start in free-fly at a preset pose instead of the orbit: `overview`, `kerry`, `wicklow`, `glenbeigh` |
| `--sun-bearing <deg>` | Sun compass bearing, 0 = north (default 225, south-west) |
| `--sun-elevation <deg>` | Sun elevation above the horizon (default 22) |

The window title shows the averaged frame rate and frame time.

### Controls

| Key | Action |
|---|---|
| **W** / **S** | Fly forward / back |
| **A** / **D** | Strafe left / right |
| **Q** / **E** | Down / up |
| **←** / **→**, **↑** / **↓** | Turn, pitch |
| Right mouse button + drag | Look around |
| **Tab** | Capture the cursor for mouse look (press again to release) |
| **Shift** | Move 5× faster |
| Scroll wheel | Change base speed (×1.25 per notch) |
| **C** | Switch between free-fly and orbit around the point in view |
| **P** | Print the camera pose as a `--view` preset line for `src/render/ViewPresets.cpp` |
| **Esc** | Quit |

Without `--view` the app starts in a slow orbit of the island; press **C** to take control. The camera stays at least 30 m above the ground and sea.

## Project structure

```text
src/
  core/     window and context, frame timing, application loop, screenshots
  render/   shaders, atmosphere, camera and view presets, terrain mesh, materials, sea
assets/
  shaders/  GLSL source
  textures/ Ireland heightmap (16-bit, built by tools/make_heightmap.py)
    materials/  CC0 colour and normal maps from ambientCG
tools/      data preparation scripts
external/
  glad/     OpenGL 3.3 core loader (generated with glad2)
```

## Data

The terrain is built from Copernicus DEM GLO-90, reprojected to Irish Transverse Mercator. See [tools/README.md](tools/README.md) to regenerate it.

Contains modified Copernicus DEM data, © DLR e.V. 2010–2014 and © Airbus Defence and Space GmbH 2014–2018, provided under COPERNICUS by the European Union and ESA.

## Built with

[GLFW](https://www.glfw.org/) · [glad](https://github.com/Dav1dde/glad) · [GLM](https://github.com/g-truc/glm) · [stb](https://github.com/nothings/stb)

## Credits

Terrain textures are CC0 from [ambientCG](https://ambientcg.com/); elevation data is from Copernicus DEM. Every asset, with its source URL and licence, is listed in [CREDITS.md](CREDITS.md).
