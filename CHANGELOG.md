# Changelog

## [1.0.0] - 2026-11-27

The first release: a ground-up rebuild of my CSU44052 Computer Graphics project at Trinity College Dublin, from legacy fixed-function OpenGL to a shader-based renderer on macOS, Windows, Linux and the web.

### Milestones

1. **Foundations:** C++17 and CMake with every dependency fetched at configure time, an OpenGL 3.3 core context, shader loading, frame timing and CI on Linux, macOS and Windows.
2. **Terrain:** a mesh from a Copernicus DEM heightmap of Ireland, reprojected to Irish Transverse Mercator, with per-vertex normals and directional light.
3. **Materials:** grass, rock, sand and heath blended by height and slope, with CC0 textures from ambientCG.
4. **Sky and camera:** an atmospheric sky (Rayleigh, Mie, ozone) and a free-fly camera whose speed follows the height above the ground.
5. **Infinite world:** CDLOD terrain streamed on a worker thread around a double-precision camera, with seeded procedural islands beyond Ireland.
6. **Shadows and day/night:** cascaded shadow maps, horizon maps for distant shadows, the sun from the NOAA equations in Irish time, a full moon and stars.
7. **Water:** Gerstner waves, drifting ripples, planar reflections, depth-tinted shallows, foam and the sun's and moon's glint.
8. **Weather:** GPU rain with transform feedback and instanced streaks, splashes, overcast skies, wet ground and raindrop rings on the sea.
9. **Tools:** an in-app Dear ImGui panel with GPU time per pass, time and weather controls, feature switches and debug views.
10. **Web:** the same source compiled with Emscripten to WebGL 2, deployed to GitHub Pages, with touch controls and a phone overlay.

### Also in this release

- HDR rendering with Khronos PBR Neutral or AgX tone mapping, and FXAA at Low quality.
- Low, Medium and High quality settings; Low on phones, with tiles requested nearest first.
- Unit tests (doctest) for the core logic, and rendering, streaming and browser tests in CI.
- `--record` to save every frame at a fixed timestep, used for the README's flight GIF.
- Release packages for macOS (universal), Windows and Linux, built when a release is published.

[1.0.0]: https://github.com/leahweldon/emerald-isle/releases/tag/v1.0.0
