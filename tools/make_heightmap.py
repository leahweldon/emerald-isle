"""Build assets/textures/ireland-heightmap.png from Copernicus DEM GLO-90.

Downloads the 1x1 degree tiles covering Ireland, mosaics them, reprojects to
Irish Transverse Mercator (EPSG:2157) and writes a 16-bit greyscale PNG.

Encoding: pixel value = elevation in metres / METRES_PER_UNIT, sea and no-data = 0.
These constants must match src/render/TerrainMesh.hpp.
"""

import argparse
import math
import os
import ssl
import sys
import urllib.error
import urllib.request
import warnings
from pathlib import Path

os.environ.setdefault("GDAL_PAM_ENABLED", "NO")  # no .aux.xml sidecar next to the PNG

import certifi
import numpy as np
import rasterio
from rasterio.enums import Resampling
from rasterio.errors import NotGeoreferencedWarning
from rasterio.merge import merge
from rasterio.transform import from_origin
from rasterio.warp import reproject, transform_bounds

TILE_URL = (
    "https://copernicus-dem-90m.s3.amazonaws.com/"
    "Copernicus_DSM_COG_30_{ns}{lat:02d}_00_{ew}{lon:03d}_00_DEM/"
    "Copernicus_DSM_COG_30_{ns}{lat:02d}_00_{ew}{lon:03d}_00_DEM.tif"
)
BOUNDS = (-10.7, 51.3, -5.4, 55.5)  # west, south, east, north (degrees)
DST_CRS = "EPSG:2157"
METRES_PER_UNIT = 0.02
NODATA = -32767.0

REPO = Path(__file__).resolve().parent.parent
# python.org macOS builds do not use the system trust store by default.
SSL_CONTEXT = ssl.create_default_context(cafile=certifi.where())


def tile_url(lat: int, lon: int) -> str:
    return TILE_URL.format(ns="N" if lat >= 0 else "S", lat=abs(lat),
                           ew="E" if lon >= 0 else "W", lon=abs(lon))


def fetch_tiles(cache: Path) -> list[Path]:
    """Download tiles intersecting BOUNDS. Open-ocean tiles do not exist and are skipped."""
    cache.mkdir(parents=True, exist_ok=True)
    west, south, east, north = BOUNDS
    paths = []
    for lat in range(math.floor(south), math.ceil(north)):
        for lon in range(math.floor(west), math.ceil(east)):
            url = tile_url(lat, lon)
            path = cache / url.rsplit("/", 1)[1]
            if not path.exists():
                try:
                    with urllib.request.urlopen(url, timeout=60, context=SSL_CONTEXT) as response:
                        data = response.read()
                except urllib.error.HTTPError as error:
                    if error.code in (403, 404):
                        print(f"  no tile at {lat},{lon} (open sea)")
                        continue
                    raise
                tmp = path.with_suffix(".part")
                tmp.write_bytes(data)
                tmp.rename(path)
                print(f"  downloaded {path.name} ({len(data) / 1e6:.1f} MB)")
            paths.append(path)
    if not paths:
        sys.exit("No DEM tiles found for the requested bounds")
    return paths


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path,
                        default=REPO / "assets" / "textures" / "ireland-heightmap.png")
    parser.add_argument("--cache", type=Path, default=REPO / "tools" / ".cache" / "copernicus-dem")
    parser.add_argument("--long-side", type=int, default=2048,
                        help="approximate output size in pixels along the longer axis")
    args = parser.parse_args()

    print("Fetching tiles")
    tiles = [rasterio.open(p) for p in fetch_tiles(args.cache)]
    try:
        mosaic, src_transform = merge(tiles, bounds=BOUNDS, nodata=NODATA, dtype="float32")
        src_crs = tiles[0].crs
    finally:
        for t in tiles:
            t.close()

    # Whole-metre pixel size so the renderer can use it as a constant.
    left, bottom, right, top = transform_bounds("EPSG:4326", DST_CRS, *BOUNDS)
    pixel = float(math.ceil(max(right - left, top - bottom) / args.long_side))
    width = math.ceil((right - left) / pixel)
    height = math.ceil((top - bottom) / pixel)
    dst_transform = from_origin(left, top, pixel, pixel)

    print(f"Reprojecting to {DST_CRS}: {width}x{height} px at {pixel:.0f} m")
    elevation = np.full((height, width), NODATA, dtype="float32")
    reproject(mosaic[0], elevation,
              src_transform=src_transform, src_crs=src_crs, src_nodata=NODATA,
              dst_transform=dst_transform, dst_crs=DST_CRS, dst_nodata=NODATA,
              resampling=Resampling.average)

    elevation[(elevation == NODATA) | ~np.isfinite(elevation) | (elevation < 0)] = 0.0
    max_units = np.iinfo(np.uint16).max
    if elevation.max() / METRES_PER_UNIT > max_units:
        sys.exit(f"Peak {elevation.max():.0f} m does not fit at {METRES_PER_UNIT} m per unit")
    encoded = np.round(elevation / METRES_PER_UNIT).astype(np.uint16)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    # The PNG is plain image data; georeferencing is dropped on purpose.
    warnings.simplefilter("ignore", NotGeoreferencedWarning)
    with rasterio.open(args.out, "w", driver="PNG", width=width, height=height,
                       count=1, dtype="uint16") as dst:
        dst.write(encoded, 1)

    land = elevation > 0
    print(f"Wrote {args.out}")
    print(f"  pixel size {pixel:.0f} m, {METRES_PER_UNIT} m per unit")
    print(f"  land {land.mean() * 100:.1f}%, max {elevation.max():.0f} m, "
          f"median land {np.median(elevation[land]):.0f} m")


if __name__ == "__main__":
    main()
