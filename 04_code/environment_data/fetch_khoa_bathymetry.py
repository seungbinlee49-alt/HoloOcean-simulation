from __future__ import annotations

import argparse
import csv
import json
import math
import os
import statistics
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


API_URL = "https://apis.data.go.kr/1192136/waterDepth/GetWaterDepthApiService"


def read_env_var(name: str) -> str:
    value = os.environ.get(name, "")
    if value:
        return value
    if sys.platform.startswith("win"):
        try:
            import winreg

            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
                reg_value, _ = winreg.QueryValueEx(key, name)
            return str(reg_value)
        except OSError:
            return ""
    return ""


@dataclass(frozen=True)
class BathyPoint:
    lat: float
    lon: float
    depth_m: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch KHOA 150 m bathymetry and convert it to a local HoloOcean terrain reference."
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--ymin", type=float, default=36.64, help="minimum latitude")
    parser.add_argument("--ymax", type=float, default=36.72, help="maximum latitude")
    parser.add_argument("--xmin", type=float, default=126.08, help="minimum longitude")
    parser.add_argument("--xmax", type=float, default=126.18, help="maximum longitude")
    parser.add_argument("--num-rows", type=int, default=300)
    parser.add_argument("--max-pages", type=int, default=200)
    parser.add_argument("--grid-size", type=int, default=80, help="resampled local heightfield grid size")
    parser.add_argument(
        "--min-water-depth",
        type=float,
        default=0.5,
        help="minimum positive depth used for HoloOcean underwater terrain products",
    )
    parser.add_argument("--service-key-env", default="KHOA_SERVICE_KEY")
    return parser.parse_args()


def request_page(service_key: str, args: argparse.Namespace, page_no: int) -> dict:
    params = {
        "serviceKey": service_key,
        "type": "json",
        "ymin": f"{args.ymin:.8f}",
        "ymax": f"{args.ymax:.8f}",
        "xmin": f"{args.xmin:.8f}",
        "xmax": f"{args.xmax:.8f}",
        "pageNo": str(page_no),
        "numOfRows": str(args.num_rows),
    }
    query = urllib.parse.urlencode(params)
    with urllib.request.urlopen(f"{API_URL}?{query}", timeout=30) as response:
        raw = response.read().decode("utf-8", errors="replace")
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"KHOA response was not JSON on page {page_no}: {raw[:300]}") from exc


def rows_from_payload(payload: dict) -> tuple[list[dict], int | None]:
    header = payload.get("header") or {}
    code = str(header.get("resultCode", ""))
    if code not in ("00", ""):
        raise RuntimeError(f"KHOA API error {code}: {header.get('resultMsg')}")

    body = payload.get("body") or {}
    total_count = body.get("totalCount")
    items = ((body.get("items") or {}).get("item") or [])
    if isinstance(items, dict):
        items = [items]
    return list(items), int(total_count) if total_count is not None else None


def fetch_all(service_key: str, args: argparse.Namespace) -> tuple[list[BathyPoint], list[dict], int | None]:
    points: list[BathyPoint] = []
    raw_pages: list[dict] = []
    total_count: int | None = None
    seen = set()

    for page_no in range(1, args.max_pages + 1):
        payload = request_page(service_key, args, page_no)
        raw_pages.append(payload)
        rows, total_count = rows_from_payload(payload)
        if not rows:
            break

        for row in rows:
            try:
                point = BathyPoint(
                    lat=float(row["lat"]),
                    lon=float(row["lot"]),
                    depth_m=float(row["dpwt"]),
                )
            except (KeyError, TypeError, ValueError):
                continue
            key = (round(point.lat, 8), round(point.lon, 8), round(point.depth_m, 4))
            if key not in seen:
                seen.add(key)
                points.append(point)

        if total_count is not None and len(points) >= total_count:
            break

    return points, raw_pages, total_count


def local_xy(points: list[BathyPoint]) -> list[tuple[float, float, float]]:
    lat0 = statistics.fmean(p.lat for p in points)
    lon0 = statistics.fmean(p.lon for p in points)
    meters_per_deg_lat = 111_320.0
    meters_per_deg_lon = 111_320.0 * math.cos(math.radians(lat0))
    return [
        (
            (p.lon - lon0) * meters_per_deg_lon,
            (p.lat - lat0) * meters_per_deg_lat,
            p.depth_m,
        )
        for p in points
    ]


