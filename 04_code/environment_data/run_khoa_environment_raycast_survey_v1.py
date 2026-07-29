from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


WORKSPACE = Path(__file__).resolve().parents[2]
VIS_DIR = WORKSPACE / "04_code" / "visualization"
if str(VIS_DIR) not in sys.path:
    sys.path.insert(0, str(VIS_DIR))

from check_khoa_survey_ground_contact_24 import (  # noqa: E402
    BINARY_PATH,
    OBJECTS,
    load_terrain,
    object_rows,
    terrain_depth_at,
)


DEFAULT_BUOY_SUMMARY = (
    WORKSPACE
    / "03_data"
    / "khoa_observation_current_20260722"
    / "buoy_latest_taean_nearby_summary.json"
)


@dataclass
class BuoyEnvironment:
    station: str
    observed_at: str
    distance_km: float
    current_dir_deg: float
    current_speed_mps: float
    temperature_c: float
    salinity_psu: float
    sound_speed_mps: float
    water_density_kgm3: float


def seawater_density_unesco_atm(temperature_c: float, salinity_psu: float) -> float:
    """UNESCO 1983 atmospheric-pressure seawater density approximation."""
    t = float(temperature_c)
    s = max(float(salinity_psu), 0.0)
    rho_w = (
        999.842594
        + 6.793952e-2 * t
        - 9.095290e-3 * t**2
        + 1.001685e-4 * t**3
        - 1.120083e-6 * t**4
        + 6.536332e-9 * t**5
    )
    a = 0.824493 - 4.0899e-3 * t + 7.6438e-5 * t**2 - 8.2467e-7 * t**3 + 5.3875e-9 * t**4
    b = -5.72466e-3 + 1.0227e-4 * t - 1.6546e-6 * t**2
    c = 4.8314e-4
    return float(rho_w + a * s + b * (s**1.5) + c * s**2)


def sound_speed_mackenzie(temperature_c: float, salinity_psu: float, depth_m: float) -> float:
    """Mackenzie-style shallow-water sound speed approximation."""
    t = float(temperature_c)
    s = float(salinity_psu)
    d = max(float(depth_m), 0.0)
    return float(
        1448.96
        + 4.591 * t
        - 5.304e-2 * t**2
        + 2.374e-4 * t**3
        + 1.340 * (s - 35.0)
        + 1.630e-2 * d
        + 1.675e-7 * d**2
        - 1.025e-2 * t * (s - 35.0)
        - 7.139e-13 * t * d**3
    )


def load_buoy_environment(path: Path, depth_m: float) -> BuoyEnvironment:
    data = json.loads(path.read_text(encoding="utf-8"))
    rows = data.get("nearest_station_latest", [])
    chosen = None
    for row in rows:
        item = row.get("latest_item") or {}
        if item.get("wtem") is not None and item.get("slnty") is not None and item.get("crdir") is not None:
            chosen = row
            break
    if chosen is None:
        raise ValueError(f"No usable buoy row with wtem/slnty/crdir in {path}")

    item = chosen["latest_item"]
    temp_c = float(item["wtem"])
    salinity = float(item["slnty"])
    # KHOA latest buoy crsp is commonly represented in cm/s. Keep the raw value in metadata
    # and convert conservatively here for a weak kinematic drift proxy.
    current_speed_mps = float(item.get("crsp") or 0.0) / 100.0
    sound_speed = sound_speed_mackenzie(temp_c, salinity, depth_m)
    density = seawater_density_unesco_atm(temp_c, salinity)
    return BuoyEnvironment(
        station=str(chosen.get("obsCode", "")),
        observed_at=str(item.get("obsrvnDt", "")),
        distance_km=float(chosen.get("dist_to_taean_mado_center_km", math.nan)),
        current_dir_deg=float(item.get("crdir") or 0.0),
        current_speed_mps=current_speed_mps,
        temperature_c=temp_c,
        salinity_psu=salinity,
        sound_speed_mps=sound_speed,
        water_density_kgm3=density,
    )


def default_water_environment() -> BuoyEnvironment:
    return BuoyEnvironment(
        station="default_holoocean_baseline",
        observed_at="not_applied",
        distance_km=math.nan,
        current_dir_deg=0.0,
        current_speed_mps=0.0,
        temperature_c=math.nan,
        salinity_psu=math.nan,
        sound_speed_mps=1480.0,
        water_density_kgm3=997.0,
    )


def apply_mado_ctd_overrides(env: BuoyEnvironment, depth_m: float) -> BuoyEnvironment:
    """Override temperature/salinity/density/sound-speed with the real 20-day CTD average
    measured at the Mado-4ho excavation site (2015-09-19..2015-10-09, 10-min interval,
    total-average row: turbidity 63.53 NTU, depth 10.14 m, T=20.35 C, pH=7.93, S=29.95 psu,
    rho=1020.87 kg/m3). This is an in-situ measurement from the actual Mado wreck site, closer
    and more direct than the distant (37 km) Taean-port buoy reading. Current direction/speed
    fields are left untouched (direction still uses the buoy proxy elsewhere in this script)."""
    temp_c = 20.35
    salinity = 29.95
    sound_speed = sound_speed_mackenzie(temp_c, salinity, depth_m)
    env.station = "mado_4ho_ctd_20150919_1009_20day_avg"
    env.observed_at = "2015-09-19..2015-10-09 (20-day, 10-min interval mean)"
    env.distance_km = 0.0
    env.temperature_c = temp_c
    env.salinity_psu = salinity
    env.sound_speed_mps = sound_speed
    env.water_density_kgm3 = 1020.87
    return env


