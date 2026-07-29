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
  baseline으로 깔고, 그 위에 보고서에 실측 좌표/구역 번호가 있는 9개 구역(서이상/동이상/18F/18H/18E/18G/19-B/19-C)에만
  뻘(SoftMud/ShellMud/HardMud/HardMudGravel) 재질을 blend합니다. 구역 경계는 완벽한 타원이 아니라
  domain-warp 노이즈로 불규칙하게 처리했고, 구역 내부에도 2-옥타브 텍스처 노이즈를 추가했습니다.
  좌표/재질 근거는 `HolodeckRaycastSonar.cpp`의 각 zone 정의 옆 주석에 보고서 좌표와 함께 남겨뒀습니다.
  - **구역 크기**: 보고서 원문("1개의 조사구역을 50×20m으로 설정하고 그리드를 설치")에 따라 18-F/18-H/18-E/18-G/
    19-B/19-C는 전부 50×20m 직사각형(타원 반경 25×10으로 근사)입니다. 서쪽 이상신호는 20×20m(반경 10×10),
    동쪽 이상신호는 추가 유물 발견으로 20×20m에서 30×30m로 확장(반경 15×15)됐다고 원문에 명시돼 있습니다.
    19-B와 19-C는 원문에 각각 별개의 50×20m 구역으로 나오는데 예전 코드는 이 둘을 하나의 타원으로 합쳐놨던
    오류가 있어 분리했습니다 (둘의 상대적 배치 자체는 원문에 없어 인접 배치로 근사).
  - **구역별 재질 임피던스**: 각 구역의 밀도/음속 값은 Hamilton(1980) 지음향 모델
    (`docs/presentation_assets/sources/APL_UW_TR9407_Table2_original_scan.png`, APL-UW TR9407 Table 2
    원본 스캔)에서 그 구역의 **표층**(SSS가 실제로 보는 깊이) 텍스처 묘사와 가장 가까운 입도(grain size, Mz phi)
    행을 찾아 매핑한 값입니다. 예를 들어 18-H는 원문에 "패각이 다수 함유된 무른 개흙"이라 18-F(패각 보통)보다
    한 단계 거친 Mz 5.0(Sandy Silt, Gravelly Mud) 행을 썼고, 서쪽 이상신호는 "자갈+강돌 섞인 단단한 개흙"이라
    Mz 1.0(Gravelly Muddy Sand) 행을 썼습니다. 어느 구역에 어느 표 행을 쓸지 고르는 것 자체는 판단이 들어가지만,
    표의 숫자 자체는 원본 스캔에서 그대로 옮긴 값이라 자체 추정치가 아닙니다. 정확한 행 선택 근거는
    `HolodeckRaycastSonar.cpp`의 `RaycastMadoReportTerrainImpedanceMaterialAtClientXY` 함수 주석에 구역별로
    남겨뒀습니다.
- **닻돌(anchor stone) 16기**: 보고서 유물 카탈로그의 실측 길이/폭/두께(cm)를 그대로 슬랩 지오메트리에 사용.
  배치 좌표 자체는 보고서에 없어 임의 배치이며, 이는 코드 주석에도 명시했습니다.
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
| 1 | `CTF_USE_COMPLEX_AS_SIMPLE` | ✅ 지형 mesh, 닻돌/reef rock slab mesh 모두 `bUseComplexAsSimpleCollision = true` 적용 |
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

닻돌/reef rock 등 개별 액터는 표준 흐름대로 액터 이름 매칭 → `materials.csv`의
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
| `ShipwreckProjectClutter` | 3000 | 5000 | **이 환경에서는 안 쓰임** — `HolodeckGameMode.cpp`가 같이 서빙하는 다른(구) scene preset의 gear/rope, rock mound 액터용. `mado_report_environment_v1` 코드 경로에서는 도달 불가 |
| `ShipwreckProjectWreck` | 1100 | 1983 | **이 환경에서는 안 쓰임** — 같은 이유. 난파선 액터를 추가하면 그 이름 매칭 규칙(`SurveyWreck`/`TorpedoMesh_*`)이 그대로 적용됨 |
| `ShipwreckProjectSoftMud`/`ShellMud`/`HardMud`/`HardMudGravel` | — | — | 지형 블렌딩에서만 쓰이는 값이며 C++ 상수로 직접 박혀 있음. CSV의 4행은 대표값 요약이고, 실제 코드는 구역마다 별도 상수(`SoftMudZ`, `ShellMud18FZ`, `ShellMud18HZ`, `DisturbedShellMud18EZ`, `HardMudPlainZ`, `HardMud19BCZ`, `HardMudGravelWestZ`)를 씀 — 아래 "구역별 재질 임피던스" 참고. CSV 행 자체는 조회되지 않음 |

