# HoloOcean Shipwreck SSS Simulation

HoloOcean 2.4.0 `RaycastSidescanSonar`를 기준으로, 태안/마도 인근 해역의 수심과 해저 표층 재질을 반영한 난파선 SSS 데이터 취득 실험 코드입니다.

이 저장소의 목표는 다음 환경을 재현하는 것입니다.

- HoloOcean 2.4.0 `FlatUnderwater` world
- `RaycastSidescanSonar` 기반 양현 SSS waterfall 생성
- KHOA 수심자료 기반 smooth bathymetry terrain
- KIGAM 태안반도 서부해역 해저지질 자료 기반 seabed material
- APL-UW TR9407 / Hamilton 계열 지음향 표를 이용한 해저 재질 음향 물성값
- 6종 난파선 proxy와 rock, gear/rope 계열 혼동 객체
- 좁은 조사구역에서 lawnmower survey path 수행

## 먼저 알아둘 점

이 저장소는 HoloOcean 본체나 Unreal 빌드 결과물을 포함하지 않습니다.

완전 재현에는 다음 중 하나가 필요합니다.

1. HoloOcean 2.4.0 소스에 `10_holoocean_sim/patches` 아래 파일을 적용하고 Unreal에서 다시 빌드
2. 이미 같은 패치가 적용된 HoloOcean 2.4.0 runtime binary 사용

즉, HoloOcean 2.4.0 공식 runtime만 있고 C++ 패치가 적용되어 있지 않으면 `khoa_survey_scene_v1` 커스텀 씬은 생성되지 않습니다.

## 폴더 구조

```text
04_code/
  environment_data/
    run_khoa_environment_raycast_survey_v1.py
    derive_kigam_seabed_material_v1.py
    fetch_khoa_bathymetry.py
    make_khoa_location_map.py
  visualization/
    check_khoa_survey_ground_contact_24.py

03_data/
  khoa_bathymetry/
    taean_mado_excavation_reference_20260719/
  kigam_marine_geology_shp_1994/
    taean_seobu_20260725/
  khoa_observation_current_20260722/

10_holoocean_sim/
  patches/
    engine/

examples/
  khoa_kigam_baseline/
  current_stress_test/
```

## 반영한 환경 자료

### 1. 수심

KHOA/data.go.kr 자연과학용 수심정보 조회 API에서 태안/마도 인근 bbox의 수심점을 받아 사용했습니다.

포함 파일:

- `03_data/khoa_bathymetry/taean_mado_excavation_reference_20260719/khoa_bathymetry_metadata.json`
- `03_data/khoa_bathymetry/taean_mado_excavation_reference_20260719/khoa_bathymetry_local_points.csv`
- `03_data/khoa_bathymetry/taean_mado_excavation_reference_20260719/khoa_bathymetry_smooth_181x181_heightfield.csv`

HoloOcean에는 점 단위 수심을 그대로 넣지 않고, 보간된 smooth heightfield를 C++ terrain mesh로 생성해 `khoa_survey_scene_v1` 아래에 배치합니다.

### 2. 해저 재질

KIGAM 태안반도 서부해역 해저지질 자료에서 조사 bbox와 겹치는 표층퇴적물 정보를 확인했고, 현재 조사구역은 `Very Fine Sand`로 판정했습니다.

포함 파일:

- `03_data/kigam_marine_geology_shp_1994/taean_seobu_20260725/seabed_material_derivation_summary.json`
- `03_data/kigam_marine_geology_shp_1994/taean_seobu_20260725/해저재질_임피던스_근거표.png`

`Very Fine Sand`의 지음향 값은 APL-UW TR9407 *High-Frequency Ocean Environmental Acoustic Models Handbook* Table 2의 Hamilton 계열 sediment ratio를 사용했습니다.

사용값:

```text
sediment class: Very Fine Sand
density ratio rho: 1.268
sound speed ratio nu: 1.0568
reference water density: 1024 kg/m^3
reference water sound speed: 1480 m/s
assigned seabed density: 1298 kg/m^3
assigned seabed sound speed: 1564 m/s
assigned impedance: about 2.030 MRayl
```

이 값은 `materials.csv`의 `ShipwreckProjectSeabed` 행에 반영됩니다.

### 3. 수온/염분 기반 물성

KHOA 인근 부이 관측값에서 수온과 염분을 가져와, 실행 시 `WaterDensity`, `WaterSpeedSound`를 계산해 SSS 센서 설정에 넣습니다.

포함 파일:

