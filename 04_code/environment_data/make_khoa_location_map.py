from __future__ import annotations

import json
import math
from io import BytesIO
from pathlib import Path

import requests
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
WIDE_DIR = ROOT / "03_data" / "khoa_bathymetry" / "taean_mado_20260719"
REF_DIR = ROOT / "03_data" / "khoa_bathymetry" / "taean_mado_excavation_reference_20260719"
OUT_DIR = WIDE_DIR / "location_map"


def lonlat_to_tile(lon: float, lat: float, z: int) -> tuple[int, int]:
    lat_rad = math.radians(lat)
    n = 2**z
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return x, y


def lonlat_to_global_px(lon: float, lat: float, z: int) -> tuple[float, float]:
    siny = math.sin(math.radians(lat))
    siny = min(max(siny, -0.9999), 0.9999)
    n = 2**z
    x = 256 * n * ((lon + 180.0) / 360.0)
    y = 256 * n * (0.5 - math.log((1 + siny) / (1 - siny)) / (4 * math.pi))
    return x, y


def fetch_tile(z: int, x: int, y: int) -> Image.Image:
    url = f"https://tile.openstreetmap.org/{z}/{x}/{y}.png"
    resp = requests.get(
        url,
        timeout=15,
        headers={"User-Agent": "Codex shipwreck project map/1.0"},
    )
    resp.raise_for_status()
    return Image.open(BytesIO(resp.content)).convert("RGB")


def make_osm_map(
    bounds: tuple[float, float, float, float],
    zoom: int,
    out_size: tuple[int, int] = (920, 760),
):
    lon_min, lat_min, lon_max, lat_max = bounds
    x0, y1 = lonlat_to_tile(lon_min, lat_min, zoom)
    x1, y0 = lonlat_to_tile(lon_max, lat_max, zoom)
    xs = range(min(x0, x1), max(x0, x1) + 1)
    ys = range(min(y0, y1), max(y0, y1) + 1)

    mosaic = Image.new("RGB", (len(xs) * 256, len(ys) * 256), "white")
    for ix, x in enumerate(xs):
        for iy, y in enumerate(ys):
            mosaic.paste(fetch_tile(zoom, x, y), (ix * 256, iy * 256))

    gx_min, gy_max = lonlat_to_global_px(lon_min, lat_min, zoom)
    gx_max, gy_min = lonlat_to_global_px(lon_max, lat_max, zoom)
    origin_x = min(xs) * 256
    origin_y = min(ys) * 256
    crop_box = (
        int(gx_min - origin_x),
        int(gy_min - origin_y),
        int(gx_max - origin_x),
        int(gy_max - origin_y),
    )
    cropped = mosaic.crop(crop_box).resize(out_size, Image.Resampling.LANCZOS)

    def project(lon: float, lat: float) -> tuple[float, float]:
        gx, gy = lonlat_to_global_px(lon, lat, zoom)
        x = (gx - gx_min) / (gx_max - gx_min) * out_size[0]
        y = (gy - gy_min) / (gy_max - gy_min) * out_size[1]
        return x, y

    return cropped, project


def draw_bbox(draw: ImageDraw.ImageDraw, project, bbox: dict, color: str, width: int) -> None:
    pts = [
        project(bbox["xmin"], bbox["ymin"]),
        project(bbox["xmax"], bbox["ymin"]),
        project(bbox["xmax"], bbox["ymax"]),
        project(bbox["xmin"], bbox["ymax"]),
        project(bbox["xmin"], bbox["ymin"]),
    ]
    draw.line(pts, fill=color, width=width, joint="curve")


