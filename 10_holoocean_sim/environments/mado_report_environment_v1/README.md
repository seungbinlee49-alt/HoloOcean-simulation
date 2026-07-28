# Environment: `mado_report_environment_v1`

HoloOcean 2.4.0 `FlatUnderwater` world 위에 만든 100 x 120m 규모의 태안 마도해역 조사구역 환경입니다.
2021년 문화재청 마도해역 시굴조사 보고서(`03_data/taean_mado_2021_report_analysis`)에 기재된 구역/좌표/재질
기술을 근거로 해저 재질 분포를 반영했고, KHOA 실측 수심을 보간한 기복 지형을 사용합니다.

이전에 이 저장소에 있던 `khoa_survey_scene_v1`(좁은 구역, 단일 재질 Very Fine Sand, 임의 배치 난파선 6종)은
완전히 대체되었습니다. 이 환경에는 **현재 난파선 액터가 없습니다** (아래 "알려진 제약" 참고).

## 이 환경이 반영한 것

- **지형**: KHOA 실측 수심점(원 해상도 ~150m 간격)을 보간·평활화한 401x351 heightfield.
  `10_holoocean_sim/environments/mado_report_environment_v1/patches/engine/Source/Holodeck/HolodeckCore/Private/ShipwreckKhoaSmoothTerrainData.generated.h`.
  더 촘촘한 그리드는 렌더링을 매끄럽게 할 뿐, 실측보다 더 정밀한 정보를 의미하지 않습니다.
  `04_code/environment_data/generate_khoa_smooth_terrain_header_v2.py`로 재생성할 수 있습니다
  (입력은 `03_data/khoa_bathymetry/.../khoa_bathymetry_smooth_181x181_heightfield.csv`를 X∈[-150,110],
  Y∈[-120,110] 범위로 자른 서브셋, `--grid-x 401 --grid-y 351 --smooth-sigma-cells 3.5 --smooth-passes 5`).
- **해저 재질**: 조사구역 전체는 KIGAM 표층퇴적물 판정 기반 `Very Fine Sand`(density 1298 kg/m^3, sound speed
  1564 m/s, APL-UW TR9407 Table 2 Hamilton ratio 사용, `03_data/kigam_marine_geology_shp_1994` 참고)를
  baseline으로 깔고, 그 위에 보고서에 실측 좌표/구역 번호가 있는 9개 구역(서이상/동이상/18F/18H/19-B/C/18E 등)에만
  뻘(SoftMud/ShellMud/HardMud/HardMudGravel) 재질을 blend합니다. 구역 경계는 완벽한 타원이 아니라
  domain-warp 노이즈로 불규칙하게 처리했고, 구역 내부에도 2-옥타브 텍스처 노이즈를 추가했습니다.
  좌표/재질 근거는 `HolodeckRaycastSonar.cpp`의 각 zone 정의 옆 주석에 보고서 좌표와 함께 남겨뒀습니다.
- **닻돌(anchor stone) 16기**: 보고서 유물 카탈로그의 실측 길이/폭/두께(cm)를 그대로 슬랩 지오메트리에 사용.
  배치 좌표 자체는 보고서에 없어 임의 배치이며, 이는 코드 주석에도 명시했습니다.
- **패각/자갈/할석/강돌 산란체 78개**: 크기·개수는 절차적 생성(golden-angle scatter)이며 실측 근거는 없습니다.
- **해류**: Mado-2호선(2011) 보고서의 실측 유속표 범위(0.0247~0.2151 m/s)를 참고해 0.115 m/s를 기본값으로 사용.
- **수온/염분/음속/밀도**: Mado-4호선 20일 CTD 평균 실측값(`--water-profile mado_ctd`, 기본값)을 사용.
  T=20.35C, S=29.95psu, rho=1020.87 kg/m^3.

## 좌표/범위

```json
"env_min": [-143.21, -112.57, -12.0],
"env_max": [107.17, 108.20, 5.0]
```

지형 X/Y 실측 범위는 `ShipwreckKhoaSmoothTerrainData::XMinM/XMaxM/YMinM/YMaxM`과 정확히 일치합니다.
이 범위를 벗어나는 조사 경로는 지형 밖으로 나가 빈(검은) 데이터가 나옵니다 (SCENE_INTEGRATION_GUIDE.md §4.2).

## SCENE_INTEGRATION_GUIDE.md 체크리스트 대비 현황

