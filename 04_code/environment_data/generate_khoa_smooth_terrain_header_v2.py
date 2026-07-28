from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


def read_heightfield(path: Path) -> tuple[list[float], list[float], list[list[float]]]:
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit(f"empty heightfield: {path}")

    xs = sorted({float(r["x_m"]) for r in rows})
    ys = sorted({float(r["y_m"]) for r in rows})
    x_index = {x: i for i, x in enumerate(xs)}
    y_index = {y: i for i, y in enumerate(ys)}
    grid: list[list[float | None]] = [[None for _ in xs] for _ in ys]
    for row in rows:
        x = float(row["x_m"])
        y = float(row["y_m"])
        grid[y_index[y]][x_index[x]] = float(row["depth_m"])

    if any(value is None for row in grid for value in row):
        raise SystemExit("heightfield grid has missing cells")

    return xs, ys, [[float(v) for v in row] for row in grid]  # type: ignore[arg-type]


def gaussian_kernel1d(sigma: float) -> list[float]:
    if sigma <= 0:
        return [1.0]
    radius = max(1, int(math.ceil(sigma * 3.0)))
    values = [math.exp(-(i * i) / (2.0 * sigma * sigma)) for i in range(-radius, radius + 1)]
    total = sum(values)
    return [v / total for v in values]


