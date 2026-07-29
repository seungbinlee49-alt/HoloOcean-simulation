from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import uuid
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


WORKSPACE = Path(__file__).resolve().parents[2]
HEADER_PATH = (
    WORKSPACE
    / "10_holoocean_sim"
    / "holoocean_2_4_dev"
    / "engine"
    / "Source"
    / "Holodeck"
    / "HolodeckCore"
    / "Private"
    / "ShipwreckKhoaSmoothTerrainData.generated.h"
)
BINARY_PATH = (
    WORKSPACE
    / "10_holoocean_sim"
    / "holoocean_2_4_official_runtime"
    / "2.4.0"
    / "worlds"
    / "Ocean"
    / "Windows"
    / "Holodeck"
    / "Binaries"
    / "Win64"
    / "Holodeck.exe"
)
DEFAULT_OUT = WORKSPACE / "06_results" / "20260722_khoa_survey_ground_contact_check"
MADO_SCENES_DIR = (
    WORKSPACE / "10_holoocean_sim" / "holoocean_2_4_dev" / "engine" / "Content" / "Config" / "mado_scenes"
)
MADO_TERRAIN_DIR = (
    WORKSPACE / "10_holoocean_sim" / "holoocean_2_4_dev" / "engine" / "Content" / "Config" / "mado_terrain"
)

WRECKS = [
    ("intact_A", "wreck", -58.0, -22.0, 22.0),
    ("buried_A", "wreck", -47.0, 9.0, -18.0),
    ("fragmented_A", "wreck", -32.0, 31.0, 41.0),
    ("lowrelief_A", "wreck", -10.0, -20.0, 12.0),
    ("ribfield_A", "wreck", 7.0, 17.0, -28.0),
    ("debrisfield_A", "wreck", 19.0, -31.0, 36.0),
    ("fragmented_B", "wreck", -6.0, 36.0, -52.0),
    ("buried_B", "wreck", 24.0, 4.0, 63.0),
]
GEAR = [
    ("gear_rope_01", "gear_rope", -63.0, 4.0, 28.0),
    ("gear_rope_02", "gear_rope", -52.0, -36.0, -8.0),
    ("gear_rope_03", "gear_rope", 0.0, -24.0, 16.0),
    ("gear_rope_04", "gear_rope", 25.0, -18.0, -35.0),
]
ROCKS = [
    ("rock_mound_01", "rock_mound", -66.0, 18.0, -30.0),
    ("rock_mound_02", "rock_mound", -60.0, -6.0, -13.0),
    ("rock_mound_03", "rock_mound", -44.0, -30.0, 4.0),
    ("rock_mound_04", "rock_mound", -9.0, -18.0, 21.0),
]
OBJECTS = WRECKS + GEAR + ROCKS


def parse_scalar(text: str, name: str, cast=float):
    pattern = rf"{name}\s*=\s*([-+0-9.eE]+)f?"
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f"Could not parse scalar {name}")
    return cast(match.group(1))


