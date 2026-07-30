# Environment: `mado_report_environment_v1`

HoloOcean 2.4.0 `FlatUnderwater` world 위에 만든 100 x 120m 규모의 태안 마도해역 조사구역 환경입니다.
2021년 문화재청 마도해역 시굴조사 보고서(`03_data/taean_mado_2021_report_analysis`)에 기재된 구역/좌표/재질
기술을 근거로 해저 재질 분포를 반영했고, KHOA 실측 수심을 보간한 기복 지형을 사용합니다.

이전에 이 저장소에 있던 `khoa_survey_scene_v1`(좁은 구역, 단일 재질 Very Fine Sand, 임의 배치 난파선 6종)은
완전히 대체되었습니다. 이 환경에는 **현재 난파선 액터가 없습니다** (아래 "알려진 제약" 참고).

## 이 환경이 반영한 것

- **지형**: KHOA 실측 수심점(원 해상도 ~150m 간격)을 보간·평활화한 401x351 heightfield.
  `Content/Config/mado_terrain/mado_report_environment_v1_terrain.csv`에서 런타임에 로드합니다
  (`MadoSceneConfig.h/.cpp`의 `FMadoTerrainData`/`GetActiveMadoTerrainData()`) — 예전에는
  `ShipwreckKhoaSmoothTerrainData.generated.h`로 컴파일되어 있어 지형을 바꾸려면 리빌드가 필요했지만,
  이제 씬 JSON의 `terrain_data_source` 필드가 어떤 CSV를 쓸지 지정하므로 새 사이트를 추가할 때도 리빌드가
  필요 없습니다 (아래 "씬 변형" 참고). 더 촘촘한 그리드는 렌더링을 매끄럽게 할 뿐, 실측보다 더 정밀한
  정보를 의미하지 않습니다. `04_code/environment_data/generate_khoa_smooth_terrain_header_v2.py`의
  `--out-smoothed-csv` 출력으로 재생성할 수 있습니다 (입력은
  `03_data/khoa_bathymetry/.../khoa_bathymetry_smooth_181x181_heightfield.csv`를 X∈[-150,110],
  Y∈[-120,110] 범위로 자른 서브셋, `--grid-x 401 --grid-y 351 --smooth-sigma-cells 3.5 --smooth-passes 5`;
  git에는 용량을 줄이기 위해 `seabed_z_m` 컬럼을 빼고 좌표/깊이를 소수점 6자리로 반올림한 버전을 커밋했습니다
  — 재생성 시 완전 동일 값은 아니지만 float32 유효자리 이내 차이라 결과에 실질적 영향 없음을 직접
  검증했습니다, corr=0.9999995).
- **해저 재질**: 조사구역 전체는 KIGAM 표층퇴적물 판정 기반 `Very Fine Sand`(density 1298 kg/m^3, sound speed
  1564 m/s, APL-UW TR9407 Table 2 Hamilton ratio 사용, `03_data/kigam_marine_geology_shp_1994` 참고)를
  baseline으로 깔고, 그 위에 보고서에 실측 좌표/구역 번호가 있는 8개 구역(서이상/동이상/18F/18H/18E/18G/19-B/19-C)에만
  뻘(SoftMud/ShellMud/HardMud/HardMudGravel) 재질을 blend합니다. 구역 경계는 완벽한 타원이 아니라
  domain-warp 노이즈로 불규칙하게 처리했고, 구역 내부에도 2-옥타브 텍스처 노이즈를 추가했습니다.
  좌표/재질 근거는 `Content/Config/mado_scenes/mado_report_environment_v1.json`의 각 `facies_zones[].evidence`
  필드에 보고서 좌표와 함께 남겨뒀습니다 (config-driven으로 바뀌면서 C++ 주석이 아니라 JSON으로 이동).
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
    표의 숫자 자체는 원본 스캔에서 그대로 옮긴 값이라 자체 추정치가 아닙니다. 정확한 행 선택 근거는 위와 같이
    `mado_report_environment_v1.json`의 `facies_zones[].evidence` 필드에 구역별로 남겨뒀습니다. 이 값을
    좌표 기반 임피던스로 계산하는 코드는 `patches/FIELD_IMPEDANCE_HOOK.md`에 있는
    `GetFieldImpedanceAtLocation()` 구현(예전 이름 `RaycastMadoReportTerrainImpedanceMaterialAtClientXY`)입니다.
