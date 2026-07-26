# Full-current stress test

This is not the default survey setting.

This run applies the nearby KHOA buoy current as a kinematic path drift without a controller:

```text
raw current speed = 0.95 m/s
drift_scale = 1.0
applied drift speed = 0.95 m/s
max_drift_m = 999.0
```

The result is kept as a stress-test because the AUV can drift far outside the narrow survey window if current is applied directly without path-following control.

- Full SSS: `khoa_environment_raycast_survey_full.png`
- Pass montage: `khoa_environment_raycast_survey_5pass_montage.png`
- Environment summary: `environment_and_sensor_summary.json`

This does not represent a controlled AUV survey. A realistic current experiment should add a path-following controller or compensation model before treating the output as final survey data.
