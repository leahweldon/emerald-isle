# Tools

## make_heightmap.py

Builds `assets/textures/ireland-heightmap.png` from [Copernicus DEM GLO-90](https://spacedata.copernicus.eu/collections/copernicus-digital-elevation-model).

1. Downloads the 1° × 1° tiles covering lon −10.7 to −5.4, lat 51.3 to 55.5 from the public [AWS Open Data bucket](https://registry.opendata.aws/copernicus-dem/) (about 50 MB, cached in `tools/.cache/`). Open-sea tiles do not exist and are skipped.
2. Mosaics them and reprojects to Irish Transverse Mercator (EPSG:2157) with average resampling.
3. Sets sea, no-data and negative values to 0.
4. Writes a 16-bit greyscale PNG, about 2048 px on the long side.

### Rerunning

Requires Python 3.10+. From the repository root:

```bash
python3 -m venv tools/.venv
tools/.venv/bin/pip install -r tools/requirements.txt
tools/.venv/bin/python tools/make_heightmap.py
```

On Windows use `tools\.venv\Scripts\python`. Options:

| Option | Default | Description |
|---|---|---|
| `--out <file>` | `assets/textures/ireland-heightmap.png` | Output PNG |
| `--cache <dir>` | `tools/.cache/copernicus-dem` | Tile download cache |
| `--long-side <px>` | `2048` | Approximate size of the longer axis |

### Output encoding

| Property | Value |
|---|---|
| Pixel value | elevation in metres ÷ 0.02 (0 = sea) |
| Pixel size | whole metres, printed by the script (230 m at the default size) |
| Row 0 | north |

The renderer reads these as `kHeightmapMetresPerUnit` and `kHeightmapMetresPerPixel` in `src/world/HeightField.hpp`. If `--long-side` changes the pixel size, update `kHeightmapMetresPerPixel` to match.

Average resampling smooths peaks: the highest pixel at 230 m is about 945 m, against Carrauntoohil's 1039 m.

## make_water_normals.py

Builds the two tileable water normal maps in `assets/textures/water/` from a random wave spectrum, so no texture is downloaded. Each map is the inverse FFT of a directional, Phillips-like spectrum on an integer frequency grid (so it tiles exactly), with slopes taken from the spectrum's derivatives and scaled to a target RMS slope.

| Map | Peak wavelength | Character |
|---|---|---|
| `water-normal-1.png` | 1/6 of the tile | longer ripples aligned across the wind (texture +X) |
| `water-normal-2.png` | 1/18 of the tile | short, choppier ripples, less aligned |

### Rerunning

Uses only numpy, from the same environment as `make_heightmap.py`. From the repository root:

```bash
tools/.venv/bin/python tools/make_water_normals.py
```

| Option | Default | Description |
|---|---|---|
| `--out-dir <dir>` | `assets/textures/water` | Output directory |
| `--size <px>` | `512` | Width and height of each map |

The seeds and spectrum shapes are fixed in the script, so the output is the same on every run. Normals are OpenGL convention (+Y in green).

## Recording the README flight

`--record <dir>` saves every frame as `frame_00000.png`, `frame_00001.png`, … at a fixed timestep of 1/30 s. Before saving each frame it waits until every tile the view wants has streamed in (at most 300 frames), so the clip has no half-loaded terrain however fast the camera moves; the waves, rain and camera all advance by exactly one step per saved frame. It needs `--frames` or `--autofly` to end, and works with `--headless`, `--orbit` and any `--view`.

`docs/images/flight.gif` is a 9 s golden-hour flight west over Kerry, recorded at 1920 × 1080 and turned into a looping 960 px GIF with [ffmpeg](https://ffmpeg.org/) and [gifski](https://gif.ski/) (`brew install ffmpeg gifski`). From the repository root:

```bash
./build/emerald-isle --headless --width 1920 --height 1080 --view kerry --time 21:00 --autofly 9 --record /tmp/flight
mkdir -p /tmp/flight-960
ffmpeg -framerate 30 -i /tmp/flight/frame_%05d.png -vf "fps=20,scale=960:-1:flags=lanczos" /tmp/flight-960/f_%04d.png
gifski --fps 20 --quality 70 --width 960 -o docs/images/flight.gif /tmp/flight-960/f_*.png
```

That takes about a minute to record on an M5 and gives 181 frames and 5.9 MB; quality 80 is sharper but about 8.7 MB. A rain clip works the same way, for example `--view shore --time 13:30 --rain 1 --orbit --frames 240`.
