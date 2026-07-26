# KHOA/KIGAM baseline Raycast survey

This is the default reproducible baseline for this repository.

It combines the following items:

1. KHOA smooth bathymetry terrain from the Taean-Mado depth grid.
2. KIGAM-based seabed material assignment for the local survey area.
3. Nearby KHOA buoy temperature/salinity converted into `WaterDensity` and `WaterSpeedSound`.
4. Six shipwreck proxy families and rock / gear-rope false-positive objects.
5. HoloOcean 2.4.0 `RaycastSidescanSonar` in `FlatUnderwater`.

Current drift is intentionally disabled in this baseline:

```text
drift_scale = 0.0
max_drift_m = 0.0
yaw_amp_deg = 0.0
```

The current direction/speed values are still stored in `environment_and_sensor_summary.json`, but they are not used to perturb the survey path in this default run.

- Full SSS: `khoa_environment_raycast_survey_full.png`
- Pass montage: `khoa_environment_raycast_survey_5pass_montage.png`
- Range-gain diagnostic: `khoa_environment_raycast_survey_full_range_gain_diagnostic.png`
- Object contact table: `03_environment/khoa_object_ground_contact_table.csv`
- Environment summary: `03_environment/environment_and_sensor_summary.json`

The range-gain diagnostic image is only a visualization aid. The normal SSS output is `khoa_environment_raycast_survey_full.png`.
