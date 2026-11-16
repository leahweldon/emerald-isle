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

The renderer reads these as `kHeightmapMetresPerUnit` and `kHeightmapMetresPerPixel` in `src/render/TerrainMesh.hpp`. If `--long-side` changes the pixel size, update `kHeightmapMetresPerPixel` to match.

Average resampling smooths peaks: the highest pixel at 230 m is about 945 m, against Carrauntoohil's 1039 m.
