"""Derive a sourced ShipwreckProjectSeabed acoustic impedance from the KIGAM 1994
1:250,000 marine geology shapefiles (수치표층퇴적물유형분포도 / 수치표층퇴적물평균입도분포도),
intersected with the closed HoloOcean survey bbox, and patch materials.csv.

HoloOcean's RaycastSidescanSonar (HolodeckRaycastSonar.cpp::ComputeDetection) tags any
actor whose name contains ShipwreckProject_Seafloor / ShipwreckProject_SeabedProxy /
KhoaSmoothBathymetryTerrain / ShipwreckProject_SeabedFeature as material_type
"ShipwreckProjectSeabed", then looks up (density, speed_of_sound) for that key in
materials.csv and uses density*speed as the acoustic impedance for the Fresnel-style
reflection coefficient r = (z - z_water) / (z + z_water). This script replaces the
previously arbitrary ShipwreckProjectSeabed row with a value sourced from the actual
1994 KIGAM survey classification of our survey area.
"""
import json
import os
import re

import shapefile

WORKSPACE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RAW_DIR = os.path.join(
    WORKSPACE, "03_data", "kigam_marine_geology_shp_1994", "taean_seobu_20260725", "raw"
)
OUT_DIR = os.path.dirname(RAW_DIR)

# Same closed survey bbox used for the KHOA bathymetry / khoa_survey_scene_v1 preset
# (see 환경_반영_근거_정리.md section 3).
SURVEY_BBOX = dict(lat_min=36.685, lat_max=36.700, lon_min=126.125, lon_max=126.145)

# Verified from the primary free-access source: APL-UW TR 9407 "High-Frequency Ocean
# Environmental Acoustic Models Handbook" (Applied Physics Laboratory, University of
# Washington, 1994), Section IV.A.3 "Table for Model Parameters in Terms of Sediment
# Name", Table 2 (p. IV-6). DTIC accession ADB199453 (public, unclassified).
# https://apps.dtic.mil/sti/tr/pdf/ADB199453.pdf
# This table's sediment names/mean-grain-size (Mz, phi) bins follow the Wentworth/Lane/
# Inman/Shepard/Folk classification schemes and its ratios are the standard secondary
# compilation of Hamilton's (1980, JASA 68(5):1313-1340) geoacoustic sediment model.
# rho = ratio of sediment mass density to water mass density (Table 1 def., p. IV-4)
# nu  = ratio of sediment sound speed to water sound speed (Table 1 def., p. IV-4)
# Values below are (Mz_phi, density_ratio_rho, sound_speed_ratio_nu) transcribed directly
# from Table 2, spot-checked visually against the scanned page.
HAMILTON_TABLE2_RATIOS_BY_CLASS = {
    "Medium Sand":              (1.5, 1.845, 1.1782),
    "Fine Sand":                (2.5, 1.451, 1.1073),  # table row "Fine Sand, Silty Sand"
    "Muddy Sand":               (3.0, 1.339, 1.0800),
    "Very Fine Sand":           (3.5, 1.268, 1.0568),
    "Clayey Sand":              (4.0, 1.224, 1.0364),
    "Coarse Silt":              (4.5, 1.195, 1.0179),
    "Sandy Silt":               (5.0, 1.169, 0.9999),
}

# Reference bottom-water density/sound-speed this project already uses elsewhere for its
# RaycastSidescanSonar runs (see 환경_반영_근거_정리.md SSS sensor settings table /
# run_khoa_environment_raycast_survey_v1.py default_water_environment()).
REFERENCE_WATER_DENSITY_KG_M3 = 1024.0
REFERENCE_WATER_SPEED_M_S = 1480.0


def hamilton_density_speed(grain_class: str) -> tuple[float, float]:
    _, rho_ratio, nu_ratio = HAMILTON_TABLE2_RATIOS_BY_CLASS[grain_class]
    density = rho_ratio * REFERENCE_WATER_DENSITY_KG_M3
    speed = nu_ratio * REFERENCE_WATER_SPEED_M_S
    return round(density), round(speed)


def shape_bbox(shp):
    if getattr(shp, "bbox", None):
        return list(shp.bbox)
    xs = [p[0] for p in shp.points]
    ys = [p[1] for p in shp.points]
    return [min(xs), min(ys), max(xs), max(ys)]


def bbox_intersects(a, b):
    return not (a[2] < b["lon_min"] or a[0] > b["lon_max"] or a[3] < b["lat_min"] or a[1] > b["lat_max"])