- **닻돌(anchor stone) 16기**: 보고서 유물 카탈로그의 실측 길이/폭/두께(cm)를 그대로 슬랩 지오메트리에 사용.
  배치 좌표 자체는 보고서에 없어 임의 배치이며, 이는 코드 주석에도 명시했습니다.
- **해류**: Mado-2호선(2011) 보고서의 실측 유속표 범위(0.0247~0.2151 m/s)를 참고해 0.115 m/s를 기본값으로 사용.
- **수온/염분/음속/밀도**: Mado-4호선 20일 CTD 평균 실측값(`--water-profile mado_ctd`, 기본값)을 사용.
  T=20.35C, S=29.95psu, rho=1020.87 kg/m^3.

## 씬 변형 (config-driven variants)

facies zone/닻돌/reef cue/wreck 스폰은 전부 C++ 하드코딩이 아니라
`Content/Config/mado_scenes/*.json`에서 런타임에 읽어옵니다 (`MadoSceneConfig.h/.cpp`). 새 변형을
만들 때 엔진을 다시 빌드할 필요 없이 JSON만 추가하면 됩니다.
`HOLOOCEAN_SHIPWRECK_SCENE_PRESET`에 `.json`으로 끝나는 값을 주면 그 파일을(상대경로면
`Content/Config/mado_scenes/` 기준) 직접 로드하고, `mado_report_environment_v1`처럼 알려진 이름을 주면
번들된 기본 JSON으로 매핑됩니다.

- **`mado_report_environment_v1.json`**: 위에서 설명한 기본 씬 (보고서 근거 재질/구역/닻돌). reef cue는
  근거 없어서 삭제했습니다 (`reef_edge_cues: []` — 아래 "알려진 제약" 6번 참고).
- **`mado_report_environment_v1_heldout_material_v1.json`**: 지형·구역·닻돌 배치는 기본 씬과
  완전히 동일하고, **전역 baseline 재질만** 다른 실제 Hamilton 행(Mz 1.5 Medium Sand / Mz 5.0 Sandy Silt,
  Gravelly Mud)으로 교체한 held-out 테스트용 변형입니다. 다운스트림 탐지 파이프라인이 기본 씬의 특정
  재질에만 과적합되지 않았는지 확인하는 용도이며, **실제 마도 현장의 재질 구성에 대한 주장이 아닙니다**
  (JSON의 최상위 `comment` 필드에 명시). 실측 검증: 기본 씬(Very Fine Sand) 대비 raw 신호 평균이 약 31%
  더 밝게 나옴 — 임피던스가 더 높은 재질이므로 물리적으로 타당한 방향.
  - **모래파/사퇴(sand wave/ridge) 지형 타일은 넣지 않았습니다.** 2021년 시굴조사 보고서 전문과 KIGAM
    해저지질 자료 어디에도 이 현장 주변의 수중 모래파/사퇴/리플 지형에 대한 언급이 없습니다 (보고서에
    나오는 유일한 "사구" 관련 서술은 태안 육상 해안사구 토양이며, 수중 지형과 무관). 근거가 없어 추가하지
    않았습니다.

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

### 재질 조회 방식 (2026-07-30, 태그 기반으로 전환)