def idw_resample(xyd: list[tuple[float, float, float]], grid_size: int) -> list[dict]:
    xs = [v[0] for v in xyd]
    ys = [v[1] for v in xyd]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    grid: list[dict] = []

    for iy in range(grid_size):
        y = ymin + (ymax - ymin) * iy / max(1, grid_size - 1)
        for ix in range(grid_size):
            x = xmin + (xmax - xmin) * ix / max(1, grid_size - 1)
            weighted = []
            for px, py, depth in xyd:
                d2 = (x - px) ** 2 + (y - py) ** 2
                if d2 < 1e-9:
                    weighted = [(depth, 1.0)]
                    break
                weighted.append((depth, 1.0 / d2))
            denom = sum(w for _, w in weighted)
            depth = sum(v * w for v, w in weighted) / denom
            grid.append({"ix": ix, "iy": iy, "x_m": x, "y_m": y, "depth_m": depth, "seabed_z_m": -depth})
    return grid


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def normalize(values: list[float], value: float) -> float:
    lo, hi = min(values), max(values)
    if hi <= lo:
        return 0.0
    return (value - lo) / (hi - lo)


def try_write_visuals(output_dir: Path, points: list[BathyPoint], grid: list[dict], grid_size: int) -> None:
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return

    def ramp(t: float) -> tuple[int, int, int]:
        # shallow: pale cyan, deep: dark blue
        t = max(0.0, min(1.0, t))
        return (
            int(220 - 170 * t),
            int(245 - 145 * t),
            int(255 - 70 * t),
        )

    depth_values = [p.depth_m for p in points]
    w, h = 1100, 820
    margin = 70
    lon_min, lon_max = min(p.lon for p in points), max(p.lon for p in points)
    lat_min, lat_max = min(p.lat for p in points), max(p.lat for p in points)
    img = Image.new("RGB", (w, h), "white")
    draw = ImageDraw.Draw(img)
    draw.rectangle([margin, margin, w - margin, h - margin], outline=(0, 0, 0), width=2)
    for p in points:
        x = margin + (p.lon - lon_min) / max(1e-9, lon_max - lon_min) * (w - 2 * margin)
        y = h - margin - (p.lat - lat_min) / max(1e-9, lat_max - lat_min) * (h - 2 * margin)
        color = ramp(normalize(depth_values, p.depth_m))
        draw.ellipse([x - 4, y - 4, x + 4, y + 4], fill=color, outline=(30, 30, 30))
    draw.text((margin, 20), "KHOA bathymetry points near Taean-Mado (color = depth)", fill=(0, 0, 0))
    draw.text((margin, h - 45), f"bbox lon {lon_min:.4f}..{lon_max:.4f}, lat {lat_min:.4f}..{lat_max:.4f}", fill=(0, 0, 0))
    img.save(output_dir / "khoa_bathymetry_points_map.png")

    grid_depths = [row["depth_m"] for row in grid]
    cell = 10
    grid_img = Image.new("RGB", (grid_size * cell, grid_size * cell), "white")
    grid_draw = ImageDraw.Draw(grid_img)
    for row in grid:
        ix = int(row["ix"])
        iy = int(row["iy"])
        color = ramp(normalize(grid_depths, float(row["depth_m"])))
        grid_draw.rectangle(
            [ix * cell, (grid_size - 1 - iy) * cell, (ix + 1) * cell - 1, (grid_size - iy) * cell - 1],
            fill=color,
        )
    grid_img.save(output_dir / "khoa_resampled_heightfield.png")