def parse_array(text: str, name: str) -> np.ndarray:
    pattern = rf"{name}\[\d+\]\s*=\s*\{{(.*?)\}};"
    match = re.search(pattern, text, re.S)
    if not match:
        raise ValueError(f"Could not parse array {name}")
    values = [
        float(v.rstrip("f"))
        for v in re.findall(r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?f?", match.group(1))
    ]
    return np.asarray(values, dtype=np.float64)


def load_terrain() -> dict:
    text = HEADER_PATH.read_text(encoding="utf-8")
    grid_x = int(parse_scalar(text, "GridX", int))
    grid_y = int(parse_scalar(text, "GridY", int))
    x_values = parse_array(text, "XValues")
    y_values = parse_array(text, "YValues")
    depths = parse_array(text, "DepthM")
    if depths.size != grid_x * grid_y:
        raise ValueError(f"DepthM has {depths.size} values, expected {grid_x * grid_y}")
    return {
        "grid_x": grid_x,
        "grid_y": grid_y,
        "x_values": x_values,
        "y_values": y_values,
        "depths": depths.reshape((grid_y, grid_x)),
        "depth_min": float(parse_scalar(text, "DepthMinM")),
        "depth_median_survey_window": float(parse_scalar(text, "DepthMedianSurveyWindowM")),
        "depth_max": float(parse_scalar(text, "DepthMaxM")),
        "x_min": float(parse_scalar(text, "XMinM")),
        "x_max": float(parse_scalar(text, "XMaxM")),
        "y_min": float(parse_scalar(text, "YMinM")),
        "y_max": float(parse_scalar(text, "YMaxM")),
    }


def terrain_depth_at(terrain: dict, x: float, y: float) -> float:
    x_values = terrain["x_values"]
    y_values = terrain["y_values"]
    depths = terrain["depths"]
    grid_x = terrain["grid_x"]
    grid_y = terrain["grid_y"]
    u = (x - terrain["x_min"]) / (terrain["x_max"] - terrain["x_min"]) * (grid_x - 1)
    v = (y - terrain["y_min"]) / (terrain["y_max"] - terrain["y_min"]) * (grid_y - 1)
    ix0 = int(np.clip(np.floor(u), 0, grid_x - 2))
    iy0 = int(np.clip(np.floor(v), 0, grid_y - 2))
    ix1 = ix0 + 1
    iy1 = iy0 + 1
    tx = float(np.clip(u - ix0, 0.0, 1.0))
    ty = float(np.clip(v - iy0, 0.0, 1.0))
    d00 = depths[iy0, ix0]
    d10 = depths[iy0, ix1]
    d01 = depths[iy1, ix0]
    d11 = depths[iy1, ix1]
    d0 = d00 + (d10 - d00) * tx
    d1 = d01 + (d11 - d01) * tx
    return float(d0 + (d1 - d0) * ty)


def load_terrain_csv(path: Path) -> dict:
    """Parses a Content/Config/mado_terrain/*.csv heightfield (ix,iy,x_m,y_m,depth_m rows in
    raster order, iy outer / ix inner -- same file MadoSceneConfig.cpp's LoadTerrainCsv reads
    on the engine side) into the same dict shape load_terrain() returns."""
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError(f"empty terrain csv: {path}")
    xs: list[float] = []
    ys: list[float] = []
    depths: list[float] = []
    first_iy = None
    grid_x = None
    for row in rows:
        iy = int(row["iy"])
        if first_iy is None:
            first_iy = iy
        elif grid_x is None and iy != first_iy:
            grid_x = len(xs)
        xs.append(float(row["x_m"]))
        ys.append(float(row["y_m"]))
        depths.append(float(row["depth_m"]))
    if not grid_x or len(depths) % grid_x != 0:
        raise ValueError(f"could not detect grid_x from terrain csv: {path}")
    grid_y = len(depths) // grid_x
    x_values = np.asarray(xs[:grid_x], dtype=np.float64)
    y_values = np.asarray(ys[::grid_x], dtype=np.float64)
    depths_arr = np.asarray(depths, dtype=np.float64).reshape((grid_y, grid_x))
    return {
        "grid_x": grid_x,
        "grid_y": grid_y,
        "x_values": x_values,
        "y_values": y_values,
        "depths": depths_arr,
        "depth_min": float(depths_arr.min()),
        "depth_median_survey_window": float(np.median(depths_arr)),
        "depth_max": float(depths_arr.max()),
        "x_min": float(x_values[0]),
        "x_max": float(x_values[-1]),
        "y_min": float(y_values[0]),
        "y_max": float(y_values[-1]),
    }


def load_terrain_for_scene_proxy(scene_proxy: str) -> dict:
    """Loads whichever terrain heightfield the ENGINE will actually use for this scene_proxy
    value, so Python-side pose computation (AUV altitude, ground-contact checks) matches what
    gets rendered. Mirrors MadoSceneConfig.cpp's scene JSON -> terrain_data_source resolution
    instead of always reading the old District-II-only compiled header text. Falls back to
    load_terrain() (the old behavior) for anything that isn't a recognized scene JSON, so
    existing non-Mado callers are unaffected."""
    if scene_proxy.lower().endswith(".json"):
        scene_json_path = MADO_SCENES_DIR / scene_proxy
        if scene_json_path.is_file():
            scene = json.loads(scene_json_path.read_text(encoding="utf-8"))
            terrain_source = scene.get("terrain_data_source", "")
            if terrain_source:
                csv_path = MADO_TERRAIN_DIR / terrain_source
                if csv_path.is_file():
                    return load_terrain_csv(csv_path)
    return load_terrain()


def object_rows(terrain: dict) -> list[dict]:
    rows = []
    for name, kind, x, y, yaw in OBJECTS:
        depth = terrain_depth_at(terrain, x, y)
        terrain_z = -depth
        clearance = 0.015 if kind == "wreck" else 0.006
        rows.append(
            {
                "name": name,
                "class": kind,
                "x_m": x,
                "y_m": y,
                "yaw_deg": yaw,
                "khoa_depth_m": depth,
                "terrain_z_m": terrain_z,
                "expected_bottom_z_m": terrain_z + clearance,
                "expected_clearance_m": clearance,
                "placement_policy": "UE bounds bottom snapped to terrain z + clearance",
            }
        )
    return rows


def font(size: int) -> ImageFont.ImageFont:
    for candidate in [r"C:\Windows\Fonts\malgun.ttf", r"C:\Windows\Fonts\arial.ttf"]:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            pass
    return ImageFont.load_default()


def draw_contact_map(terrain: dict, rows: list[dict], out_path: Path) -> None:
    x_min, x_max = -72.0, 32.0
    y_min, y_max = -42.0, 42.0
    width, height = 1500, 1050
    margin = 90

    canvas = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(canvas)
    title_font = font(30)
    label_font = font(18)
    small_font = font(15)

    def sx(x):
        return margin + (x - x_min) / (x_max - x_min) * (width - margin * 2)

    def sy(y):
        return height - margin - (y - y_min) / (y_max - y_min) * (height - margin * 2)

    # Draw local bathymetry as a smooth grayscale field inside the survey window.
    nx, ny = 260, 210
    tile = Image.new("L", (nx, ny), 0)
    pix = tile.load()
    for iy in range(ny):
        y = y_min + (y_max - y_min) * (ny - 1 - iy) / (ny - 1)
        for ix in range(nx):
            x = x_min + (x_max - x_min) * ix / (nx - 1)
            depth = terrain_depth_at(terrain, x, y)
            val = int(np.clip((depth - 7.6) / 2.6 * 255, 0, 255))
            pix[ix, iy] = val
    bathy = tile.resize((width - margin * 2, height - margin * 2), Image.Resampling.BILINEAR)
    bathy_rgb = Image.merge("RGB", (bathy, bathy, bathy))
    canvas.paste(bathy_rgb, (margin, margin))

    draw.rectangle((margin, margin, width - margin, height - margin), outline=(0, 0, 0), width=3)
    for y in [-36, -18, 0, 18, 24, 36]:
        draw.line((sx(-66), sy(y), sx(26), sy(y)), fill=(20, 20, 20), width=3)
        draw.text((sx(27) + 6, sy(y) - 11), f"y={y:g}", fill=(40, 40, 40), font=small_font)

    color_by_kind = {
        "wreck": (220, 60, 55),
        "gear_rope": (0, 165, 155),
        "rock_mound": (90, 90, 90),
    }
    shape_by_kind = {"wreck": "circle", "gear_rope": "square", "rock_mound": "square"}
    for row in rows:
        x = sx(row["x_m"])
        y = sy(row["y_m"])
        color = color_by_kind[row["class"]]
        r = 9 if row["class"] == "wreck" else 7
        if shape_by_kind[row["class"]] == "circle":
            draw.ellipse((x - r, y - r, x + r, y + r), fill=color, outline=(0, 0, 0), width=1)
        else:
            draw.rectangle((x - r, y - r, x + r, y + r), fill=color, outline=(0, 0, 0), width=1)
        label = f"{row['name']} bottom={row['expected_bottom_z_m']:.2f}"
        draw.text((x + 10, y - 12), label, fill=color, font=small_font)

    draw.text(
        (margin, 28),
        "KHOA smooth bathymetry contact check: objects placed at terrain_z(x,y)",
        fill=(0, 0, 0),
        font=title_font,
    )
    draw.text(
        (margin, height - 58),
        "Background grayscale = KHOA-derived depth inside the local survey window. Black lines = lawnmower passes.",
        fill=(0, 0, 0),
        font=label_font,
    )
    canvas.save(out_path)


def draw_contact_profile(rows: list[dict], out_path: Path) -> None:
    width, height = 1500, 860
    margin_l, margin_r = 110, 70
    margin_t, margin_b = 90, 90
    x_min, x_max = -72.0, 32.0
    z_values = [float(row["terrain_z_m"]) for row in rows] + [
        float(row["expected_bottom_z_m"]) for row in rows
    ]
    z_min = min(z_values) - 0.25
    z_max = max(z_values) + 0.45

    canvas = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(canvas)
    title_font = font(30)
    label_font = font(18)
    small_font = font(15)

    def sx(x):
        return margin_l + (x - x_min) / (x_max - x_min) * (width - margin_l - margin_r)

    def sz(z):
        return height - margin_b - (z - z_min) / (z_max - z_min) * (height - margin_t - margin_b)

    draw.text(
        (margin_l, 28),
        "KHOA ground-contact side profile: spawn_z follows terrain_z at each object position",
        fill=(0, 0, 0),
        font=title_font,
    )
    draw.line((sx(x_min), sz(z_min), sx(x_max), sz(z_min)), fill=(0, 0, 0), width=2)
    draw.line((sx(x_min), sz(z_min), sx(x_min), sz(z_max)), fill=(0, 0, 0), width=2)
    for z in np.linspace(math.ceil(z_min * 10) / 10, math.floor(z_max * 10) / 10, 6):
        y = sz(float(z))
        draw.line((sx(x_min), y, sx(x_max), y), fill=(225, 225, 225), width=1)
        draw.text((18, y - 9), f"z={z:.2f} m", fill=(80, 80, 80), font=small_font)

    color_by_kind = {
        "wreck": (220, 60, 55),
        "gear_rope": (0, 165, 155),
        "rock_mound": (90, 90, 90),
    }
    for row in rows:
        x = sx(float(row["x_m"]))
        z_ground = sz(float(row["terrain_z_m"]))
        z_spawn = sz(float(row["expected_bottom_z_m"]))
        color = color_by_kind[row["class"]]
        draw.line((x, z_ground, x, z_spawn), fill=color, width=4)
        draw.ellipse((x - 7, z_ground - 7, x + 7, z_ground + 7), fill=(0, 0, 0))
        draw.rectangle((x - 10, min(z_ground, z_spawn) - 12, x + 10, max(z_ground, z_spawn) + 4), outline=color, width=3)
        draw.text((x + 8, z_spawn - 18), row["name"], fill=color, font=small_font)

    draw.text(
        (margin_l, height - 48),
        "Black dots = KHOA terrain_z(x,y). Colored short bars = expected actor bottom z after UE bounds snap.",
        fill=(0, 0, 0),
        font=label_font,
    )
    canvas.save(out_path)


def save_contact_csv(rows: list[dict], out_path: Path) -> None:
    fields = [
        "name",
        "class",
        "x_m",
        "y_m",
        "yaw_deg",
        "khoa_depth_m",
        "terrain_z_m",
        "expected_bottom_z_m",
        "expected_clearance_m",
        "placement_policy",
    ]
    with out_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def make_viewport_scenario(width: int, height: int) -> dict:
    return {
        "name": "khoa_survey_ground_contact_viewport_check",
        "package_name": "Ocean",
        "world": "FlatUnderwater",
        "env_min": [-900, -850, -30],
        "env_max": [900, 760, 5],
        "main_agent": "auv0",
        "ticks_per_sec": 30,
        "frames_per_sec": 30,
        "window_width": width,
        "window_height": height,
        "agents": [
            {
                "agent_name": "auv0",
                "agent_type": "TorpedoAUV",
                "control_scheme": 0,
                "location": [-66.0, -36.0, -4.2],
                "rotation": [0.0, 0.0, 0.0],
                "sensors": [
                    {
                        "sensor_type": "ViewportCapture",
                        "sensor_name": "ViewportCapture",
                        "Hz": 5,
                        "configuration": {"CaptureWidth": width, "CaptureHeight": height},
                    },
                    {"sensor_type": "PoseSensor", "sensor_name": "PoseSensor", "Hz": 5},
                ],
            }
        ],
    }


def look_direction(location: list[float], target: list[float]) -> list[float]:
    loc = np.asarray(location, dtype=np.float64)
    tgt = np.asarray(target, dtype=np.float64)
    delta = tgt - loc
    norm = float(np.linalg.norm(delta))
    if norm <= 1e-9:
        return [1.0, 0.0, 0.0]
    return (delta / norm).astype(float).tolist()


def save_frame(frame: np.ndarray, out_path: Path) -> None:
    arr = np.asarray(frame)
    if arr.ndim == 3 and arr.shape[-1] == 4:
        # HoloOcean 2.4 Windows ViewportCapture frames arrive as BGRA.
        arr = arr[:, :, [2, 1, 0]]
    elif arr.ndim == 3 and arr.shape[-1] == 3:
        arr = arr[:, :, [2, 1, 0]]
    Image.fromarray(arr.astype(np.uint8)).save(out_path)


def apply_clear_visuals(env) -> None:
    env.should_render_viewport(True)
    env.set_render_quality(3, should_keep_fps=True)
    env.change_time_of_day(12.0)
    env.water_color(0.16, 0.38, 0.48)
    # Visual inspection only: reduce fog so the KHOA seabed/contact relation is visible.
    # This does not affect SSS acquisition runs.
    env.water_fog(0.0008, 220.0, 0.16, 0.38, 0.48)
    env.air_fog(0.0, 220.0, 0.75, 0.86, 0.92)
    try:
        env.turn_on_flashlight(
            "flashlight1",
            intensity=30000.0,
            beam_width=55.0,
            location_z_offset=0.0,
            angle_pitch=-12.0,
        )
    except Exception:
        pass


def annotate_image(path: Path, out_path: Path, title: str, subtitle: str) -> None:
    img = Image.open(path).convert("RGB")
    draw = ImageDraw.Draw(img, "RGBA")
    draw.rectangle((12, 12, img.width - 12, 94), fill=(0, 0, 0, 145))
    draw.text((26, 22), title, fill=(255, 255, 255), font=font(24))
    draw.text((26, 59), subtitle, fill=(235, 235, 235), font=font(17))
    img.save(out_path)


def make_montage(paths: list[Path], out_path: Path) -> None:
    cell_w, cell_h = 640, 410
    cols = 2
    rows = int(math.ceil(len(paths) / cols))
    canvas = Image.new("RGB", (cell_w * cols, cell_h * rows), "white")
    draw = ImageDraw.Draw(canvas)
    for idx, path in enumerate(paths):
        img = Image.open(path).convert("RGB")
        img.thumbnail((cell_w - 30, cell_h - 66), Image.Resampling.LANCZOS)
        x0 = (idx % cols) * cell_w
        y0 = (idx // cols) * cell_h
        draw.text((x0 + 14, y0 + 12), path.stem, fill=(0, 0, 0), font=font(16))
        canvas.paste(img, (x0 + (cell_w - img.width) // 2, y0 + 44))
    canvas.save(out_path)


def capture_viewports(
    rows: list[dict],
    out_dir: Path,
    width: int,
    height: int,
    binary_path: Path,
    scene_proxy: str,
) -> list[Path]:
    import holoocean
    from holoocean.environments import HoloOceanEnvironment

    os.environ["HOLOOCEAN_SHIPWRECK_SPAWN"] = "1"
    os.environ["HOLOOCEAN_SHIPWRECK_SCENE_PRESET"] = scene_proxy
    os.environ["HOLOOCEAN_SHIPWRECK_BASE_Z"] = str(-8.92948036011342)
    os.environ["HOLOOCEAN_SHIPWRECK_RESET_OCTREE_CACHE"] = "1"
    os.environ.pop("HOLOOCEAN_SHIPWRECK_VISUAL_DEBUG_GUIDES", None)
    os.environ.pop("HOLOOCEAN_SHIPWRECK_DEBUG_VISUAL", None)
    if "literature" not in scene_proxy.lower():
        os.environ.pop("HOLOOCEAN_SHIPWRECK_FORCE_LITERATURE_PROXIES", None)

    scenario = make_viewport_scenario(width, height)
    interesting = [row for row in rows if row["class"] == "wreck"][:8]
    saved: list[Path] = []

    with HoloOceanEnvironment(
        scenario=scenario,
        binary_path=str(binary_path),
        uuid=f"khoa_ground_contact_{uuid.uuid4().hex[:8]}",
        show_viewport=False,
        verbose=False,
    ) as env:
        apply_clear_visuals(env)
        for _ in range(40):
            env.act("auv0", [0, 0, 0, 0, 0])
            env.tick()

        views = [
            {
                "name": "00_full_oblique",
                "loc": [-72.0, -54.0, -2.2],
                "target": [-20.0, 0.0, -8.9],
                "label": "full local survey area",
            },
            {
                "name": "00b_full_topdown",
                "loc": [-20.0, -2.0, -0.9],
                "target": [-20.0, -2.0, -9.1],
                "label": "full local survey area, downward camera",
            },
            {
                "name": "01_center_low_oblique",
                "loc": [-34.0, -18.0, -6.3],
                "target": [-18.0, -4.0, -8.9],
                "label": "low camera near KHOA seabed",
            },
        ]
        for row in interesting:
            x = float(row["x_m"])
            y = float(row["y_m"])
            z = float(row["terrain_z_m"])
            views.append(
                {
                    "name": f"{row['name']}_contact_view",
                    "loc": [x + 4.5, y - 3.5, z + 1.8],
                    "target": [x, y, z + 0.25],
                    "label": f"{row['name']} contact check, terrain_z={z:.2f}",
                }
            )
            views.append(
                {
                    "name": f"{row['name']}_topdown_contact_view",
                    "loc": [x, y, z + 4.8],
                    "target": [x, y, z - 0.25],
                    "label": f"{row['name']} top-down contact check, terrain_z={z:.2f}",
                }
            )

        for view in views:
            env.move_viewport(view["loc"], look_direction(view["loc"], view["target"]))
            frame = None
            for _ in range(22):
                env.act("auv0", [0, 0, 0, 0, 0])
                state = env.tick()
                if "ViewportCapture" in state:
                    frame = np.asarray(state["ViewportCapture"]).copy()
            if frame is None:
                continue
            raw = out_dir / f"{view['name']}_raw.png"
            ann = out_dir / f"{view['name']}.png"
            save_frame(frame, raw)
            annotate_image(
                raw,
                ann,
                f"{view['name']} | loc={view['loc']} target={view['target']}",
                f"Actual HoloOcean viewport: FlatUnderwater + {scene_proxy}",
            )
            saved.append(ann)
    return saved


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--skip-viewport", action="store_true")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--binary-path", type=Path, default=BINARY_PATH)
    parser.add_argument("--scene-proxy", default="khoa_survey_scene_v1")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    terrain = load_terrain()
    rows = object_rows(terrain)
    csv_path = args.output_dir / "01_khoa_ground_contact_table.csv"
    map_path = args.output_dir / "02_khoa_ground_contact_map.png"
    profile_path = args.output_dir / "02b_khoa_ground_contact_side_profile.png"
    save_contact_csv(rows, csv_path)
    draw_contact_map(terrain, rows, map_path)
    draw_contact_profile(rows, profile_path)

    viewport_files: list[Path] = []
    montage_path = args.output_dir / "03_actual_holoocean_viewport_contact_montage.png"
    if not args.skip_viewport:
        viewport_files = capture_viewports(
            rows,
            args.output_dir,
            args.width,
            args.height,
            args.binary_path,
            args.scene_proxy,
        )
        if viewport_files:
            make_montage(viewport_files, montage_path)

    meta = {
        "terrain_header": str(HEADER_PATH),
        "binary_path": str(args.binary_path),
        "scene_proxy": args.scene_proxy,
        "terrain": {
            "grid_x": terrain["grid_x"],
            "grid_y": terrain["grid_y"],
            "x_min": terrain["x_min"],
            "x_max": terrain["x_max"],
            "y_min": terrain["y_min"],
            "y_max": terrain["y_max"],
            "depth_min": terrain["depth_min"],
            "depth_median_survey_window": terrain["depth_median_survey_window"],
            "depth_max": terrain["depth_max"],
        },
        "files": {
            "contact_table": str(csv_path),
            "contact_map": str(map_path),
            "contact_side_profile": str(profile_path),
            "viewport_montage": str(montage_path) if viewport_files else "",
        },
        "objects": rows,
    }
    (args.output_dir / "README.md").write_text(
        "\n".join(
            [
                "# KHOA survey scene ground-contact check",
                "",
                "Purpose: verify that the survey wrecks and clutter are placed on the KHOA-derived seabed height, not floating at an arbitrary z value.",
                "",
                "## How the check works",
                f"The C++ scene preset `{args.scene_proxy}` calls `ShipwreckProjectKhoaTerrainZAt(x,y)` for each wreck/clutter placement.",
                "The C++ placement then snaps each proxy actor's UE bounds bottom to the terrain z plus a small clearance.",
                "This script parses the same generated KHOA terrain header and calculates the expected terrain/bottom z at each object coordinate.",
                "",
                "## Main outputs",
                f"- `{csv_path.name}`: object x/y, KHOA depth, terrain z, and expected bottom-contact z.",
                f"- `{map_path.name}`: local survey map with KHOA depth shading and object spawn z labels.",
                f"- `{profile_path.name}`: side-profile check showing object z placement on terrain z.",
                f"- `{montage_path.name}`: actual HoloOcean viewport captures, if available.",
                "",
                "## Important interpretation",
                "Viewport images can be visually unclear because underwater fog, lighting, and camera angle hide the seabed surface.",
                "The CSV/map are the stronger evidence for whether object z placement follows the KHOA terrain.",
                "",
                "## Metadata",
                "```json",
                json.dumps(meta, ensure_ascii=False, indent=2),
                "```",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    (args.output_dir / "metadata.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"OUT={args.output_dir}")
    print(f"TABLE={csv_path}")
    print(f"MAP={map_path}")
    print(f"PROFILE={profile_path}")
    if viewport_files:
        print(f"VIEWPORT_MONTAGE={montage_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
