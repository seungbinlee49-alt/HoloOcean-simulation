# Environment: `mado_district1_environment_v1`

`mado_report_environment_v1`(마도 Ⅱ지구)과 **같은** 2021년 문화재청 마도해역 시굴조사 보고서
(`03_data/taean_mado_2021_report_analysis`)에 실려있는, 완전히 **다른 실제 조사구역**입니다.
2026-07-29 리뷰 항목 ④(탐지 환경 다양화)의 평가용(evaluation) 두 번째 사이트로 추가했습니다 —
학습용 domain-randomized 변형(`mado_report_environment_v1_heldout_material_v1.json`)과 섞으면 안
됩니다. 저건 같은 사이트의 다른 재질 버전이고, 이건 **진짜 다른 위치의 사이트**입니다.

## 마도 Ⅱ지구와 뭐가 다른가

| | 마도 Ⅱ지구 (`mado_report_environment_v1`) | 마도 Ⅰ지구 (이 환경) |
| --- | --- | --- |
| 실측 중심좌표 | (README 미기재, 마도2호선 북동쪽 200m) | N36°41′23.8″ E126°07′41.4″ (마도1호선 남서쪽) |
| 실측 수심 | 8.45~9.42m | 13.24~14.17m (KHOA 인근 실측점 기준 13.5~17m) — **거의 2배 깊음** |
| 발굴 규모 | 100×120m, 8개 구역 | 18-1구역 40×20m + 18-2구역 20×20m (인접 배치 근사) |
| 표층 재질 | 9→8개 구역별로 다른 재질 blend | 전 구역 균질한 무른 개흙 (표층 기준 재질 대비 없음, 아래 참고) |
| 닻돌 | 16기 (보고서 근거) | 0기 (보고서 유물 카탈로그 확인 결과 전부 마도 Ⅱ지구 출수, 아래 근거 참고) |
| reef cue | 0개 (2026-07-29 근거 없어 삭제됨) | 0개 (애초에 근거 없어서 추가 안 함) |
| 선체 증거 | 없음 (테스트 placeholder였다가 제거됨) | 원문 기록은 있으나(저판재 추정 선체편, 약 150×50×30cm) 씬에 스폰하지 않음 — 아래 참고 |

## 좌표/범위

```json
"env_min": [-70.0, -81.0, -20.0],
"env_max": [70.0, 81.0, 5.0]
```

실제 발굴 면적(18-1 40×20m + 18-2 20×20m = 1,200㎡)보다 넓습니다 — KHOA 실측 수심점이 150m
간격이라, 발굴 면적만큼 좁게 자르면 실측점이 안 걸려서 지형을 사실상 추측(외삽)해야 합니다.
그래서 인근 실측점들이 들어오는 범위까지 넓혀서 잘랐습니다. 즉 이 범위 숫자 자체는 원문에 있는
면적 수치가 아니라 지형 데이터 확보 목적으로 정한 것입니다 (마도 Ⅱ지구의 100×120m은 원문의
2018/2019년 조사면적 합과 일치하는 것과 다름).

## 이 환경이 반영한 것

- **지형**: 같은 KHOA 실측 수심점 데이터셋에서, 마도 Ⅰ지구 좌표 인근 영역만 새로 크롭·평활화한
  251x281 heightfield. `Content/Config/mado_terrain/mado_district1_environment_v1_terrain.csv`
  (`mado_report_environment_v1`과 같은 config-driven 지형 로더를 공유 — 자세한 내용은
  [`mado_report_environment_v1/README.md`](../mado_report_environment_v1/README.md)의 "지형"
  항목과 "씬 변형" 참고). 씬 로컬 (0,0)은 크롭 영역의 중심이며, 실제 18-1 grid 중심점과 정확히
  일치하지는 않습니다 — KHOA 원 해상도가 ~150m 간격이라 그 이하 정밀도는 의미가 없습니다.
- **해저 재질**: 보고서 원문(line 1169-1183, 원문 발췌는 JSON의 `comment` 필드 참고)에 따르면
  18-1/18-2 두 구역 모두 표층(0~60cm)이 "무른 개흙층"으로 동일하게 서술되어 있고, 재질 대비는
  더 깊은 층(60~80cm 패각 함유 등)에서만 나타납니다. SSS는 표층만 보므로, 이 환경은 마도
  Ⅱ지구처럼 구역별 재질 blend를 넣지 않고 **전체를 균질한 Very Fine Silt(Mz 7.5) baseline**으로
  처리했습니다. 이건 재질 다양성을 안 만든 게 아니라, 표층 기준으로는 대비를 만들 근거가 실제로
  없다는 뜻입니다.