## 알려진 제약 (인수인계 시 바로 알아야 할 것)

1. **난파선 액터가 없습니다.** 이 환경은 해저 지형/재질/닻돌 구성만 담당합니다. GT 마스크가 필요하면
   별도로 `wreck` 태그를 가진 액터를 배치해야 합니다.
2. **지형이 평탄하지 않습니다.** SSS 쪽 slant→ground 보정 로직이 단일 고도를 가정한다면 사전 협의가 필요합니다
   (SCENE_INTEGRATION_GUIDE.md §4.3).
3. **`--azimuth-ray-count`를 충분히 크게 주지 않으면 동심원 형태의 가짜 무늬(aliasing)가 생깁니다.**
   `RaycastSidescanSonar.cpp`(stock, 이 환경에서 수정하지 않음)의 histogram은 slant range 기준 균등 bin인데
   azimuth ray는 각도 기준 균등 간격이라, 특정 각도 구간에서 bin당 샘플 수가 부족하면 노이즈만 채워집니다.
   `run_khoa_environment_raycast_survey_v1.py`는 이제 `--azimuth-ray-count`를 지정하지 않으면
   `recommended_azimuth_ray_count()`가 자동으로 안전값을 계산합니다. 근거·검증 데이터는
   `SCENE_INTEGRATION_GUIDE.md` §4.5 참고.
4. 지형/재질은 `HOLOOCEAN_SHIPWRECK_SCENE_PRESET=mado_report_environment_v1` 환경변수로 활성화됩니다.
   (`HOLOOCEAN_SHIPWRECK_ENABLE_HARD_FACIES_ACOUSTIC_MAP=1`을 추가로 주면 blend 대신 binary/hard 구역 분류로
   전환할 수 있으나, 기본값은 꺼져 있고 권장 경로는 blend 방식입니다.)
5. **후방산란 각도 모델은 단순 `R^2 cos(θ)` (Lambertian 형태이며, 저(低) grazing 각도에서 실제 해저처럼
   0이 아닌 바닥값(floor)을 갖지 않고 0으로 수렴합니다.** 평탄·단일재질 테스트 씬(altitude 4.7m,
   `RangeBins=1000`, `RangeMax=50m`, Very Fine Sand ρ=1298/c=1564)으로 직접 검증한 결과, 시뮬레이터 raw
   출력은 이 공식과 표본이 안정적인 구간(nadir 기준 2°-73°)에서 **상관계수 1.0000, RMSE 0.0006**로 사실상
   완전히 일치합니다 — 구현 자체는 수식대로 정확히 동작함을 확인. 다만 실제 문헌(Jackson & Richardson류
   high-frequency bottom scattering 측정)의 grazing-angle 응답은 낮은 grazing 각도에서 완만한 상수 바닥값을
   유지하는 경우가 많아, 우리 모델의 저(低) grazing 영역 형태는 실측과 정성적으로 다릅니다 (알려진 단순화이며,
   TVG 정규화가 range 의존 형태를 흡수하므로 실제 워터폴 이미지에는 거의 영향이 없다고 판단해 미구현 상태로
   유지). nadir 기준 73° 이상(=grazing 17° 이하)에서는 위 3번과 같은 원인(해당 config 기준 bin당 표본 부족)
   으로 raw 값이 불안정해집니다. 검증 그래프:
   `docs/presentation_assets/sources/backscatter_angle_validation_flat_single_material_v1.png`

## 엔진 패치 적용

`patches/engine` 아래 파일을 HoloOcean 2.4.0 소스의 같은 상대경로에 덮어쓰고 다시 빌드합니다.

- `Source/Holodeck/HolodeckCore/Private/HolodeckGameMode.cpp` — 지형/닻돌/reef cue 스폰
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

이 스크립트는 환경이 의도대로 스폰됐는지 확인하는 용도의 raw raycast 캡처만 담당합니다. TVG 정규화,
log-scale 표시, speckle noise 같은 SSS 디스플레이 후처리는 이 저장소의 범위가 아니며 SSS 취득 파이프라인
쪽에서 처리합니다.