def load_taean_intersecting(layer_name):
    shp_path = os.path.join(RAW_DIR, f"M_geology_{layer_name}_태안반도_서부.shp")
    sf = shapefile.Reader(shp_path, encoding="utf-8")
    fields = [f[0] for f in sf.fields if f[0] != "DeletionFlag"]
    hits = []
    for i in range(sf.numRecords):
        rec = dict(zip(fields, sf.record(i)))
        if "태안" not in str(rec.get("pkgname", "")):
            continue
        bbox = shape_bbox(sf.shape(i))
        if bbox_intersects(bbox, SURVEY_BBOX):
            hits.append({"bbox": bbox, "rec": rec})
    return hits


def patch_materials_csv(path, density, speed):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    pattern = re.compile(r"^ShipwreckProjectSeabed\s*,")
    replaced = False
    for idx, line in enumerate(lines):
        if pattern.match(line):
            # preserve this file's own "key, value, value" spacing style
            sep = ", " if ", " in line else ","
            lines[idx] = f"ShipwreckProjectSeabed{sep}{density}{sep}{speed}\n"
            replaced = True
            break
    if not replaced:
        raise RuntimeError(f"ShipwreckProjectSeabed row not found in {path}")

    with open(path, "w", encoding="utf-8", newline="") as f:
        f.writelines(lines)


def main():
    deposit_type_hits = load_taean_intersecting("deposits_type")
    mean_particle_hits = load_taean_intersecting("deposits_mean_particle")
    topography_hits = load_taean_intersecting("topography")

    assert deposit_type_hits, "no deposits_type polygon intersects survey bbox"
    assert mean_particle_hits, "no deposits_mean_particle polygon intersects survey bbox"

    deposit_code = deposit_type_hits[0]["rec"]["deposit"]
    grain_class = mean_particle_hits[0]["rec"]["dpname"]
    phi_range = mean_particle_hits[0]["rec"]["mptcl3"]

    if grain_class not in HAMILTON_TABLE2_RATIOS_BY_CLASS:
        raise RuntimeError(f"no APL-UW TR9407 Table 2 entry for grain class '{grain_class}'")
    mz_phi, rho_ratio, nu_ratio = HAMILTON_TABLE2_RATIOS_BY_CLASS[grain_class]
    density, speed = hamilton_density_speed(grain_class)
    impedance_mrayl = density * speed / 1.0e6

    materials_csv_paths = [
        os.path.join(
            WORKSPACE, "10_holoocean_sim", "holoocean_2_4_dev", "engine", "Content",
            "Config", "materials.csv",
        ),
        os.path.join(
            WORKSPACE, "10_holoocean_sim", "holoocean_2_4_official_runtime", "2.4.0",
            "worlds", "Ocean", "materials.csv",
        ),
    ]
    patched = []
    for p in materials_csv_paths:
        if os.path.exists(p):
            patch_materials_csv(p, density, speed)
            patched.append(p)

    summary = {
        "survey_bbox": SURVEY_BBOX,
        "kigam_source": "해저지질도_25만_태안반도서부해역_1994년발간 (KIGAM 지오빅데이터 오픈플랫폼, 활용신청 수령)",
        "deposits_type_folk_code": deposit_code,
        "deposits_mean_particle_class": grain_class,
        "deposits_mean_particle_phi_range": phi_range,
        "topography_contour_lines_near_bbox_m": sorted(
            {h["rec"]["wtdepth"] for h in topography_hits}
        ),
        "geoacoustic_source": (
            "APL-UW TR9407 High-Frequency Ocean Environmental Acoustic Models Handbook, "
            "Table 2 (p. IV-6), DTIC ADB199453, https://apps.dtic.mil/sti/tr/pdf/ADB199453.pdf "
            "-- standard secondary compilation of Hamilton (1980) geoacoustic sediment ratios. "
            "Literature class ratios applied to this project's own reference water values "
            f"({REFERENCE_WATER_DENSITY_KG_M3:.0f} kg/m^3, {REFERENCE_WATER_SPEED_M_S:.0f} m/s); "
            "not a site measurement."
        ),
        "hamilton_table2_mean_grain_size_phi": mz_phi,
        "hamilton_table2_density_ratio": rho_ratio,
        "hamilton_table2_sound_speed_ratio": nu_ratio,
        "reference_water_density_kg_m3": REFERENCE_WATER_DENSITY_KG_M3,
        "reference_water_speed_of_sound_m_s": REFERENCE_WATER_SPEED_M_S,
        "assigned_density_kg_m3": density,
        "assigned_speed_of_sound_m_s": speed,
        "assigned_impedance_MRayl": round(impedance_mrayl, 3),
        "previous_placeholder_density_kg_m3": 1700,
        "previous_placeholder_speed_of_sound_m_s": 1650,
        "previous_placeholder_impedance_MRayl": round(1700 * 1650 / 1.0e6, 3),
        "materials_csv_patched": patched,
    }

    with open(os.path.join(OUT_DIR, "seabed_material_derivation_summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