- **닻돌/reef cue 없음**: 보고서 출수유물 목록(원문 line 6045: "석제류는 닻돌 16점... 출수지점은
  마도Ⅱ지구")에서 직접 확인 — 마도 Ⅰ지구 출수 유물이 아닙니다. 없는 게 정상입니다.
- **선체 파편 증거는 기록만 하고, 씬에는 스폰하지 않습니다.** 18-1구역에서 실제로 확인된 저판재
  추정 선체편(약 150×50×30cm, 40cm 깊이에 매몰, report line 1172-1176)은 진짜 실측 근거지만,
  지금 wreck 스폰 메커니즘(`SpawnShipwreckProjectMadoWrecks`)이 만들 수 있는 형태는 용골+갈비뼈
  8개+양쪽 난간으로 구성된 완전한 선체 골격 모양뿐이라, 평평한 판자 파편 하나를 표현하기엔
  안 맞습니다. 실제 모양과 다른 placeholder를 넣느니 안 넣는 게 낫다고 판단해서, JSON의 최상위
  `comment` 필드에 이 근거를 기록만 해두고 `wreck_spawns`는 비워뒀습니다 (`[]`). 나중에 실제 3D
  모델이 준비되면 그때 이 위치/치수 정보를 그대로 써서 넣으면 됩니다.
- **캡처 시 주의**: 이 환경은 수심이 마도 Ⅱ지구보다 훨씬 깊어서, Python 쪽 자세 계산이 올바른
  지형 데이터를 읽도록 `run_khoa_environment_raycast_survey_v1.py`의
  `load_terrain_for_scene_proxy()`가 `--scene-proxy`로 넘긴 씬 JSON의 `terrain_data_source`를
  보고 맞는 지형을 로드합니다 (이 씬을 추가하면서 발견한 버그: 예전에는 항상 마도 Ⅱ지구 지형을
  읽어서 AUV가 실제보다 훨씬 높이 떠서 나는 문제가 있었습니다 — 지금은 고쳐져 있습니다).

## 검증

`HOLOOCEAN_SHIPWRECK_SCENE_PRESET`에 `mado_district1_environment_v1.json`을 직접 지정하면
(`--scene-proxy mado_district1_environment_v1.json`) 로드됩니다. 엔진 로그로 직접 확인:

```
MadoSceneConfig: loaded 'mado_district1_environment_v1' ... (zones=0 anchor_stones=0 reef_cues=0 wrecks=0)
MadoTerrainData: loaded 'mado_district1_environment_v1_terrain.csv' grid=251x281 x=[-69.55,69.55] y=[-80.67,80.67] depth=[13.24,14.17]
```

캡처 결과: 재질 구역/닻돌/reef cue/wreck이 전부 없어서, 균질한 텍스처만 나오는 게 정상입니다
(마도 Ⅱ지구의 뚜렷한 재질 구역 대비와의 차이 자체가 의도된 것 — 위 "마도 Ⅱ지구와 뭐가 다른가"
참고).

## 엔진 패치 적용

이 환경은 별도 C++ 패치가 없습니다 — `mado_report_environment_v1/patches/engine`의 config-driven
시스템(`MadoSceneConfig.h/.cpp`, `HolodeckGameMode.cpp`, `HolodeckRaycastSonar.cpp`)을 그대로
공유합니다. 이 폴더에는 **씬 데이터만** 있습니다:

- `patches/engine/Content/Config/mado_scenes/mado_district1_environment_v1.json`
- `patches/engine/Content/Config/mado_terrain/mado_district1_environment_v1_terrain.csv`

적용 순서: `mado_report_environment_v1/patches/engine`을 먼저 적용(엔진 소스+빌드)한 뒤, 이
폴더의 두 파일을 같은 상대경로(`Content/Config/mado_scenes/`, `Content/Config/mado_terrain/`)에
추가로 복사하면 됩니다. 리빌드는 필요 없습니다.

## 역할 분담

`mado_report_environment_v1/README.md`의 "역할 분담"과 동일 — 이 저장소는 환경/씬 제작만
담당하고, SSS 취득 파이프라인은 별도 담당자가 관리합니다.