def label(draw: ImageDraw.ImageDraw, xy: tuple[float, float], text: str, fill="black", bg="white") -> None:
    x, y = xy
    font = ImageFont.load_default()
    lines = text.split("\n")
    boxes = [draw.textbbox((0, 0), line, font=font) for line in lines]
    w = max(box[2] - box[0] for box in boxes) + 10
    h = len(lines) * 14 + 8
    draw.rectangle([x, y, x + w, y + h], fill=bg, outline=fill)
    yy = y + 4
    for line in lines:
        draw.text((x + 5, yy), line, fill=fill, font=font)
        yy += 14


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    wide_meta = json.loads((WIDE_DIR / "khoa_bathymetry_metadata.json").read_text(encoding="utf-8"))
    ref_meta = json.loads((REF_DIR / "khoa_bathymetry_metadata.json").read_text(encoding="utf-8"))
    bbox = wide_meta["bbox"]
    ref_bbox = ref_meta["bbox"]
    center_lat = (bbox["ymin"] + bbox["ymax"]) / 2
    center_lon = (bbox["xmin"] + bbox["xmax"]) / 2

    korea_img, korea_proj = make_osm_map((124.0, 33.0, 131.5, 39.5), 7)
    korea_draw = ImageDraw.Draw(korea_img)
    draw_bbox(korea_draw, korea_proj, bbox, "#e53935", 6)
    cx, cy = korea_proj(center_lon, center_lat)
    korea_draw.ellipse([cx - 7, cy - 7, cx + 7, cy + 7], fill="#e53935", outline="white", width=2)
    label(korea_draw, (cx + 14, cy - 18), "KHOA bathymetry bbox\nTaean-Mado / West Sea", "#b71c1c")
    label(korea_draw, (15, 15), "Korea overview\nRed box = downloaded KHOA area")

    zoom_img, zoom_proj = make_osm_map((125.98, 36.56, 126.28, 36.80), 11)
    zoom_draw = ImageDraw.Draw(zoom_img)
    draw_bbox(zoom_draw, zoom_proj, bbox, "#e53935", 5)
    draw_bbox(zoom_draw, zoom_proj, ref_bbox, "#ff9800", 4)
    zcx, zcy = zoom_proj(center_lon, center_lat)
    zoom_draw.ellipse([zcx - 6, zcy - 6, zcx + 6, zcy + 6], fill="#e53935", outline="white", width=2)
    label(zoom_draw, (20, 20), "Taean-Mado zoom\nRed: 2954-point bbox\nOrange: 125-point reference")
    label(zoom_draw, (zcx + 10, zcy - 12), f"center\n{center_lat:.4f}, {center_lon:.4f}", "#b71c1c")

    canvas = Image.new("RGB", (1900, 880), "white")
    canvas.paste(korea_img, (20, 90))
    canvas.paste(zoom_img, (960, 90))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    draw.text((25, 20), "KHOA Taean-Mado Bathymetry Area on Korea Map", fill="black", font=font)
    draw.text(
        (25, 45),
        f"Wide bbox: lat {bbox['ymin']}-{bbox['ymax']}, lon {bbox['xmin']}-{bbox['xmax']}",
        fill="black",
        font=font,
    )
    draw.text(
        (25, 65),
        f"Approx. {wide_meta['local_extent_m']['width']:.0f} m x "
        f"{wide_meta['local_extent_m']['height']:.0f} m | 2954 depth points | "
        f"center {center_lat:.4f}, {center_lon:.4f}",
        fill="black",
        font=font,
    )
    draw.text(
        (25, 858),
        "Map background: OpenStreetMap tiles. Overlay: KHOA/data.go.kr water-depth API bbox.",
        fill="gray",
        font=font,
    )

    out_png = OUT_DIR / "KHOA_Taean_Mado_Korea_overview_bbox_map.png"
    canvas.save(out_png)

    readme = OUT_DIR / "README.md"
    readme.write_text(
        "\n".join(
            [
                "# KHOA Taean-Mado bathymetry location map",
                "",
                "- Source: KHOA / data.go.kr natural-science water-depth API",
                f"- Wide bbox: lat {bbox['ymin']} to {bbox['ymax']}, lon {bbox['xmin']} to {bbox['xmax']}",
                f"- Center: lat {center_lat:.6f}, lon {center_lon:.6f}",
                f"- Metric extent: about {wide_meta['local_extent_m']['width']:.0f} m x {wide_meta['local_extent_m']['height']:.0f} m",
                f"- Point count: {wide_meta['point_count']}",
                "- Location: West Sea near Taean-gun, Mado/Sinjindo area, Chungcheongnam-do, Korea",
                "",
                "Red box = 2954-point wide bbox. Orange box = 125-point narrow reference bbox.",
            ]
        ),
        encoding="utf-8",
    )
    print(out_png.resolve())
    print(readme.resolve())


if __name__ == "__main__":
    main()