`holoocean_ws/docs/FIELD_IMPLEMENTATION_GUIDE.md` §4.1/4.2 인터페이스로 전환했습니다 — 예전에는
액터 **이름 문자열**(`ActorName.Contains(TEXT("KhoaSmoothBathymetryTerrain"))` 등)로 판정하고,
지형 임피던스는 `"ShipwreckProjectImpedance_<정수>"` 형태로 문자열에 인코딩해서 넘겼습니다. 이 방식은
오브젝트 종류가 늘 때마다 C++ 수정 + 전체 리빌드가 필요했고, `Contains()`가 부분일치라 오탐 위험이
있었고, 문자열 왕복 때문에 레이당(핑당 최대 수만 회) 비용과 정밀도 손실이 있었습니다.

지금은 **액터 태그**로 판정합니다.

| 태그 | 붙는 곳 | 의미 |
| --- | --- | --- |
| `sssfacies` | 지형 액터 (`SpawnShipwreckProjectKhoaSmoothTerrainMesh`) | 좌표 기반 연속 필드 — `GetFieldImpedanceAtLocation()` 호출 |
| `sssmat:ShipwreckProjectAnchorStone` | 닻돌 16기 | `materials.csv`에서 이름으로 직접 조회 |
| `sssmat:ShipwreckProjectReefRock` | reef edge cue (현재 데이터 없음) | 〃 |
| `sssmat:ShipwreckProjectWreck` + `wreck` | 난파선 파츠 (현재 스폰 안 됨) | 재질 태그 + GT 라벨 태그, 둘 다 별도 |

`GetFieldImpedanceAtLocation(ClientHitPoint, GroundRangeM)`는 `HolodeckRaycastSonar.h`에 선언된
가상함수이고(기본 구현 `-1.0f` = "필드 없음, 표준 조회로 폴백"), 이 환경의 실제 구현(존 블렌드 +
2-옥타브 텍스처 노이즈 + 나디르 페이드, 예전 `RaycastMadoReportTerrainImpedanceMaterialAtClientXY`와
수식은 동일하고 반환 타입만 FString→float)은 `patches/FIELD_IMPEDANCE_HOOK.md`에 있습니다. 취득 측의
`sss-scene-runtime.diff`가 이 파일을 이미 수정하므로, 저희 쪽 diff(`patches/engine.diff`)와 충돌하지
않도록 이 한 군데만 diff 대신 적용 지침 문서로 드립니다 (아래 "엔진 패치 적용" 참고).

**예전 액터-이름 매칭 코드는 로컬 dev workspace에는 fallback으로 남아있지만(다른 미검증 테스트 씬이
의존할 수 있어 보수적으로 삭제하지 않음), 태그가 항상 먼저 검사되므로 이 환경은 실질적으로 태그 경로만
탑니다.**

`materials.csv`에는 이 표에 없는 `M_Landscape`, `M_URockA`, `M_PreviewOceanWater` 같은 행도 섞여 있습니다.
이 환경 코드가 만든 게 아니라, `FlatUnderwater` 월드의 기본 애셋을 처음 실행할 때 HoloOcean이 자동 등록한
행입니다 (§3.2 auto-discovery 동작). 이 환경과는 무관하니 무시하면 됩니다.

### 지형이 아닌 개별 재질 목록 (`materials.csv`)