def flat_object_rows(flat_depth_m: float) -> list[dict]:
    """Object placement table for a non-KHOA flat-seabed baseline.

    This keeps the same survey objects and XY/yaw layout while removing KHOA
    bathymetry from the placement calculation. It is used only for fair
    ablation runs where the question is "what changes when environment inputs
    are not applied?"
    """
    depth = float(flat_depth_m)
    terrain_z = -depth
    rows = []
    for name, kind, x, y, yaw in OBJECTS:
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
                "placement_policy": "flat seabed baseline: UE bounds bottom snapped to flat z + clearance",
            }
        )
    return rows


MADO_REPORT_MAJOR_OBJECTS = [
    ("soft_mud_core", "mado_facies_soft_mud", 0.0, 0.0, -4.0, 0.006),
    ("shell_mud_18F", "mado_facies_shell_mud", 16.0, -7.0, 12.0, 0.006),
    ("shell_mud_18H_dense", "mado_facies_shell_mud", 39.0, 25.0, -10.0, 0.006),
    ("disturbed_shell_mud_18E", "mado_facies_disturbed_shell_mud", -14.0, -30.0, 7.0, 0.006),
    ("hard_mud_18G", "mado_facies_hard_mud", 27.0, -17.0, 18.0, 0.006),
    ("hard_mud_halstone_shell_19BC", "mado_facies_hard_mud_shell_halstone", -42.0, 28.0, -16.0, 0.006),
    ("hard_mud_gravel_west_anomaly", "mado_facies_hard_mud_gravel", -38.0, 2.0, 4.0, 0.006),
    # East anomaly report text (20x20m->30x30m grid, N36.41.18.3 E126.08.25.6) only describes
    # black/dark-gray hard mud to 3m depth, no gravel/river stones - unlike the west anomaly.
    ("hard_mud_east_anomaly", "mado_facies_hard_mud", 42.0, -31.0, -9.0, 0.006),
    # shell_gravel_west_south_transition removed: no report coordinate or measured survey grid,
    # only a general directional-trend sentence from the 19-A block description.
    ("anchor_176_Mado18_38", "mado_anchor_stone", -50.0, -38.0, 17.0, 0.008),
    ("anchor_177_Mado18_64", "mado_anchor_stone", -36.0, -17.0, -26.0, 0.008),
    ("anchor_178_Mado18_103", "mado_anchor_stone", -18.0, -37.0, 41.0, 0.008),
    ("anchor_179_Mado18_104", "mado_anchor_stone", -6.0, -29.0, -12.0, 0.008),
    ("anchor_180_Mado18_105", "mado_anchor_stone", 8.0, -36.0, 58.0, 0.008),
    ("anchor_181_Mado18_124", "mado_anchor_stone", 20.0, -14.0, 8.0, 0.008),
    ("anchor_182_Mado18_135_18HCluster", "mado_anchor_stone", 31.0, 18.0, -38.0, 0.008),
    ("anchor_183_Mado18_136_18HCluster", "mado_anchor_stone", 41.0, 25.0, 24.0, 0.008),
    ("anchor_184_Mado18_137_18HCluster", "mado_anchor_stone", 36.0, 33.0, -10.0, 0.008),
    ("anchor_185_Mado18_141_18HCluster", "mado_anchor_stone", 49.0, 19.0, 44.0, 0.008),
    ("anchor_186_Mado18_142_18HCluster", "mado_anchor_stone", 29.0, 29.0, 77.0, 0.008),
    ("anchor_187_Mado18_143_18HCluster", "mado_anchor_stone", 48.0, 35.0, -52.0, 0.008),
    ("anchor_188_Mado19_33", "mado_anchor_stone", -40.0, 31.0, 32.0, 0.008),
    ("anchor_189_Mado18_168_east_anomaly", "mado_anchor_stone", 44.0, -25.0, 13.0, 0.008),
    ("anchor_190_Mado18_173_east_anomaly", "mado_anchor_stone", 52.0, -35.0, -31.0, 0.008),
    ("anchor_191_Mado18_174_east_anomaly", "mado_anchor_stone", 34.0, -38.0, 63.0, 0.008),
    ("shell_pebble_cluster_18H", "mado_scatter_cluster", 39.0, 25.0, 0.0, 0.008),
    ("shell_pebble_cluster_19BC", "mado_scatter_cluster", -42.0, 28.0, 0.0, 0.008),
    ("shell_pebble_cluster_18E", "mado_scatter_cluster", -14.0, -30.0, 0.0, 0.008),
    ("halstone_cluster_west_south", "mado_scatter_cluster", -48.0, -26.0, 0.0, 0.008),
    ("riverstone_cluster_east_anomaly", "mado_scatter_cluster", 42.0, -31.0, 0.0, 0.008),
    ("reef_edge_west_south", "mado_reef_edge_cue", -57.0, -44.0, 16.0, 0.008),
]

def recommended_azimuth_ray_count(azimuth_deg: float, range_bins: int = 1000) -> int:
    """Minimum AzimuthRayCount needed to avoid the concentric-ring aliasing artifact.

    RaycastSidescanSonar bins each ping's rays into `range_bins` histogram columns. If the
    angular spacing between adjacent rays is too coarse, some columns receive zero real hits
    on every ping (not just occasionally), which shows up as static concentric rings/arcs in
    the waterfall image once range-gain normalization divides by those near-empty columns.

    This threshold is empirically calibrated, not a first-principles derivation: a simple
    flat-bottom secant-geometry model undershoots the real requirement by ~50x, most likely
    because the true SonarSocket mount tilt (baked into the AUV mesh, not in this script) does
    not match the naive "azimuth=0 is nadir" assumption. The two data points below are real,
    directly captured/verified results, not estimates:

      - Azimuth=85 deg,  AzimuthRayCount=8500  (0.0100 deg/ray) -> confirmed ring artifact
      - Azimuth=170 deg, AzimuthRayCount=68000 (0.0025 deg/ray) -> confirmed clean, no artifact

    both at RangeBins=1000, RangeMax in [50, 60] m, altitude ~4.7 m above seabed. The 0.0025
    deg/ray threshold is used here as the safe angular resolution, scaled linearly with
    range_bins (finer range binning plausibly needs proportionally finer angular sampling --
    theoretically motivated, not independently verified). If you change RangeMax or altitude
    far from the validated range (50-60 m / ~4-5 m), re-verify visually for ring artifacts
    rather than trusting this formula blindly.
    """
    safe_deg_per_ray = 0.0025 * (1000.0 / max(int(range_bins), 1))
    return max(math.ceil(azimuth_deg / safe_deg_per_ray), 2)