| # | 항목 | 상태 |
| --- | --- | --- |
| 1 | `CTF_USE_COMPLEX_AS_SIMPLE` | ✅ 지형 mesh, 닻돌/산란체 slab mesh 모두 `bUseComplexAsSimpleCollision = true` 적용 |
| 2 | `materials.csv` 등록 | ⚠️ 아래 "재질 조회 방식" 참고 — 일반 룰과 다름 |
| 3 | 재질 애셋 이름 일관성 | N/A — 지형은 UE Material 애셋을 쓰지 않음 (아래 참고) |
| 4 | `wreck` 태그 | ❌ **미충족 — 이 환경에는 난파선 액터가 없습니다.** GT 마스크를 이 환경으로 뽑으면 100% 빈 마스크가 정상입니다. |
| 5 | 지형 기복 사전 고지 | ⚠️ **이 환경은 평탄하지 않습니다.** 최대 고저차는 depth 8.45~9.42m 범위(약 1m 기복). SSS 쪽 slant→ground 보정이 단일 고도를 가정한다면 오차가 생깁니다. |
| 6 | `env_min`/`env_max` | ✅ 위 값 그대로 config.json에 반영하면 됩니다 |
| 7 | 엔진 소스 SSS 패치 | ⚠️ 아래 "역할 분담" 참고 |

### 재질 조회 방식이 일반 규약과 다른 이유

가이드 §3의 표준 흐름은 `Material->GetName()` (UE Material 애셋 이름)을 `materials.csv`에서 조회합니다.
이 환경의 **지형(seabed)만은 이 흐름을 타지 않습니다.** `HolodeckRaycastSonar.cpp::ComputeDetection`이
지형 히트를 감지하면 `RaycastMadoReportTerrainImpedanceMaterialAtClientXY(X, Y, GroundRangeM)`를 직접 호출해서
좌표 기반으로 블렌딩된 임피던스를 계산하고, 그 값을 `"ShipwreckProjectImpedance_<정수>"` 형태의 문자열로
합성해 `Detection.material_type`에 바로 넣습니다. `GetImpedanceFromMap()`은 이 접두사를 만나면 문자열에서
숫자를 직접 파싱해서 `materials.csv` 조회를 건너뜁니다 (`HolodeckRaycastSonar.cpp` 참고).

닻돌/산란체/reef rock 등 개별 액터는 표준 흐름대로 액터 이름 매칭 → `materials.csv`의
`ShipwreckProjectAnchorStone`, `ShipwreckProjectReefRock` 등을 조회합니다. 이 부분은 가이드와 동일합니다.

`materials.csv`에는 이 표에 없는 `M_Landscape`, `M_URockA`, `M_PreviewOceanWater` 같은 행도 섞여 있습니다.
이 환경 코드가 만든 게 아니라, `FlatUnderwater` 월드의 기본 애셋을 처음 실행할 때 HoloOcean이 자동 등록한
행입니다 (§3.2 auto-discovery 동작). 이 환경과는 무관하니 무시하면 됩니다.

### 지형이 아닌 개별 재질 목록 (`materials.csv`)

| 이름 | 밀도 kg/m^3 | 음속 m/s | 용도 |
| --- | --- | --- | --- |
| `ShipwreckProjectSeabed` | 1298 | 1564 | 재질 판정 실패 시 fallback (정상 동작에서는 거의 안 씀) |
| `ShipwreckProjectAnchorStone` | 2600 | 3800 | 닻돌 16기 |
| `ShipwreckProjectReefRock` | 2700 | 4500 | reef edge cue 4개 |
| `ShipwreckProjectClutter` | 3000 | 5000 | gear/rope, rock mound류 혼동 객체 |
| `ShipwreckProjectWreck` | 1100 | 1983 | **현재 씬에 해당 액터가 없어 실제로는 안 쓰임** — 난파선 추가 시를 대비한 값 |
| `ShipwreckProjectSoftMud`/`ShellMud`/`HardMud`/`HardMudGravel` | — | — | 지형 블렌딩에서만 쓰이는 값이며 C++ 상수로 직접 박혀 있음 (`HolodeckRaycastSonar.cpp` 내 `SoftMudZ` 등). CSV 행은 참고용으로만 남겨뒀고, 실제로는 조회되지 않음 |

## 알려진 제약 (인수인계 시 바로 알아야 할 것)

1. **난파선 액터가 없습니다.** 이 환경은 해저 지형/재질/닻돌/산란체 구성만 담당합니다. GT 마스크가 필요하면
   별도로 `wreck` 태그를 가진 액터를 배치해야 합니다.
2. **지형이 평탄하지 않습니다.** SSS 쪽 slant→ground 보정 로직이 단일 고도를 가정한다면 사전 협의가 필요합니다
   (SCENE_INTEGRATION_GUIDE.md §4.3).
