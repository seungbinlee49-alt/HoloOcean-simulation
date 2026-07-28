"""Range-gain (TVG-style) normalization for raw SSS captures.

Real side-scan sonar always applies time-varying gain (TVG) before display, to
compensate for the geometric/range-dependent falloff in raw backscatter
intensity. Without this, near-range returns are always much brighter than
far-range returns regardless of what is actually on the seabed, which masks
real material contrast.

This script:
  1. Loads a raw SSS .npy array (rows = along-track pings, cols = range bins,
     left/right channels concatenated).
  2. Estimates the expected range-dependent falloff per column (median across
     all rows, which averages out point targets and keeps the smooth range
     trend).
  3. Divides it out, so a flat/uniform seabed would read as a constant value
     regardless of range, and only real reflectivity differences (material,
     objects) remain visible.
  4. Saves both a normalized-intensity PNG and the normalized array.

Does not modify the raw .npy in place; writes new files alongside it.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def tvg_normalize(arr: np.ndarray, smooth_window: int = 9, rows_per_pass: int = 0) -> np.ndarray:
    """If rows_per_pass > 0, computes and applies a SEPARATE range-gain profile per pass,
    instead of one profile for the whole multi-pass capture. A single global profile only
    corrects the average range trend; it does not correct pass-to-pass gain differences
    (e.g. from slightly different AUV altitude per y-track), which is exactly what produces
    the sharp brightness jumps at pass boundaries -- a non-physical artifact with no
    equivalent in a real single continuous survey."""
    if rows_per_pass <= 0 or rows_per_pass >= arr.shape[0]:
        segments = [(0, arr.shape[0])]
    else:
        segments = [(s, min(s + rows_per_pass, arr.shape[0])) for s in range(0, arr.shape[0], rows_per_pass)]

    normalized = np.empty_like(arr, dtype=np.float64)
    for start, end in segments:
        seg = arr[start:end]
        col_profile = np.median(seg, axis=0)
        if smooth_window > 1:
            kernel = np.ones(smooth_window) / smooth_window
            col_profile = np.convolve(col_profile, kernel, mode="same")
        col_profile = np.maximum(col_profile, 1e-9)
        normalized[start:end] = seg / col_profile[np.newaxis, :]
    return normalized


def to_display_png(
    arr: np.ndarray,
    path: Path,
    p_low: float = 1.0,
    p_high: float = 99.5,
    log_scale: bool = True,
) -> None:
    # Real sonar displays are shown in dB (log) because raw backscatter dynamic range between
    # weak (sand/mud) and strong (gravel, rock, metal) returns commonly spans 40-60+ dB. A
    # linear stretch crushes everything except the brightest few percent to black -- which is
    # exactly what a linear percentile stretch of this array does.
    if log_scale:
        floor = np.percentile(arr[arr > 0], 0.5) if np.any(arr > 0) else 1e-9
        display_arr = 10.0 * np.log10(np.maximum(arr, floor * 1e-3) + floor * 1e-3)
    else:
        display_arr = arr
    lo = np.percentile(display_arr, p_low)
    hi = np.percentile(display_arr, p_high)
    hi = max(hi, lo + 1e-9)
    scaled = np.clip((display_arr - lo) / (hi - lo), 0.0, 1.0)
    img = (scaled * 255).astype(np.uint8)
    Image.fromarray(img, mode="L").save(path)


def add_speckle_noise(arr: np.ndarray, mult_sigma: float, add_sigma: float, seed: int = 0) -> np.ndarray:
    rng = np.random.default_rng(seed)
    mult_noise = 1.0 + rng.normal(0.0, mult_sigma, size=arr.shape) if mult_sigma > 0 else 1.0
    add_noise = rng.normal(0.0, add_sigma, size=arr.shape) if add_sigma > 0 else 0.0
    return np.maximum(arr * mult_noise + add_noise, 0.0)


def add_anisotropic_speckle_noise(
    arr: np.ndarray,
    target_cv: float = 0.41,
    range_corr_px: float = 74.0,
    along_track_corr_px: float = 2.0,
    seed: int = 0,
) -> np.ndarray:
    """Correlated multiplicative speckle, calibrated against a real reference image
    (measured: contrast/mean ~0.41, ~74px correlation along range/columns, ~2px along
    along-track/rows -- i.e. a streaky, range-direction-elongated grain, not isotropic
    per-pixel static)."""
    rng = np.random.default_rng(seed)
    white = rng.normal(0.0, 1.0, size=arr.shape)

    def gaussian_kernel_1d(sigma_px: float) -> np.ndarray:
        if sigma_px < 0.5:
            return np.array([1.0])
        radius = max(1, int(round(sigma_px * 2)))
        x = np.arange(-radius, radius + 1)
        k = np.exp(-(x ** 2) / (2 * sigma_px ** 2))
        return k / k.sum()

    range_kernel = gaussian_kernel_1d(range_corr_px / 2.5)
    along_kernel = gaussian_kernel_1d(along_track_corr_px / 2.5)

    smoothed = np.apply_along_axis(lambda m: np.convolve(m, range_kernel, mode="same"), axis=1, arr=white)
    smoothed = np.apply_along_axis(lambda m: np.convolve(m, along_kernel, mode="same"), axis=0, arr=smoothed)

    current_std = smoothed.std()
    if current_std > 1e-9:
        smoothed = smoothed / current_std
    mult_noise = 1.0 + smoothed * target_cv
    return np.maximum(arr * mult_noise, 0.0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply TVG normalization (+ optional speckle) to a raw SSS npy.")
    parser.add_argument("--input-npy", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--smooth-window", type=int, default=9)
    parser.add_argument("--rows-per-pass", type=int, default=0, help="If set, normalize each pass's range-gain independently instead of one global profile.")
    parser.add_argument("--speckle-mult-sigma", type=float, default=0.0)
    parser.add_argument("--speckle-add-sigma", type=float, default=0.0)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    arr = np.load(args.input_npy)

    normalized = tvg_normalize(arr, args.smooth_window, args.rows_per_pass)
    np.save(args.output_dir / "tvg_normalized_raw.npy", normalized)
    to_display_png(normalized, args.output_dir / "tvg_normalized_display.png")

    if args.speckle_mult_sigma > 0 or args.speckle_add_sigma > 0:
        speckled = add_speckle_noise(normalized, args.speckle_mult_sigma, args.speckle_add_sigma)
        np.save(args.output_dir / "tvg_normalized_speckled_raw.npy", speckled)
        to_display_png(speckled, args.output_dir / "tvg_normalized_speckled_display.png")

    print(f"saved to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