- `03_data/khoa_observation_current_20260722/buoy_latest_taean_nearby_summary.json`
- `03_data/khoa_observation_current_20260722/nearest_buoys_to_taean_mado.json`

현재 코드는 수온/염분을 물성값으로 반영하지만, 유체역학 기반 조류 힘 모델은 아닙니다.

### 4. 유향/유속

유향/유속은 기본 baseline에서는 꺼져 있습니다.

이유는 부이에서 가져온 유속을 그대로 위치에 누적하면 AUV가 좁은 조사구역 밖으로 크게 밀려나기 때문입니다. 따라서 기본 실험은 수심/재질/물성/SSS geometry 검증용으로 안정화하고, 유향/유속은 `current_stress_test` 예제로 분리했습니다.

## HoloOcean 패치 적용

`10_holoocean_sim/patches` 아래 파일을 HoloOcean 2.4.0 소스의 같은 상대경로에 복사합니다.

주요 파일:

- `HolodeckGameMode.cpp`: KHOA terrain, 난파선 6종, 혼동 객체, 좁은 조사구역 scene preset 생성
- `ShipwreckKhoaSmoothTerrainData.generated.h`: KHOA smooth bathymetry heightfield
- `HolodeckRaycastSonar.cpp`: hit actor 이름/태그 기반으로 seabed/wreck/clutter material type 기록
- `RaycastSidescanSonar.cpp`: Raycast SSS intensity 계산
- `materials.csv`: `ShipwreckProjectSeabed`, `ShipwreckProjectWreck`, `ShipwreckProjectClutter` 물성값

패치 후 Unreal에서 HoloOcean을 다시 빌드해야 합니다.

## 실행 예시

아래 명령은 저장소 루트에서 실행합니다.

`--binary-path`에는 본인 PC의 HoloOcean 2.4.0 `Holodeck.exe` 경로를 넣어야 합니다.

```powershell
& '.venv_holoocean\Scripts\python.exe' `
  '04_code\environment_data\run_khoa_environment_raycast_survey_v1.py' `
  --output-dir '06_results\khoa_kigam_raycast_baseline' `
  --binary-path 'C:\path\to\HoloOcean\2.4.0\worlds\Ocean\Windows\Holodeck\Binaries\Win64\Holodeck.exe' `
  --scene-proxy khoa_survey_scene_v1 `
  --world FlatUnderwater `
  --speed-mps 2.0 `
  --auto-rows-from-speed `
  --drift-scale 0.0 `
  --max-drift-m 0.0 `
  --wiggle-amp-m 0.0 `
  --yaw-amp-deg 0.0
```

결과는 다음 위치에 저장됩니다.

```text
06_results/khoa_kigam_raycast_baseline/
  01_SSS/
    khoa_environment_raycast_survey_full.png
    khoa_environment_raycast_survey_5pass_montage.png
    khoa_environment_raycast_survey_full_range_gain_diagnostic.png
  02_logs/
    pose_trace_with_buoy_drift.csv
  03_environment/
    environment_and_sensor_summary.json
    khoa_object_ground_contact_table.csv
```

## 약한 조류 proxy 예시

부이 유향/유속을 약하게 반영해 경로 흔들림만 주려면 다음 옵션을 추가합니다.

```powershell
--drift-scale 0.15 `
--max-drift-m 8.0 `
--wiggle-amp-m 0.35 `
--yaw-amp-deg 2.0
```

이 방식은 실제 유체역학 모델이 아니라, 관측 유향/유속을 근거로 한 kinematic drift/yaw proxy입니다.

## 원유속 stress test

부이 유속을 거의 그대로 누적하면 AUV가 좁은 조사구역 밖으로 크게 밀려날 수 있습니다. 이 결과는 `examples/current_stress_test`에 따로 보관했습니다.

## 포함하지 않은 것

- HoloOcean/Unreal 전체 소스 및 빌드 결과
- API key
- 원본 KIGAM zip/shapefile 전체
- 대용량 raw `.npy` 결과

## 참고 출처

- HoloOcean 2.4.0 RaycastSidescanSonar source/API
- KHOA/data.go.kr 자연과학용 수심정보 조회 API
- KHOA 부이 최신 관측자료 API
- KIGAM 지오빅데이터 오픈플랫폼, 태안반도 서부해역 해저지질 자료
- Applied Physics Laboratory, University of Washington, *High-Frequency Ocean Environmental Acoustic Models Handbook*, TR9407, Table 2, DTIC ADB199453: https://apps.dtic.mil/sti/tr/pdf/ADB199453.pdf