3. **`--azimuth-ray-count`를 충분히 크게 주지 않으면 nadir 근처에 동심원 형태의 가짜 무늬(aliasing)가 생깁니다.**
   `RaycastSidescanSonar.cpp`(stock, 이 환경에서 수정하지 않음)의 histogram은 slant range 기준 균등 bin인데
   azimuth ray는 각도 기준 균등 간격이라, nadir 근처 bin 밀도가 부족하면 일부 bin이 비어 노이즈만 채워집니다.
   `RangeBins=1000`, `RangeMax=60m` 기준으로 실측 검증 결과 **`--azimuth-ray-count` 34000 이상**을 권장합니다
   (기존 8500에서는 뚜렷한 동심원 아티팩트가 나타났고, 34000에서는 사라졌습니다). Range bin 수/범위를 바꾸면
   이 권장값도 다시 계산해야 합니다.
4. 지형/재질은 `HOLOOCEAN_SHIPWRECK_SCENE_PRESET=mado_report_environment_v1` 환경변수로 활성화됩니다.
   (`HOLOOCEAN_SHIPWRECK_ENABLE_HARD_FACIES_ACOUSTIC_MAP=1`을 추가로 주면 blend 대신 binary/hard 구역 분류로
   전환할 수 있으나, 기본값은 꺼져 있고 권장 경로는 blend 방식입니다.)

## 엔진 패치 적용

`patches/engine` 아래 파일을 HoloOcean 2.4.0 소스의 같은 상대경로에 덮어쓰고 다시 빌드합니다.

- `Source/Holodeck/HolodeckCore/Private/HolodeckGameMode.cpp` — 지형/닻돌/산란체/reef cue 스폰
- `Source/Holodeck/HolodeckCore/Private/HolodeckRaycastSonar.cpp` — 재질/임피던스 판정 (base raycast sensor 공용 클래스)
- `Source/Holodeck/HolodeckCore/Private/ShipwreckKhoaSmoothTerrainData.generated.h` — KHOA heightfield 데이터
- `Content/Config/materials.csv` — 위 표의 재질 물성

**포함하지 않은 것**: `RaycastSidescanSonar.cpp/.h`, `Octree.cpp`. 이 파일들은 SSS 취득(양측 출력/빔 지향성/GT
마스크) 담당 쪽에서 별도로 관리합니다. 이 환경 저장소는 stock 버전을 그대로 씁니다 (아래 "역할 분담" 참고).

## 역할 분담

이 저장소는 **환경/씬 제작**만 담당합니다. SSS 취득 파이프라인(양측 히스토그램, 빔 지향성, GT 마스크 생성)은
별도 담당자가 관리하며, `SCENE_INTEGRATION_GUIDE.md` §5.2에 정리된 파일들(`RaycastSidescanSonar.cpp/.h`,
`Octree.cpp`, `client/src/holoocean/sensors.py`)을 가져와 본인 엔진 소스 트리에 이 환경의 패치와 함께 적용해야
완전한 파이프라인이 됩니다.

## 실행 예시

```powershell
& '.venv_holoocean\Scripts\python.exe' `
  '04_code\environment_data\run_khoa_environment_raycast_survey_v1.py' `
  --output-dir '06_results\mado_report_environment_v1' `
  --binary-path 'C:\path\to\HoloOcean\2.4.0\worlds\Ocean\Windows\Holodeck\Binaries\Win64\Holodeck.exe' `
  --scene-proxy mado_report_environment_v1 `
  --terrain-profile khoa `
  --water-profile mado_ctd `
  --x-min -60 --x-max 60 `
  --y-tracks="-48,-32,-16,0,16,32,48" `
  --rows-per-pass 600 `
  --sonar-hz 10 --range-bins 1000 --range-min 0.5 --range-max 60.0 `
  --azimuth 85.0 --elevation 0.25 `
  --azimuth-ray-count 34000 --elevation-ray-count 4
```

`HOLOOCEAN_SHIPWRECK_SCENE_PRESET=mado_report_environment_v1` 환경변수를 실행 전에 반드시 설정해야 합니다
(PowerShell: `$env:HOLOOCEAN_SHIPWRECK_SCENE_PRESET = "mado_report_environment_v1"`).

원시 raycast 출력은 TVG(range-gain) 정규화와 log-scale 표시를 거쳐야 실제 SSS처럼 보입니다. 예시:

```powershell
& '.venv_holoocean\Scripts\python.exe' `
  '04_code\visualization\apply_tvg_normalization_v1.py' `
  --input-npy '06_results\mado_report_environment_v1\01_SSS\khoa_environment_raycast_survey_raw.npy' `
  --output-dir '06_results\mado_report_environment_v1_tvg' `
  --rows-per-pass 600 --smooth-window 9 --speckle-mult-sigma 0.12
```

결과 예시는 `examples/mado_report_environment_v1/`에 있습니다.