def convolve_horizontal(grid: list[list[float]], kernel: list[float]) -> list[list[float]]:
    radius = len(kernel) // 2
    h = len(grid)
    w = len(grid[0])
    out = [[0.0 for _ in range(w)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            value = 0.0
            for k, weight in enumerate(kernel):
                xx = min(w - 1, max(0, x + k - radius))
                value += grid[y][xx] * weight
            out[y][x] = value
    return out


def convolve_vertical(grid: list[list[float]], kernel: list[float]) -> list[list[float]]:
    radius = len(kernel) // 2
    h = len(grid)
    w = len(grid[0])
    out = [[0.0 for _ in range(w)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            value = 0.0
            for k, weight in enumerate(kernel):
                yy = min(h - 1, max(0, y + k - radius))
                value += grid[yy][x] * weight
            out[y][x] = value
    return out


def smooth_grid(grid: list[list[float]], sigma_cells: float, passes: int) -> list[list[float]]:
    kernel = gaussian_kernel1d(sigma_cells)
    out = [row[:] for row in grid]
    for _ in range(max(1, passes)):
        out = convolve_horizontal(out, kernel)
        out = convolve_vertical(out, kernel)
    return out


def bilinear_sample(xs: list[float], ys: list[float], grid: list[list[float]], x: float, y: float) -> float:
    w = len(xs)
    h = len(ys)
    if x <= xs[0]:
        ix = 0
        tx = 0.0
    elif x >= xs[-1]:
        ix = w - 2
        tx = 1.0
    else:
        ix = 0
        while ix < w - 2 and xs[ix + 1] < x:
            ix += 1
        tx = (x - xs[ix]) / (xs[ix + 1] - xs[ix])

    if y <= ys[0]:
        iy = 0
        ty = 0.0
    elif y >= ys[-1]:
        iy = h - 2
        ty = 1.0
    else:
        iy = 0
        while iy < h - 2 and ys[iy + 1] < y:
            iy += 1
        ty = (y - ys[iy]) / (ys[iy + 1] - ys[iy])

    z00 = grid[iy][ix]
    z10 = grid[iy][ix + 1]
    z01 = grid[iy + 1][ix]
    z11 = grid[iy + 1][ix + 1]
    z0 = z00 * (1.0 - tx) + z10 * tx
    z1 = z01 * (1.0 - tx) + z11 * tx
    return z0 * (1.0 - ty) + z1 * ty


def resample_grid(
    xs: list[float],
    ys: list[float],
    grid: list[list[float]],
    out_grid_x: int,
    out_grid_y: int,
) -> tuple[list[float], list[float], list[list[float]]]:
    out_xs = [xs[0] + (xs[-1] - xs[0]) * i / (out_grid_x - 1) for i in range(out_grid_x)]
    out_ys = [ys[0] + (ys[-1] - ys[0]) * i / (out_grid_y - 1) for i in range(out_grid_y)]
    out = [[bilinear_sample(xs, ys, grid, x, y) for x in out_xs] for y in out_ys]
    return out_xs, out_ys, out


def slope_limit_grid(
    xs: list[float],
    ys: list[float],
    grid: list[list[float]],
    max_slope: float,
    iterations: int,
) -> list[list[float]]:
    # max_slope is meters depth change per horizontal meter. It is deliberately
    # conservative for a broad low-gradient tidal-flat style macro terrain.
    out = [row[:] for row in grid]
    h = len(out)
    w = len(out[0])
    for _ in range(max(0, iterations)):
        changed = False
        for y in range(h):
            for x in range(w):
                values = []
                for dy, dx in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                    yy = y + dy
                    xx = x + dx
                    if yy < 0 or yy >= h or xx < 0 or xx >= w:
                        continue
                    dist = abs(xs[x] - xs[xx]) if dx else abs(ys[y] - ys[yy])
                    limit = max_slope * max(dist, 0.001)
                    neighbor = out[yy][xx]
                    values.append((neighbor - limit, neighbor + limit))
                if not values:
                    continue
                lo = max(v[0] for v in values)
                hi = min(v[1] for v in values)
                old = out[y][x]
                new = min(max(old, lo), hi)
                if abs(new - old) > 1e-6:
                    out[y][x] = new
                    changed = True
        if not changed:
            break
    return out


def fmt(value: float) -> str:
    return f"{value:.8f}f"


def chunks(values: list[str], n: int = 8) -> list[str]:
    return [", ".join(values[i : i + n]) for i in range(0, len(values), n)]


def flatten(grid: list[list[float]]) -> list[float]:
    return [value for row in grid for value in row]


def median(values: list[float]) -> float:
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate smoothed KHOA bathymetry terrain C++ header.")
    parser.add_argument("--heightfield-csv", type=Path, required=True)
    parser.add_argument("--out-header", type=Path, required=True)
    parser.add_argument("--out-meta", type=Path, required=True)
    parser.add_argument("--out-smoothed-csv", type=Path, required=True)
    parser.add_argument("--grid-x", type=int, default=181)
    parser.add_argument("--grid-y", type=int, default=181)
    parser.add_argument("--smooth-sigma-cells", type=float, default=1.35)
    parser.add_argument("--smooth-passes", type=int, default=2)
    parser.add_argument("--max-slope", type=float, default=0.075)
    parser.add_argument("--slope-limit-iterations", type=int, default=2)
    args = parser.parse_args()

    raw_xs, raw_ys, raw_grid = read_heightfield(args.heightfield_csv)
    smoothed_60 = smooth_grid(raw_grid, args.smooth_sigma_cells, args.smooth_passes)
    out_xs, out_ys, resampled = resample_grid(raw_xs, raw_ys, smoothed_60, args.grid_x, args.grid_y)
    limited = slope_limit_grid(out_xs, out_ys, resampled, args.max_slope, args.slope_limit_iterations)

    flat_depths = flatten(limited)
    x_values = [fmt(x) for x in out_xs]
    y_values = [fmt(y) for y in out_ys]
    depth_values = [fmt(value) for value in flat_depths]

    header_lines = [
        "#pragma once",
        "",
        "// Generated smoothed bathymetry terrain from KHOA/data.go.kr water-depth heightfield.",
        "// This is not a new survey source; it is a smoothed/subdivided representation",
        "// of the KHOA macro-depth field for HoloOcean native SSS geometry.",
        "// Do not edit values manually; regenerate with generate_khoa_smooth_terrain_header_v2.py.",
        "",
        "namespace ShipwreckKhoaSmoothTerrainData {",
        f"static constexpr int GridX = {len(out_xs)};",
        f"static constexpr int GridY = {len(out_ys)};",
        f"static constexpr float DepthMinM = {fmt(min(flat_depths))};",
        f"static constexpr float DepthMedianSurveyWindowM = {fmt(8.92948036011342)};",
        f"static constexpr float DepthMaxM = {fmt(max(flat_depths))};",
        f"static constexpr float XMinM = {fmt(min(out_xs))};",
        f"static constexpr float XMaxM = {fmt(max(out_xs))};",
        f"static constexpr float YMinM = {fmt(min(out_ys))};",
        f"static constexpr float YMaxM = {fmt(max(out_ys))};",
        "",
        f"static constexpr float XValues[{len(out_xs)}] = {{",
        "    " + ",\n    ".join(chunks(x_values, 6)),
        "};",
        "",
        f"static constexpr float YValues[{len(out_ys)}] = {{",
        "    " + ",\n    ".join(chunks(y_values, 6)),
        "};",
        "",
        f"static constexpr float DepthM[{len(flat_depths)}] = {{",
        "    " + ",\n    ".join(chunks(depth_values, 8)),
        "};",
        "",
        "} // namespace ShipwreckKhoaSmoothTerrainData",
        "",
    ]

    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    args.out_header.write_text("\n".join(header_lines), encoding="utf-8")

    args.out_smoothed_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.out_smoothed_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["ix", "iy", "x_m", "y_m", "depth_m", "seabed_z_m"])
        for iy, y in enumerate(out_ys):
            for ix, x in enumerate(out_xs):
                depth = limited[iy][ix]
                writer.writerow([ix, iy, x, y, depth, -depth])

    raw_flat = flatten(raw_grid)
    meta = {
        "source_heightfield_csv": str(args.heightfield_csv),
        "smoothed_heightfield_csv": str(args.out_smoothed_csv),
        "raw_grid_x": len(raw_xs),
        "raw_grid_y": len(raw_ys),
        "grid_x": len(out_xs),
        "grid_y": len(out_ys),
        "points": len(flat_depths),
        "triangles": (len(out_xs) - 1) * (len(out_ys) - 1) * 2,
        "x_min_m": min(out_xs),
        "x_max_m": max(out_xs),
        "y_min_m": min(out_ys),
        "y_max_m": max(out_ys),
        "raw_depth_min_m": min(raw_flat),
        "raw_depth_median_m": median(raw_flat),
        "raw_depth_max_m": max(raw_flat),
        "smooth_depth_min_m": min(flat_depths),
        "smooth_depth_median_m": median(flat_depths),
        "smooth_depth_max_m": max(flat_depths),
        "survey_window_depth_median_m": 8.92948036011342,
        "smooth_sigma_cells_on_60x60": args.smooth_sigma_cells,
        "smooth_passes": args.smooth_passes,
        "max_slope_m_per_m": args.max_slope,
        "slope_limit_iterations": args.slope_limit_iterations,
        "note": (
            "KHOA smooth bathymetry terrain for HoloOcean native SSS. "
            "The source KHOA field is treated as macro bathymetry; smoothing/subdivision "
            "reduces artificial grid/triangle returns from a coarse water-depth mesh."
        ),
    }
    args.out_meta.parent.mkdir(parents=True, exist_ok=True)
    args.out_meta.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
