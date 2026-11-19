"""Generate tileable water normal maps from a random wave spectrum.

Each map is the inverse FFT of a directional, Phillips-like spectrum on an integer frequency
grid, so it tiles exactly. Slopes come from the spectrum's analytic derivatives, are scaled to a
target RMS slope, and are written as OpenGL-convention normal maps (+Y up in green).

Usage: python3 tools/make_water_normals.py [--out-dir assets/textures/water] [--size 512]
"""

import argparse
import struct
import zlib
from pathlib import Path

import numpy as np

# name, seed, peak wavelength (fraction of the tile), directional spread exponent, RMS slope
MAPS = [
    ("water-normal-1.png", 1, 1.0 / 6.0, 4.0, 0.16),   # longer, wind-aligned ripples
    ("water-normal-2.png", 2, 1.0 / 18.0, 1.0, 0.20),  # short, choppier ripples, less aligned
]


def write_png(path: Path, rgb: np.ndarray) -> None:
    """Minimal 8-bit RGB PNG writer, so the script needs only numpy."""
    height, width, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(height))

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9))
    path.write_bytes(png + chunk(b"IEND", b""))


def make_normal_map(size: int, seed: int, peak: float, spread: float, rms_slope: float) -> np.ndarray:
    rng = np.random.default_rng(seed)
    # Integer frequencies (cycles per tile) along x (wind direction) and y.
    freq = np.fft.fftfreq(size, d=1.0 / size)
    kx, ky = np.meshgrid(freq, freq)
    k = np.hypot(kx, ky)
    k[0, 0] = 1.0

    # Phillips-like: rises to a peak at 1/peak cycles per tile, falls as k^-4 above it, and favours
    # the wind direction; the highest frequencies are cut to avoid aliasing in the texture.
    k_peak = 1.0 / peak
    spectrum = np.exp(-((k_peak / k) ** 2)) / k**4
    spectrum *= np.abs(kx / k) ** spread
    spectrum *= np.exp(-((k / (0.35 * size)) ** 2))
    spectrum[0, 0] = 0.0

    noise = rng.normal(size=(size, size)) + 1j * rng.normal(size=(size, size))
    heights = noise * np.sqrt(spectrum)
    slope_x = np.real(np.fft.ifft2(1j * kx * heights))
    slope_y = np.real(np.fft.ifft2(1j * ky * heights))
    scale = rms_slope / np.sqrt(np.mean(slope_x**2 + slope_y**2))
    slope_x *= scale
    slope_y *= scale

    normal = np.stack([-slope_x, -slope_y, np.ones_like(slope_x)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return np.clip(np.round((normal * 0.5 + 0.5) * 255.0), 0, 255).astype(np.uint8)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out-dir", type=Path, default=Path("assets/textures/water"))
    parser.add_argument("--size", type=int, default=512)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name, seed, peak, spread, rms_slope in MAPS:
        rgb = make_normal_map(args.size, seed, peak, spread, rms_slope)
        write_png(args.out_dir / name, rgb)
        print(f"Wrote {args.out_dir / name} ({args.size} x {args.size})")


if __name__ == "__main__":
    main()