| 이름 | 밀도 kg/m^3 | 음속 m/s | 용도 |
| --- | --- | --- | --- |
| `ShipwreckProjectSeabed` | 1298 | 1564 | 재질 판정 실패 시 fallback (정상 동작에서는 거의 안 씀) |
| `ShipwreckProjectAnchorStone` | 2560 | 3700 | 닻돌 16기. **2026-07-30 수정**: 예전엔 저장소에 이미 있던 범용 `M_CobbleStone_Rough`(2600/3800) 값을 근거 없이 그대로 복사해서 썼음. 지금은 APL-UW TR9407 Table 2의 실제 "Rock" 행(밀도비 2.50, 음속비 2.50 × 기준 물성 1024/1480)으로 교체함. 다만 이건 암석 일반값이고, JSON `anchor_stones[].rock_type`에 이미 기록된 화강암/편암/응회암/사암 각각을 음향적으로 구분하진 않음 — Hamilton 표 자체가 미고결 퇴적물만 다루고 고결암 세부 종류는 없어서, 더 정밀하게 하려면 암석종별 실측 문헌치를 별도로 찾아야 함 |
| `ShipwreckProjectReefRock` | 2560 | 3700 | **현재 안 쓰임** — reef edge cue가 근거 없어 삭제됨 (아래 "알려진 제약" 6번). 값은 위 AnchorStone과 같은 근거(Hamilton Table 2 "Rock" 행)로 맞춰둠. `sssmat:ShipwreckProjectReefRock` 태그 부착 코드는 이미 있어서, JSON에 근거 있는 reef cue를 다시 채워 넣기만 하면 바로 살아남 |
| `ShipwreckProjectClutter` | 3000 | 5000 | **이 환경에서는 안 쓰임.** `HolodeckGameMode.cpp`의 `SpawnShipwreckProjectFlatUnderwaterScene` 안, `HOLOOCEAN_SHIPWRECK_SCENE_PRESET`이 아무 것도 안 가리킬 때만 실행되는 완전히 별개의 구식 폴백 데모 씬(GearRope/RockMound 액터)용. 마도 프로젝트와 무관하고, 이 씬이 활성화되면 `mado_report_environment_v1` 코드 경로가 먼저 return 되므로 도달 자체가 안 됨. 값 근거는 추적 안 됨(마도 작업 이전부터 있던 값) — 마도 프로젝트 소관이 아니라서 그대로 둠 |
| `ShipwreckProjectWreck` | 1100 | 1983 | **이 환경에서는 안 쓰임.** 위와 같은 구식 폴백 데모 씬(`SurveyWreck_Intact_A` 등)용, 마도 프로젝트와 무관. `wreck_spawns`가 두 마도 환경 다 비어있어서(근거 있는 형태가 아니라 미스폰) 어차피 안 쓰임 — 나중에 근거 있는 wreck_spawns 항목을 채우면 `SpawnShipwreckProjectMadoWrecks`가 `sssmat:ShipwreckProjectWreck` + `wreck` 태그를 자동으로 붙이므로 이 행이 그때 다시 살아남 |

**2026-07-30 삭제**: `ShipwreckProjectSoftMud`/`ShellMud`/`HardMud`/`HardMudGravel` 4행. 어떤 코드 경로도 이 이름들을 조회하지 않는 완전한 죽은 데이터였음 (facies zone 재질은 처음부터 `Content/Config/mado_scenes/*.json`의 `facies_zones[].target_material`에서 직접 읽음, 위 "씬 변형" 참고). 대표값 요약이라는 명목으로 남아있었지만 실제로 참조되는 곳이 없어 정리함.

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
   (예전에 있던 `HOLOOCEAN_SHIPWRECK_ENABLE_HARD_FACIES_ACOUSTIC_MAP` binary/hard 분류 토글은 실제로 쓰인 적이
   없는 죽은 코드로 확인되어 삭제했습니다 — 이제 blend 경로만 존재합니다.)
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
6. **reef edge cue는 근거 없어서 삭제했습니다 (2026-07-29).** mado_district1 환경을 만들면서 다른
   구성요소들과 대조하다가, `reef_edge_cues`만 다른 요소들(facies zone/닻돌/wreck)과 달리 JSON에
   `evidence` 필드가 아예 없다는 걸 발견했습니다. 이 4개가 처음 추가된 커밋(`c93f7fe`)의 실제 좌표
   생성 코드를 보면 `1.9f + 0.35f * Index`, `0.28f + 0.07f * Index` 같은 등차수열로 크기를 찍어낸
   것이었습니다 — 닻돌처럼 카탈로그에서 옮긴 불규칙한 실측치가 아니라, 그냥 절차적으로 생성한
   장식용 값이었다는 뜻입니다. 원문에서도 이 4개를 특정할 근거를 못 찾아서, `reef_edge_cues: []`로
   비웠습니다. C++ 스폰 메커니즘(`SpawnShipwreckProjectMadoReefEdgeCues`, config-driven)은 나중에
   진짜 근거를 찾으면 재사용할 수 있게 그대로 남겨뒀습니다.