def main() -> int:
    args = parse_args()
    service_key = read_env_var(args.service_key_env)
    if not service_key:
        print(f"ERROR: environment variable {args.service_key_env} is not set.", file=sys.stderr)
        return 2

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    points, raw_pages, total_count = fetch_all(service_key, args)
    if not points:
        raise RuntimeError("KHOA returned no bathymetry points for this bounding box.")
    water_points = [p for p in points if p.depth_m >= args.min_water_depth]
    if not water_points:
        raise RuntimeError(f"No water-depth points deeper than {args.min_water_depth} m were found.")

    point_rows = [
        {"lat": p.lat, "lon": p.lon, "depth_m": p.depth_m, "seabed_z_m": -p.depth_m}
        for p in points
    ]
    water_point_rows = [
        {"lat": p.lat, "lon": p.lon, "depth_m": p.depth_m, "seabed_z_m": -p.depth_m}
        for p in water_points
    ]
    xyd = local_xy(points)
    water_xyd = local_xy(water_points)
    local_rows = [
        {"x_m": x, "y_m": y, "depth_m": depth, "seabed_z_m": -depth}
        for x, y, depth in xyd
    ]
    water_local_rows = [
        {"x_m": x, "y_m": y, "depth_m": depth, "seabed_z_m": -depth}
        for x, y, depth in water_xyd
    ]
    grid_rows = idw_resample(xyd, args.grid_size)
    water_grid_rows = idw_resample(water_xyd, args.grid_size)

    depths = [p.depth_m for p in points]
    water_depths = [p.depth_m for p in water_points]
    x_values = [row["x_m"] for row in local_rows]
    y_values = [row["y_m"] for row in local_rows]
    water_x_values = [row["x_m"] for row in water_local_rows]
    water_y_values = [row["y_m"] for row in water_local_rows]
    metadata = {
        "created_at": datetime.now().isoformat(timespec="seconds"),
        "source": {
            "name": "KHOA / data.go.kr natural-science water depth API",
            "api_url_without_key": API_URL,
            "description": "150 m interval bathymetry points: lat, lot, dpwt.",
        },
        "bbox": {"ymin": args.ymin, "ymax": args.ymax, "xmin": args.xmin, "xmax": args.xmax},
        "point_count": len(points),
        "water_point_count": len(water_points),
        "min_water_depth_filter_m": args.min_water_depth,
        "reported_total_count": total_count,
        "depth_m": {
            "min": min(depths),
            "max": max(depths),
            "mean": statistics.fmean(depths),
            "median": statistics.median(depths),
        },
        "water_only_depth_m": {
            "min": min(water_depths),
            "max": max(water_depths),
            "mean": statistics.fmean(water_depths),
            "median": statistics.median(water_depths),
        },
        "local_extent_m": {
            "x_min": min(x_values),
            "x_max": max(x_values),
            "y_min": min(y_values),
            "y_max": max(y_values),
            "width": max(x_values) - min(x_values),
            "height": max(y_values) - min(y_values),
        },
        "water_only_local_extent_m": {
            "x_min": min(water_x_values),
            "x_max": max(water_x_values),
            "y_min": min(water_y_values),
            "y_max": max(water_y_values),
            "width": max(water_x_values) - min(water_x_values),
            "height": max(water_y_values) - min(water_y_values),
        },
        "holoocean_use": {
            "macro_depth_reference_m": statistics.median(water_depths),
            "macro_depth_range_m": [min(water_depths), max(water_depths)],
            "recommended_role": "Use as macro bathymetry / regional depth slope. Keep procedural ripple, mound, sediment and false-positive objects for SSS-scale micro terrain.",
            "why_micro_terrain_still_needed": "KHOA grid is about 150 m interval, which is coarser than the 1-10 m seabed relief and clutter visible in SSS crops.",
        },
    }
    holoocean_config = {
        "preset_name": "khoa_taean_mado_macro_bathymetry_v1",
        "source_metadata": "khoa_bathymetry_metadata.json",
        "heightfield_csv": "khoa_bathymetry_water_resampled_heightfield.csv",
        "recommended_world": "SimpleUnderwater",
        "surface_z_m": 0.0,
        "seabed_z_mode": "seabed_z_m = -depth_m",
        "nominal_water_depth_m": round(statistics.median(water_depths), 3),
        "survey_area_note": "The KHOA bbox covers km-scale macro bathymetry. A local HoloOcean test scene can use a scaled subset or the median depth plus procedural micro-terrain.",
        "keep_procedural_micro_terrain": [
            "sand ripples",
            "low ridges",
            "mounds",
            "sediment blankets",
            "rock/gear/rope false positives",
        ],
        "replace_previous_assumptions": [
            "arbitrary nominal water depth",
            "arbitrary large-scale seabed slope",
        ],
    }

    write_csv(output_dir / "khoa_bathymetry_points.csv", point_rows, ["lat", "lon", "depth_m", "seabed_z_m"])
    write_csv(output_dir / "khoa_bathymetry_water_points.csv", water_point_rows, ["lat", "lon", "depth_m", "seabed_z_m"])
    write_csv(output_dir / "khoa_bathymetry_local_points.csv", local_rows, ["x_m", "y_m", "depth_m", "seabed_z_m"])
    write_csv(output_dir / "khoa_bathymetry_water_local_points.csv", water_local_rows, ["x_m", "y_m", "depth_m", "seabed_z_m"])
    write_csv(
        output_dir / "khoa_bathymetry_resampled_heightfield.csv",
        grid_rows,
        ["ix", "iy", "x_m", "y_m", "depth_m", "seabed_z_m"],
    )
    write_csv(
        output_dir / "khoa_bathymetry_water_resampled_heightfield.csv",
        water_grid_rows,
        ["ix", "iy", "x_m", "y_m", "depth_m", "seabed_z_m"],
    )
    (output_dir / "khoa_raw_pages.json").write_text(json.dumps(raw_pages, ensure_ascii=False, indent=2), encoding="utf-8")
    (output_dir / "khoa_bathymetry_metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    (output_dir / "holoocean_khoa_terrain_config.json").write_text(
        json.dumps(holoocean_config, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    try_write_visuals(output_dir, points, grid_rows, args.grid_size)
    water_visual_dir = output_dir / "water_only_visuals"
    water_visual_dir.mkdir(exist_ok=True)
    try_write_visuals(water_visual_dir, water_points, water_grid_rows, args.grid_size)

    readme = f"""# KHOA Taean-Mado Bathymetry v1

## What Was Downloaded

KHOA/data.go.kr natural-science water-depth API data were fetched for the Taean-Mado area.

- API: `{API_URL}`
- Parameters: `type=json`, `ymin/ymax`, `xmin/xmax`, `pageNo`, `numOfRows`
- Response fields used: `lat`, `lot`, `dpwt`
- Downloaded points: `{len(points)}`
- Water-only points (`depth >= {args.min_water_depth:.1f} m`): `{len(water_points)}`
- Depth range: `{min(depths):.2f} m` to `{max(depths):.2f} m`
- Median depth: `{statistics.median(depths):.2f} m`
- Water-only depth range: `{min(water_depths):.2f} m` to `{max(water_depths):.2f} m`
- Water-only median depth: `{statistics.median(water_depths):.2f} m`

## How This Should Be Used In HoloOcean

This replaces the previous arbitrary macro-depth assumption. It should be used as:

1. regional water-depth reference,
2. large-scale seabed slope/depth trend,
3. justification for Taean-Mado / West-Sea shallow-water scene parameters.

It does **not** remove the need for SSS-scale procedural terrain. The API interval is about 150 m, so features like sand ripples, small mounds, sediment blankets, rocks, ropes and fishing gear are below this data resolution. Those should remain as procedural micro-terrain / false-positive objects, but now placed on top of a KHOA-derived macro terrain.

## Main Files

- `khoa_bathymetry_points.csv`: original lat/lon/depth points
- `khoa_bathymetry_water_points.csv`: water-only points for underwater simulation
- `khoa_bathymetry_local_points.csv`: same points converted to local meter coordinates
- `khoa_bathymetry_resampled_heightfield.csv`: IDW-resampled local heightfield grid
- `khoa_bathymetry_water_resampled_heightfield.csv`: water-only heightfield for HoloOcean macro terrain
- `khoa_bathymetry_points_map.png`: visual check of downloaded points
- `khoa_resampled_heightfield.png`: visual check of resampled terrain
- `water_only_visuals/`: visual check after removing land/intertidal-like shallow points
- `holoocean_khoa_terrain_config.json`: HoloOcean-facing terrain configuration
"""
    (output_dir / "README.md").write_text(readme, encoding="utf-8")

    print(json.dumps(metadata, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