DEFAULT_ENV_MIN = [-900.0, -850.0, -30.0]
DEFAULT_ENV_MAX = [900.0, 760.0, 5.0]
# Kept in sync with ShipwreckKhoaSmoothTerrainData::XMinM/XMaxM/YMinM/YMaxM (regenerated
# 2026-07-28 at 401x351 resolution). Update these together whenever the terrain header is
# regenerated -- a stale range here is exactly the "survey path goes outside the terrain,
# produces empty/black data" failure mode the SSS integration guide (SCENE_INTEGRATION_GUIDE.md
# section 4.2) warns about.
MADO_REPORT_ENV_MIN = [-143.21, -112.57, -12.0]
MADO_REPORT_ENV_MAX = [107.17, 108.20, 5.0]


def mado_report_environment_rows(depth_at_fn) -> list[dict]:
    rows = []
    for name, kind, x, y, yaw, clearance in MADO_REPORT_MAJOR_OBJECTS:
        depth = float(depth_at_fn(x, y))
        terrain_z = -depth
        is_facies_zone = kind.startswith("mado_facies")
        rows.append(
            {
                "name": name,
                "class": kind,
                "x_m": x,
                "y_m": y,
                "yaw_deg": yaw,
                "khoa_depth_m": depth,
                "terrain_z_m": terrain_z,
                "expected_bottom_z_m": terrain_z if is_facies_zone else terrain_z + clearance,
                "expected_clearance_m": 0.0 if is_facies_zone else clearance,
                "placement_policy": (
                    "mado_report_environment_v1: non-geometry report facies zone on KHOA seabed; "
                    "subtle effective impedance applied in Raycast terrain hits"
                    if is_facies_zone
                    else "mado_report_environment_v1: KHOA terrain snap + 120x100m report-core layout"
                ),
            }
        )
    return rows


def normalize_to_u8(arr: np.ndarray) -> np.ndarray:
    arr = np.asarray(arr, dtype=np.float32)
    finite = arr[np.isfinite(arr)]
    if finite.size == 0:
        return np.zeros(arr.shape, dtype=np.uint8)
    lo = float(np.percentile(finite, 1.0))
    hi = float(np.percentile(finite, 99.5))
    if hi <= lo:
        lo = float(finite.min())
        hi = float(finite.max())
    if hi <= lo:
        return np.zeros(arr.shape, dtype=np.uint8)
    return (np.clip((arr - lo) / (hi - lo), 0.0, 1.0) * 255).astype(np.uint8)


def smooth_1d(values: np.ndarray, radius: int = 21) -> np.ndarray:
    radius = max(int(radius), 1)
    kernel = np.ones(radius * 2 + 1, dtype=np.float32)
    kernel /= kernel.sum()
    padded = np.pad(values.astype(np.float32), (radius, radius), mode="edge")
    return np.convolve(padded, kernel, mode="valid")


