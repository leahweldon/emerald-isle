"""Prepare the assets packaged into the web build.

Copies assets/ to the output directory, re-encodes the material JPEGs at a quality that suits a
download (they are stored near-lossless, about 2 MB each), and adds the panel's font. Files are
only rewritten when their source is newer, so rebuilds are quick.

Usage: python3 tools/make_web_assets.py <assets dir> <font file> <output dir>
"""

import shutil
import sys
from pathlib import Path

from PIL import Image

JPEG_QUALITY = 85  # visually the same on terrain at the web build's resolutions


def up_to_date(source: Path, target: Path) -> bool:
    return target.exists() and target.stat().st_mtime >= source.stat().st_mtime


def main() -> None:
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    assets, font, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    total = 0
    for source in sorted(p for p in assets.rglob("*") if p.is_file() and not p.name.startswith(".")):
        target = out / source.relative_to(assets)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not up_to_date(source, target):
            if source.suffix.lower() == ".jpg" and "materials" in source.parts:
                Image.open(source).save(target, "JPEG", quality=JPEG_QUALITY, optimize=True, progressive=True)
            else:
                shutil.copy2(source, target)
        total += target.stat().st_size
    font_target = out / "fonts" / font.name
    font_target.parent.mkdir(parents=True, exist_ok=True)
    if not up_to_date(font, font_target):
        shutil.copy2(font, font_target)
    total += font_target.stat().st_size
    print(f"Web assets: {total / 1e6:.1f} MB in {out}")


if __name__ == "__main__":
    main()