## 엔진 패치 적용

2026-07-30부터 **엔진 소스 수정분은 whole-file이 아니라 diff**로 전달합니다
(`FIELD_IMPLEMENTATION_GUIDE.md` §5.4 — 취득 측이 같은 파일을 통짜로 받으면 upstream이 바뀌거나
양쪽이 같은 파일을 건드릴 때 조용히 덮어써진다고 지적한 부분). 순서대로 적용하세요.

1. HoloOcean 2.4.0 소스 (`develop` 브랜치, `holoocean_ws`가 쓰는 것과 같은 커밋)를 받습니다.
2. 취득 측 패치(`holoocean-sss-engine.diff`, `sss-scene-runtime.diff`)를 먼저 적용합니다 — 이 순서가
   중요합니다 (`sss-scene-runtime.diff`가 `holoocean-sss-engine.diff`의 결과물을 전제로 함).
3. 이 폴더의 `../engine.diff`를 적용합니다. `MadoSceneConfig.h/.cpp`(신규 파일)와
   `HolodeckGameMode.cpp`(지형/닻돌/reef cue/wreck 스폰, `sssfacies`/`sssmat:...`/`wreck` 태그 부착
   포함) 수정분이 여기 들어있습니다. 이 두 파일은 저희만 건드리므로 diff가 충돌 없이 그대로 적용됩니다.
4. `patches/FIELD_IMPEDANCE_HOOK.md`를 따라 `HolodeckRaycastSonar.h/.cpp`에 `GetFieldImpedanceAtLocation()`
   본문을 채웁니다. 이 파일은 diff로 안 드리는 유일한 예외입니다 — `sss-scene-runtime.diff`가 이미 같은
   파일을 건드리므로, 저희가 또 diff를 얹으면 충돌 위험이 생깁니다 (위 "재질 조회 방식" 참고).
5. `Content/Config/materials.csv` — 이 표의 재질 물성 행을 취득 측 파일에 **추가**(통째 교체 금지 —
   `M_Metal_Steel`/`M_Wood_Pine`/`M_CobbleStone_Rough` 등 취득 측 행이 사라지면 그 오브젝트들이 기본
   임피던스로 새하얗게 나옵니다, `FIELD_IMPLEMENTATION_GUIDE.md` §5.2).
6. `Content/Config/mado_scenes/*.json`, `Content/Config/mado_terrain/*.csv` — 씬/지형 데이터, 그대로
   같은 상대경로에 복사.

**더 이상 필요 없음**: `ShipwreckKhoaSmoothTerrainData.generated.h`. 지형이 컴파일된 C++ 헤더에서
`Content/Config/mado_terrain/*.csv` 런타임 로딩으로 바뀌면서 삭제했습니다.

**포함하지 않은 것**: `RaycastSidescanSonar.cpp/.h`, `SonarData.h`, `Octree.cpp`. 이 파일들은 SSS 취득
(양측 출력/빔 지향성/GT 마스크) 담당 쪽에서 관리하며, `sss-scene-runtime.diff`/`holoocean-sss-engine.diff`가
이미 필요한 수정(태그 디스패치, `impedance_override` 필드 등)을 포함합니다. 이 환경 저장소는 그 결과물
위에 `FIELD_IMPEDANCE_HOOK.md`의 작은 함수 본문 하나만 얹습니다 (아래 "역할 분담" 참고).

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