def range_gain_equalize(waterfall: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Create a diagnostic TVG/EGN-style view without changing the raw SSS.

    The raw raycast SSS is preserved. This compensates the cross-track range
    profile so the seabed background is closer to mid-gray, which is a standard
    way to inspect SSS imagery and diagnose whether geometry changes are being
    hidden by range falloff or frame-level normalization.
    """
    arr = np.asarray(waterfall, dtype=np.float32)
    out = arr.copy()
    cols = out.shape[1]
    mid = cols // 2
    profile = np.zeros(cols, dtype=np.float32)
    eps = 1e-6

    for start, end in [(0, mid), (mid, cols)]:
        part = out[:, start:end]
        if part.size == 0:
            continue
        p = np.percentile(part, 70.0, axis=0).astype(np.float32)
        p = smooth_1d(p, radius=max(7, part.shape[1] // 80))
        good = p > max(float(np.percentile(p, 35.0)), eps)
        target = float(np.median(p[good])) if np.any(good) else float(np.median(p))
        target = max(target, eps)
        gain = np.clip(target / np.maximum(p, eps), 0.15, 8.0)
        out[:, start:end] = part * gain[None, :]
        profile[start:end] = p
    return normalize_to_u8(out), profile


def chase_camera_pose(
    auv_x: float,
    auv_y: float,
    auv_z: float,
    yaw_deg: float,
    distance_m: float = 10.0,
    height_m: float = 5.0,
) -> tuple[list[float], list[float]]:
    """Camera behind-and-above the AUV, looking forward-down along its heading.

    move_viewport expects [roll, pitch, yaw] in DEGREES, not a direction vector --
    see capture_clean_terrain_diver_view_v1.py for the same fix applied to the
    ground-contact viewport script, which was silently passing a near-zero-degree
    rotation and never actually turning the camera.
    """
    yaw_rad = math.radians(yaw_deg)
    fwd_x, fwd_y = math.cos(yaw_rad), math.sin(yaw_rad)
    # Clamp below the water surface (z=0) so a large height offset combined with a
    # shallow AUV altitude can't push the chase camera above water (seeing sky/waves
    # instead of the underwater scene).
    cam_z = min(auv_z + height_m, -1.0)
    cam_loc = [auv_x - fwd_x * distance_m, auv_y - fwd_y * distance_m, cam_z]
    target = [auv_x + fwd_x * 3.0, auv_y + fwd_y * 3.0, auv_z]
    delta = [target[0] - cam_loc[0], target[1] - cam_loc[1], target[2] - cam_loc[2]]
    horizontal = math.hypot(delta[0], delta[1])
    cam_yaw = math.degrees(math.atan2(delta[1], delta[0]))
    cam_pitch = math.degrees(math.atan2(-delta[2], max(horizontal, 1e-6)))
    return cam_loc, [0.0, cam_pitch, cam_yaw]


def current_unit_from_direction(direction_deg: float) -> np.ndarray:
    # Treat x as east-west and y as north-south. 0 deg = +y, 90 deg = +x.
    rad = math.radians(float(direction_deg))
    return np.asarray([math.sin(rad), math.cos(rad)], dtype=np.float32)


def pass_plan(
    x_min: float,
    x_max: float,
    y_tracks: list[float],
    rows_per_pass: int,
) -> tuple[list[dict], list[dict]]:
    samples: list[dict] = []
    ranges: list[dict] = []
    row_idx = 0
    for pass_idx, y in enumerate(y_tracks):
        left_to_right = pass_idx % 2 == 0
        xs = np.linspace(x_min, x_max, rows_per_pass)
        if not left_to_right:
            xs = xs[::-1]
        start_row = row_idx
        for local_idx, x in enumerate(xs):
            yaw = 0.0 if left_to_right else 180.0
            samples.append(
                {
                    "row": row_idx,
                    "pass_index": pass_idx + 1,
                    "local_row": local_idx,
                    "x": float(x),
                    "y": float(y),
                    "yaw_deg": yaw,
                }
            )
            row_idx += 1
        ranges.append(
            {
                "pass_index": pass_idx + 1,
                "start_row": start_row,
                "end_row": row_idx - 1,
                "y_track_m": float(y),
                "direction": "left_to_right" if left_to_right else "right_to_left",
            }
        )
    return samples, ranges


def build_scenario(args: argparse.Namespace, env: BuoyEnvironment, start: dict) -> dict:
    base_sensor_cfg = {
        "RangeBins": args.range_bins,
        "Azimuth": args.azimuth,
        "Elevation": args.elevation,
        "RangeMin": args.range_min,
        "RangeMax": args.range_max,
        "WaterDensity": env.water_density_kgm3,
        "WaterSpeedSound": env.sound_speed_mps,
        "AddSigma": args.add_sigma,
        "MultSigma": args.mult_sigma,
        "ElevationRayCount": args.elevation_ray_count,
        "AzimuthRayCount": args.azimuth_ray_count,
        "ViewRegion": False,
        "ViewRays": False,
        "ViewPoints": False,
    }
    sensors = [
        {"sensor_type": "PoseSensor", "sensor_name": "PoseSensor", "Hz": args.sonar_hz},
    ]
    if not args.visual_only:
        sensors = [
            {
                "sensor_type": "RaycastSidescanSonar",
                "sensor_name": "SidescanSonarRight",
                "socket": "SonarSocket",
                "Hz": args.sonar_hz,
                "rotation": [0.0, 0.0, -42.5],
                "configuration": dict(base_sensor_cfg),
            },
            {
                "sensor_type": "RaycastSidescanSonar",
                "sensor_name": "SidescanSonarLeft",
                "socket": "SonarSocket",
                "Hz": args.sonar_hz,
                "rotation": [0.0, 0.0, 42.5],
                "configuration": dict(base_sensor_cfg),
            },
            *sensors,
        ]

    return {
        "name": "khoa_environment_raycast_survey_v1",
        "package_name": args.package_name,
        "world": args.world,
        "env_min": args.env_min,
        "env_max": args.env_max,
        "main_agent": "auv0",
        "ticks_per_sec": args.render_fps,
        "frames_per_sec": args.render_fps,
        "agents": [
            {
                "agent_name": "auv0",
                "agent_type": "TorpedoAUV",
                "control_scheme": 0,
                "location": [start["x"], start["y"], start["z"]],
                "rotation": [0.0, 0.0, start["yaw_deg"]],
                "sensors": sensors,
            }
        ],
    }


def font(size: int) -> ImageFont.ImageFont:
    for candidate in [r"C:\Windows\Fonts\malgun.ttf", r"C:\Windows\Fonts\arial.ttf"]:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            pass
    return ImageFont.load_default()


def save_montage(image: np.ndarray, ranges: list[dict], out_path: Path) -> None:
    gap = 36
    title_h = 28
    strip_images = []
    label_font = font(16)
    for row in ranges:
        strip = image[int(row["start_row"]) : int(row["end_row"]) + 1, :]
        canvas = Image.new("L", (strip.shape[1], strip.shape[0] + title_h), 255)
        canvas.paste(Image.fromarray(strip), (0, title_h))
        draw = ImageDraw.Draw(canvas)
        draw.text(
            (6, 4),
            f"pass_{row['pass_index']:02d} y={row['y_track_m']:g} {row['direction']}",
            fill=0,
            font=label_font,
        )
        strip_images.append(canvas)
    width = max(im.width for im in strip_images)
    height = sum(im.height for im in strip_images) + gap * (len(strip_images) - 1)
    montage = Image.new("L", (width, height), 255)
    y = 0
    for im in strip_images:
        montage.paste(im, (0, y))
        y += im.height + gap
    montage.save(out_path)


def save_range_profile(path: Path, profile: np.ndarray) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerow(["col", "range_profile_p70_smoothed"])
        for idx, value in enumerate(profile):
            writer.writerow([idx, f"{float(value):.8f}"])


def write_csv(path: Path, rows: list[dict], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description="KHOA terrain + buoy environment + Raycast SSS survey.")
    parser.add_argument("--output-dir", type=Path, default=WORKSPACE / "06_results" / "20260722_khoa_env_raycast_survey_v1")
    parser.add_argument("--binary-path", type=Path, default=BINARY_PATH)
    parser.add_argument("--package-name", default="Ocean")
    parser.add_argument("--world", default="FlatUnderwater")
    parser.add_argument("--scene-proxy", default="khoa_survey_scene_v1")
    parser.add_argument("--buoy-summary", type=Path, default=DEFAULT_BUOY_SUMMARY)
    parser.add_argument(
        "--terrain-profile",
        choices=["khoa", "flat"],
        default="khoa",
        help="khoa follows the KHOA smooth bathymetry grid; flat uses one constant seabed depth.",
    )
    parser.add_argument(
        "--flat-depth-m",
        type=float,
        default=8.18,
        help="Constant seabed depth used only with --terrain-profile flat.",
    )
    parser.add_argument(
        "--water-profile",
        choices=["default", "buoy", "mado_ctd"],
        default="mado_ctd",
        help=(
            "default uses HoloOcean-style rho/c values; buoy uses the distant (37 km) KHOA "
            "Taean-port buoy temperature/salinity; mado_ctd (default) overrides temperature, "
            "salinity, and water density with the real 20-day CTD average measured at the "
            "Mado-4ho excavation site itself (2015-09-19..2015-10-09, T=20.35 C, S=29.95 psu, "
            "rho=1020.87 kg/m3), which is a closer and more direct in-situ measurement than the "
            "distant buoy. Current direction still comes from the buoy proxy in all modes."
        ),
    )
    parser.add_argument("--x-min", type=float, default=-66.0)
    parser.add_argument("--x-max", type=float, default=26.0)
    parser.add_argument("--y-tracks", default="-36,-18,0,18,24")
    parser.add_argument("--rows-per-pass", type=int, default=300)
    parser.add_argument(
        "--auto-rows-from-speed",
        action="store_true",
        help=(
            "Compute rows-per-pass from x survey length, sonar Hz, and speed-mps so "
            "the waterfall row spacing is physically consistent with the requested speed."
        ),
    )
    parser.add_argument("--render-fps", type=int, default=10)
    parser.add_argument("--speed-mps", type=float, default=1.2)
    parser.add_argument("--auv-altitude-above-seabed", type=float, default=4.7)
    parser.add_argument("--drift-scale", type=float, default=0.08)
    parser.add_argument(
        "--mado-current-speed-mps",
        type=float,
        default=0.115,
        help=(
            "Drift-proxy current magnitude, taken directly from the 2011 Mado-2ho excavation "
            "report's measured current table at the Mado wreck site itself (10-day, 4x/day "
            "current-meter readings: 0.0247-0.2151 m/s, mean ~0.10-0.13 m/s). This replaces the "
            "distant Taean-port buoy reading (37 km away) as the drift-magnitude source; the "
            "buoy is still used for direction and for temperature/salinity/density."
        ),
    )
    parser.add_argument("--max-drift-m", type=float, default=15.0)
    parser.add_argument("--wiggle-amp-m", type=float, default=0.35)
    parser.add_argument("--yaw-amp-deg", type=float, default=2.0)
    parser.add_argument("--yaw-period-sec", type=float, default=24.0)
    parser.add_argument("--range-bins", type=int, default=1000)
    parser.add_argument("--range-min", type=float, default=0.5)
    parser.add_argument("--range-max", type=float, default=60.0)
    parser.add_argument("--azimuth", type=float, default=85.0)
    parser.add_argument("--elevation", type=float, default=0.25)
    parser.add_argument(
        "--azimuth-ray-count",
        type=int,
        default=None,
        help=(
            "Rays per ping across the azimuth fan. Default (None) auto-computes a safe value "
            "from --azimuth/--range-bins via recommended_azimuth_ray_count() to avoid the "
            "concentric-ring aliasing artifact (see that function's docstring for the "
            "validated data points behind this). Override only if you have separately verified "
            "your chosen value is artifact-free for your azimuth/range-bins combination."
        ),
    )
    parser.add_argument("--elevation-ray-count", type=int, default=4)
    parser.add_argument("--add-sigma", type=float, default=0.0)
    parser.add_argument("--mult-sigma", type=float, default=0.0)
    parser.add_argument("--sonar-hz", type=int, default=10)
    parser.add_argument("--env-min", nargs=3, type=float, default=DEFAULT_ENV_MIN.copy())
    parser.add_argument("--env-max", nargs=3, type=float, default=DEFAULT_ENV_MAX.copy())
    parser.add_argument("--show-viewport", action="store_true")
    parser.add_argument("--visual-only", action="store_true")
    parser.add_argument("--hold-seconds", type=float, default=20.0)
    parser.add_argument(
        "--nice-visuals",
        action="store_true",
        help=(
            "Reduce water/air fog, set noon lighting, and turn on the AUV flashlight so the "
            "seabed and wrecks are actually visible in --show-viewport sessions. Visual only: "
            "does not change materials.csv, sonar sensor config, or SSS output in any way."
        ),
    )
    parser.add_argument(
        "--chase-camera",
        action="store_true",
        help=(
            "Move the viewport camera to follow the AUV each step (behind-and-above, looking "
            "forward-down along its heading). Without this the viewport camera never moves and "
            "just sits at its default spawn position while the AUV teleports around off-screen. "
            "Visual only, no effect on SSS output."
        ),
    )
    parser.add_argument("--chase-camera-distance", type=float, default=6.0)
    parser.add_argument("--chase-camera-height", type=float, default=1.5)
    args = parser.parse_args()

    if args.azimuth_ray_count is None:
        args.azimuth_ray_count = recommended_azimuth_ray_count(args.azimuth, args.range_bins)
        print(
            f"azimuth_ray_count not set, auto-computed {args.azimuth_ray_count} "
            f"from azimuth={args.azimuth:g} deg, range_bins={args.range_bins} "
            "(see recommended_azimuth_ray_count() for the validated basis)."
        )

    if (
        "mado_report" in args.scene_proxy.lower()
        and args.env_min == DEFAULT_ENV_MIN
        and args.env_max == DEFAULT_ENV_MAX
    ):
        args.env_min = MADO_REPORT_ENV_MIN.copy()
        args.env_max = MADO_REPORT_ENV_MAX.copy()

    terrain = load_terrain()
    y_tracks = [float(v.strip()) for v in args.y_tracks.split(",") if v.strip()]
    if args.auto_rows_from_speed:
        track_len_m = abs(float(args.x_max) - float(args.x_min))
        row_spacing_m = max(float(args.speed_mps), 1e-6) / max(float(args.sonar_hz), 1.0)
        args.rows_per_pass = max(2, int(round(track_len_m / row_spacing_m)) + 1)
    samples, pass_ranges = pass_plan(args.x_min, args.x_max, y_tracks, args.rows_per_pass)
    effective_row_spacing_m = abs(float(args.x_max) - float(args.x_min)) / max(args.rows_per_pass - 1, 1)
    effective_speed_mps = effective_row_spacing_m * max(float(args.sonar_hz), 1.0)

    def depth_at(x: float, y: float) -> float:
        if args.terrain_profile == "flat":
            return float(args.flat_depth_m)
        return float(terrain_depth_at(terrain, x, y))

    nominal_depth = float(depth_at((args.x_min + args.x_max) * 0.5, float(np.median(y_tracks))))
    if args.water_profile in ("buoy", "mado_ctd"):
        buoy_env = load_buoy_environment(args.buoy_summary, nominal_depth)
    else:
        buoy_env = default_water_environment()
    if args.water_profile == "mado_ctd":
        buoy_env = apply_mado_ctd_overrides(buoy_env, nominal_depth)
    current_unit = current_unit_from_direction(buoy_env.current_dir_deg)
    # Magnitude comes from the real Mado-2ho site current-meter measurements, not the distant
    # buoy reading (see --mado-current-speed-mps help). Direction still uses the buoy proxy.
    applied_drift_speed = min(args.mado_current_speed_mps, args.max_drift_m / 120.0)

    out = args.output_dir
    (out / "01_SSS").mkdir(parents=True, exist_ok=True)
    (out / "02_logs").mkdir(parents=True, exist_ok=True)
    (out / "03_environment").mkdir(parents=True, exist_ok=True)

    os.environ["HOLOOCEAN_SHIPWRECK_SPAWN"] = "1"
    os.environ["HOLOOCEAN_SHIPWRECK_SCENE_PRESET"] = args.scene_proxy
    os.environ["HOLOOCEAN_SHIPWRECK_BASE_Z"] = f"{-nominal_depth:.6f}"
    os.environ["HOLOOCEAN_SHIPWRECK_TERRAIN_PROFILE"] = args.terrain_profile
    if args.terrain_profile == "flat":
        os.environ["HOLOOCEAN_SHIPWRECK_FLAT_DEPTH_M"] = f"{float(args.flat_depth_m):.6f}"
    else:
        os.environ.pop("HOLOOCEAN_SHIPWRECK_FLAT_DEPTH_M", None)
    os.environ["HOLOOCEAN_SHIPWRECK_RESET_OCTREE_CACHE"] = "1"
    # Keep production survey runs clean. Visual/debug guides and literature stress
    # proxies are useful for inspection, but they should never leak into the
    # baseline SSS collection unless the scene preset itself explicitly asks for
    # them.
    os.environ.pop("HOLOOCEAN_SHIPWRECK_VISUAL_DEBUG_GUIDES", None)
    os.environ.pop("HOLOOCEAN_SHIPWRECK_DEBUG_VISUAL", None)
    if "literature" not in args.scene_proxy.lower():
        os.environ.pop("HOLOOCEAN_SHIPWRECK_FORCE_LITERATURE_PROXIES", None)

    if "mado_report" in args.scene_proxy.lower():
        placement_rows = mado_report_environment_rows(depth_at)
    else:
        placement_rows = object_rows(terrain) if args.terrain_profile == "khoa" else flat_object_rows(args.flat_depth_m)
    write_csv(
        out / "03_environment" / "khoa_object_ground_contact_table.csv",
        placement_rows,
        [
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
        ],
    )

    first = samples[0]
    first_depth = depth_at(first["x"], first["y"])
    first["z"] = -first_depth + args.auv_altitude_above_seabed
    scenario = build_scenario(args, buoy_env, first)

    print("Starting KHOA environment Raycast survey.")
    print(f"visual_only={args.visual_only}")
    print(f"world={args.world}, scene_proxy={args.scene_proxy}")
    print(f"rows={len(samples)}, passes={len(pass_ranges)}, range_bins={args.range_bins}")
    print(
        f"speed target={args.speed_mps:.3f} m/s, rows_per_pass={args.rows_per_pass}, "
        f"row_spacing={effective_row_spacing_m:.3f} m, effective_speed={effective_speed_mps:.3f} m/s"
    )
    print(
        f"buoy={buoy_env.station} at {buoy_env.observed_at}, "
        f"T={buoy_env.temperature_c:.2f} C, S={buoy_env.salinity_psu:.2f} psu, "
        f"rho={buoy_env.water_density_kgm3:.2f}, c={buoy_env.sound_speed_mps:.2f}"
    )
    print(
        f"buoy current raw={buoy_env.current_speed_mps:.3f} m/s (direction proxy only), "
        f"dir={buoy_env.current_dir_deg:.1f} deg, "
        f"Mado-2ho measured current speed={args.mado_current_speed_mps:.3f} m/s, "
        f"applied drift speed={applied_drift_speed:.3f} m/s, yaw_amp={args.yaw_amp_deg:.1f} deg"
    )

    import holoocean
    from holoocean.environments import HoloOceanEnvironment

    rows: list[np.ndarray] = []
    pose_rows: list[dict] = []
    zeros = np.zeros(5, dtype=np.float32)
    uuid_name = f"khoa_env_raycast_{uuid.uuid4().hex[:8]}"
    with HoloOceanEnvironment(
        scenario=scenario,
        binary_path=str(args.binary_path),
        uuid=uuid_name,
        show_viewport=args.show_viewport,
        verbose=False,
    ) as env:
        if args.nice_visuals:
            env.should_render_viewport(True)
            env.set_render_quality(3, should_keep_fps=True)
            env.change_time_of_day(12.0)
            env.water_color(0.20, 0.45, 0.52)
            env.water_fog(0.00005, 400.0, 0.20, 0.45, 0.52)
            env.air_fog(0.0, 400.0, 0.75, 0.86, 0.92)
            try:
                env.turn_on_flashlight(
                    "flashlight1",
                    intensity=60000.0,
                    beam_width=65.0,
                    location_z_offset=0.0,
                    angle_pitch=-25.0,
                )
            except Exception:
                pass

        for sample in samples:
            row_idx = int(sample["row"])
            elapsed = row_idx / max(float(args.sonar_hz), 1.0)
            planned_xy = np.asarray([sample["x"], sample["y"]], dtype=np.float32)
            drift = current_unit * min(applied_drift_speed * elapsed, args.max_drift_m)
            wiggle = float(args.wiggle_amp_m) * np.asarray(
                [
                    math.sin(2 * math.pi * elapsed / max(args.yaw_period_sec, 1.0) + 0.7),
                    math.sin(2 * math.pi * elapsed / max(args.yaw_period_sec * 1.37, 1.0)),
                ],
                dtype=np.float32,
            )
            actual_xy = planned_xy + drift + wiggle
            depth = depth_at(float(actual_xy[0]), float(actual_xy[1]))
            z = -depth + args.auv_altitude_above_seabed
            yaw = float(sample["yaw_deg"]) + args.yaw_amp_deg * math.sin(
                2 * math.pi * elapsed / max(args.yaw_period_sec, 1.0)
            )

            env.agents["auv0"].teleport(
                location=np.asarray([actual_xy[0], actual_xy[1], z], dtype=np.float32),
                rotation=np.asarray([0.0, 0.0, yaw], dtype=np.float32),
            )
            if args.chase_camera:
                cam_loc, cam_rot = chase_camera_pose(
                    float(actual_xy[0]),
                    float(actual_xy[1]),
                    float(z),
                    yaw,
                    distance_m=args.chase_camera_distance,
                    height_m=args.chase_camera_height,
                )
                env.move_viewport(cam_loc, cam_rot)
            env.act("auv0", zeros)
            state = env.tick()
            if "SidescanSonarRight" in state and "SidescanSonarLeft" in state:
                right = np.asarray(state["SidescanSonarRight"], dtype=np.float32).reshape(-1)
                left = np.asarray(state["SidescanSonarLeft"], dtype=np.float32).reshape(-1)
                rows.append(np.concatenate([left[::-1], right], axis=0))
            pose_rows.append(
                {
                    "row": row_idx,
                    "pass_index": int(sample["pass_index"]),
                    "planned_x_m": float(planned_xy[0]),
                    "planned_y_m": float(planned_xy[1]),
                    "actual_x_m": float(actual_xy[0]),
                    "actual_y_m": float(actual_xy[1]),
                    "actual_z_m": float(z),
                    "khoa_depth_m": float(depth),
                    "yaw_deg": float(yaw),
                    "drift_x_m": float(drift[0]),
                    "drift_y_m": float(drift[1]),
                }
            )
            if row_idx % 300 == 0:
                print(f"row={row_idx}, pass={sample['pass_index']}, collected={len(rows)}")

        if args.show_viewport and args.hold_seconds > 0:
            print(f"Holding viewport for {args.hold_seconds:.1f} seconds.")
            hold_ticks = int(args.hold_seconds * max(args.render_fps, 1))
            for _ in range(hold_ticks):
                env.act("auv0", zeros)
                env.tick()

    full_path = None
    montage_path = None
    waterfall = None
    if rows:
        waterfall = np.stack(rows, axis=0)
        np.save(out / "01_SSS" / "khoa_environment_raycast_survey_raw.npy", waterfall)
        image = normalize_to_u8(waterfall)
        range_image, range_profile = range_gain_equalize(waterfall)
        full_path = out / "01_SSS" / "khoa_environment_raycast_survey_full.png"
        montage_path = out / "01_SSS" / "khoa_environment_raycast_survey_5pass_montage.png"
        range_full_path = out / "01_SSS" / "khoa_environment_raycast_survey_full_range_gain_diagnostic.png"
        range_montage_path = out / "01_SSS" / "khoa_environment_raycast_survey_5pass_montage_range_gain_diagnostic.png"
        Image.fromarray(image).save(full_path)
        save_montage(image, pass_ranges, montage_path)
        Image.fromarray(range_image).save(range_full_path)
        save_montage(range_image, pass_ranges, range_montage_path)
        save_range_profile(out / "02_logs" / "range_profile_p70_smoothed.csv", range_profile)
    elif not args.visual_only:
        raise RuntimeError("No SSS rows collected.")

    write_csv(
        out / "02_logs" / "pose_trace_with_buoy_drift.csv",
        pose_rows,
        [
            "row",
            "pass_index",
            "planned_x_m",
            "planned_y_m",
            "actual_x_m",
            "actual_y_m",
            "actual_z_m",
            "khoa_depth_m",
            "yaw_deg",
            "drift_x_m",
            "drift_y_m",
        ],
    )
    write_csv(
        out / "02_logs" / "pass_ranges.csv",
        pass_ranges,
        ["pass_index", "start_row", "end_row", "y_track_m", "direction"],
    )
    if args.terrain_profile == "khoa":
        terrain_meta = {
            "profile": "khoa",
            "grid_x": terrain["grid_x"],
            "grid_y": terrain["grid_y"],
            "x_min": terrain["x_min"],
            "x_max": terrain["x_max"],
            "y_min": terrain["y_min"],
            "y_max": terrain["y_max"],
            "depth_min": terrain["depth_min"],
            "depth_max": terrain["depth_max"],
            "nominal_depth_at_survey_center_m": nominal_depth,
        }
    else:
        terrain_meta = {
            "profile": "flat",
            "flat_depth_m": float(args.flat_depth_m),
            "nominal_depth_at_survey_center_m": nominal_depth,
            "note": "KHOA bathymetry is disabled for this ablation baseline.",
        }

    meta = {
        "holoocean_version": getattr(holoocean, "__version__", "unknown"),
        "binary_path": str(args.binary_path),
        "world": args.world,
        "scene_proxy": args.scene_proxy,
        "terrain": terrain_meta,
        "buoy_environment": buoy_env.__dict__,
        "water_profile": args.water_profile,
        "drift_proxy": {
            "direction_deg": buoy_env.current_dir_deg,
            "direction_source": "Taean-port buoy (37 km away), direction only",
            "buoy_raw_current_speed_mps_unused_for_magnitude": buoy_env.current_speed_mps,
            "mado_2ho_measured_current_speed_mps": args.mado_current_speed_mps,
            "magnitude_source": (
                "2011 Mado-2ho excavation report current-meter table at the wreck site: "
                "10-day, 4x/day readings, 0.0247-0.2151 m/s range, ~0.10-0.13 m/s mean."
            ),
            "drift_scale": args.drift_scale,
            "applied_drift_speed_mps": applied_drift_speed,
            "max_drift_m": args.max_drift_m,
            "wiggle_amp_m": args.wiggle_amp_m,
            "yaw_amp_deg": args.yaw_amp_deg,
            "yaw_period_sec": args.yaw_period_sec,
            "note": "Kinematic weak drift/yaw proxy from nearby buoy current; not a full hydrodynamic force model.",
        },
        "survey_sampling": {
            "x_min_m": args.x_min,
            "x_max_m": args.x_max,
            "y_tracks_m": y_tracks,
            "rows_per_pass": args.rows_per_pass,
            "auto_rows_from_speed": bool(args.auto_rows_from_speed),
            "target_speed_mps": args.speed_mps,
            "sonar_hz": args.sonar_hz,
            "effective_row_spacing_m": effective_row_spacing_m,
            "effective_speed_mps": effective_speed_mps,
        },
        "mode": "visual_only" if args.visual_only else "sss_collection",
        "sensor": scenario["agents"][0]["sensors"][0].get("configuration"),
        "rows": int(waterfall.shape[0]) if waterfall is not None else 0,
        "cols": int(waterfall.shape[1]) if waterfall is not None else 0,
        "full_sss": str(full_path) if full_path is not None else None,
        "pass_montage": str(montage_path) if montage_path is not None else None,
        "range_gain_note": "Diagnostic TVG/EGN-style image only; raw SSS is preserved separately.",
    }
    (out / "03_environment" / "environment_and_sensor_summary.json").write_text(
        json.dumps(meta, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    if args.terrain_profile == "khoa":
        readme_intro = [
            "# KHOA environment Raycast survey v1",
            "",
            "This run combines three environment updates:",
            "",
            "1. KHOA smooth bathymetry terrain from the existing Taean-Mado depth grid.",
            "2. KIGAM/APL-UW sediment material mapping where the scene preset supports it.",
            "3. Nearby KHOA buoy observation applied to water density, sound speed, weak drift, and yaw wobble.",
        ]
    else:
        readme_intro = [
            "# Flat baseline Raycast survey v1",
            "",
            "This is an ablation baseline with the same survey path, objects, and Raycast SSS settings,",
            "but without KHOA bathymetry, KIGAM seabed material, or buoy water properties unless explicitly enabled.",
            "",
            "Use this output to compare against the environment-reflected run under controlled conditions.",
        ]

    (out / "README.md").write_text(
        "\n".join(
            readme_intro
            + [
                "",
                f"- Mode: {'visual-only viewport run' if args.visual_only else 'SSS collection run'}",
                f"- Terrain profile: `{args.terrain_profile}`",
                f"- Water profile: `{args.water_profile}`",
                "",
                f"- Full SSS: `{full_path.name if full_path is not None else 'not generated in visual-only mode'}`",
                f"- Pass montage: `{montage_path.name if montage_path is not None else 'not generated in visual-only mode'}`",
                "- Raw SSS array: `01_SSS/khoa_environment_raycast_survey_raw.npy`",
                "- Range-gain diagnostic: `01_SSS/*range_gain_diagnostic.png`",
                "- Pose/drift log: `02_logs/pose_trace_with_buoy_drift.csv`",
                "- Object contact table: `03_environment/khoa_object_ground_contact_table.csv`",
                "- Environment summary: `03_environment/environment_and_sensor_summary.json`",
                "",
                "The drift/yaw is a kinematic proxy, not a full hydrodynamic current model.",
            ]
        ),
        encoding="utf-8",
    )
    if full_path is not None:
        print(f"saved_full={full_path}")
    if montage_path is not None:
        print(f"saved_montage={montage_path}")
    print(f"saved_summary={out / '03_environment' / 'environment_and_sensor_summary.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
