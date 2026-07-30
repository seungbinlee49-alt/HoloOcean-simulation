# Environment: `mado_dangampo_environment_v1`

마도 Ⅰ/Ⅱ지구와는 **완전히 다른 보고서, 다른 실제 사이트**입니다. 근거 문서는
2019년 국립해양유산연구소 『태안 당암포해역 수중발굴조사 보고서』(2016년 긴급탐사 +
2017/2018년 1·2차 발굴조사)이며, 마도(안흥량 해협)와 지리적으로 다른 천수만 초입/
안면운하 인근입니다. 프로젝트 공식 연구개발계획서(태안군을 포함한 서남해 12개 권역,
시뮬레이션 환경 다양성 확보가 Year-1 목표)상 스코프 안에 있는 별도 사이트로 추가했습니다.

## 마도 Ⅰ/Ⅱ지구와 뭐가 다른가

| | 마도 Ⅱ지구 (`mado_report_environment_v1`) | 마도 Ⅰ지구 (`mado_district1_environment_v1`) | 당암포 (이 환경) |
| --- | --- | --- | --- |
| 근거 보고서 | 2021 문화재청 마도해역 시굴조사 보고서 | 동일 | 2019 국립해양유산연구소 당암포해역 수중발굴조사 보고서 (별도 문서) |
| 실측 중심좌표 | (README 미기재) | N36°41′23.8″ E126°07′41.4″ | N36°37′04.1″ E126°21′15.1″ (보고서 "사적 가지정 구역" 중심좌표, 원문 그대로) |
| 수심 | 8.45~9.42m | 13.24~14.17m | 서쪽 4~5m → 동쪽 9~10m (원문 서술 기반 **선형보간**, 실측 점군 보간 아님 — 아래 참고) |
| 조사/지정 규모 | 100×120m, 8개 구역 | 18-1구역 40×20m + 18-2구역 20×20m | 500×200m, 100,000㎡ (사적 가지정 전체 범위, 원문 그대로) |
| 표층 재질 | 구역별로 다른 재질 blend (8종) | 균질 Very Fine Silt (Mz 7.5) | 균질 Cobble/Gravel/Pebble (density 2560 kg/m³, sound speed 2664 m/s) |
| 닻돌 | 16기 (보고서 근거로 반영) | 0기 (전부 마도Ⅱ지구 출수, 근거 확인됨) | 0기로 처리 — **실제로는 1기 근거 있음** (N36°37′04.20″ E126°21′14.60″, 94.0×14.6×4.00~11.40cm, 21.12kg) 이지만 2026-07-30 결정으로 의도적 미반영 |
| reef cue | 0개 (근거 없어 삭제됨) | 0개 (애초에 근거 없음) | 0개 (근거 없음) |
| 선체 증거 | 없음 (근거 없는 테스트 placeholder 제거됨) | 있으나 미스폰(선체 형태 mesh가 실제 판자 파편과 안 맞아서) | 없음 — 원문에 "고선박과 유사한 형태의 이상체는 확인하지 못하였다" 명시 |

## 좌표/범위

보고서 원문(1225행): "당암포 해역 사적 가지정 범위는 중심좌표(N36°37′04.1″, E126°21′15.1″)를
기준으로 동↔서 500m, 남↔북 200m이며, 총 면적이 100,000㎡이다." — 이 좌표/범위 수치는
근사가 아니라 원문에 있는 그대로입니다. 씬 로컬 (0,0) = 이 중심좌표, x축 양수=동쪽,
y축 양수=북쪽 (x=[-250,250], y=[-100,100]).

## 이 환경이 반영한 것

- **지형**: 마도 두 환경과 달리 KHOA 실측 보간이 **아닙니다**. KHOA 150m 국가 격자가 이
  사이트 규모(500m에 5m 경사)의 국소 기복을 못 잡는다는 걸 직접 확인했습니다 — 사이트
  경계 안쪽 KHOA 실측점들은 2.5~4.7m만 나와서 원문의 동쪽 9~10m와 전혀 안 맞습니다.
  대신 원문의 다중빔음향측심기(Multi Beam Echo Sounder) 실측 서술(p.30: "사적 가지정
  구역 해저는 서쪽이 수심 4~5m로 얕고 동쪽이 9~10m로 깊은 형태") 두 끝값(서 4.5m,
  동 9.5m)을 실제 앵커로 삼아 **선형보간**했습니다. 이건 명시적 근사이지 점군 보간이
  아닙니다 — 원문 그림(p.51 멀티빔 결과도)은 좌표그리드/축척이 없는 렌더링이라 점
  데이터를 추출할 수 없었습니다.
- **해저 재질**: 3개 독립 조사(2016 긴급탐사, 2017-2018 N2E3/N2E4 블록 로그, 전체 요약,
  p.30/175/8670행)에서 일관되게 "20cm 크기 돌+자갈층, 20~30cm 두께, 그 아래 사질
  개흙"으로 서술 → APL-UW TR9407 Table 2 "Cobble, Gravel, Pebble" 행(density ratio
  2.50, sound speed ratio 1.80) 적용, 프로젝트 기준 해수값(1024 kg/m³, 1480 m/s) 곱해서
  2560/2664. 마도 닻돌의 "Rock" 행(2560/3700)과는 다른 행입니다 — 닻돌은 가공된 고체
  돌, 여긴 자연 상태 느슨한 자갈층이라 음속비가 더 낮은 게 맞습니다.
- **닻돌 근거는 있으나 미반영**: 위 비교표 참고. 좌표/치수는 JSON `comment` 필드에
  기록해뒀습니다 — 나중에 필요하면 그대로 `anchor_stones`에 추가하면 됩니다 (yaw는
  원문에 없어서 별도 결정 필요).
- **reef cue / wreck 없음**: 원문에 근거 자체가 없습니다 (위 표 참고).
- **수온/염분 미반영**: 원문에 실측 CTD 수온/염분 데이터 테이블은 없었습니다 (발간사의
  "바닷속 온도가 9~10℃로 차가웠다"는 서술 하나뿐, 정밀 물성 계산에 쓸 수 있는 형태가
  아님). 그래서 이 사이트만의 독자적 온도/염분 기반 값이 아니라, 프로젝트 공통 기준
  해수값(1024 kg/m³, 1480 m/s)을 그대로 사용했습니다.
- **수온/염분/음속/밀도 — 이제 반영됨 (2026-07-31 추가)**: 물성은 씬 JSON이 아니라
  취득 스크립트(`04_code/environment_data/run_khoa_environment_raycast_survey_v1.py`)의
  `--water-profile` 옵션으로 관리됩니다 (마도 두 환경도 마찬가지 — `MadoSceneConfig`엔
  이 필드 자체가 없습니다, 처음에 이 레이어를 못 찾아서 "반영 불가"라고 잘못 판단했던
  적이 있습니다). 당암포는 원문에 마도 수준의 CTD 실측 테이블이 없어서(발간사의 "9~10℃"
  정성적 서술뿐, 염분 실측값은 아예 없음), `apply_dangampo_water_overrides()` 함수를
  새로 추가했습니다: 온도는 원문 서술 중간값 9.5℃(2018년 4월 2차 발굴조사, 실제 이
  사이트 값), 염분은 마도 방향값(243.3도)과 동일하게 기존 태안항 부이 실측값 27.9psu를
  그대로 씁니다(당암포 고유 값 아님 — 로컬 염분 데이터 자체가 없음). 밀도/음속은 이
  둘을 UNESCO/Mackenzie 공식(스크립트에 이미 있는 공식, 새로 만든 근사식 아님)에 넣어
  계산합니다 (수심에 따라 달라짐 — 예: 6.75m 중간수심 기준 밀도 1021.49kg/m³, 음속
  1479.28m/s). **`--water-profile mado_ctd`(기본값)로 당암포를 캡처하면 안 됩니다** —
  계절도 다르고(마도는 2015년 가을 CTD) 사이트도 다른 값이 섞여 들어갑니다. 반드시
  `--water-profile dangampo_ctd`를 명시하세요.
- **유향/유속 — 이제 반영됨 (2026-07-31 추가)**: 원문(p.33)에 "다른 해역에 비해 조류는
  빠른 편"이라는 서술과 밀물(남서)/썰물(남동) 방향 서술이 있고, 같은 페이지에 실제
  수치조류도(numerical tidal current chart, 조류 cm/s 색상 범례 0-20/20-40/.../120- 7단계)
  가 있습니다. 당암포 유적 표시 지점 주변 화살표 색을 확인한 결과 가장 낮은 두 구간
  (0-20~20-40 cm/s)에 해당하는 것으로 보였습니다 — 사이트 자체가 안면수도 본류가 아니라
  안쪽으로 들어간 작은 만이라, "조류 빠름" 서술이 가리키는 본류 유속보다 국지적으로는
  느릴 가능성이 있습니다. 정확한 단일 cm/s 값은 지도 해상도 한계로 특정 불가 — 마도의
  0.115 m/s(2011 마도2호선 실측 유속표 기반)가 이 범위 안에 들어오는 우연의 일치가
  있어서, 유속 크기는 당분간 기존 `--mado-current-speed-mps 0.115` 기본값을 그대로
  써도 원문 범위와 모순되지 않습니다(다만 이건 "우연히 겹친다"는 것이지 당암포 고유
  실측값이 아닙니다). 방향(243.3도, 태안항 부이)은 마도와 동일한 광역 소스를 그대로
  재사용 — 방향은 사이트 무관하게 재사용 가능한 값이라 문제없습니다.

## 검증

**아직 실제 엔진에서 로드 테스트를 하지 않았습니다.** 지금까지 완료한 건 정적 검증뿐입니다:

- JSON 파서(`MadoSceneConfig.cpp`의 `ParseSceneConfigJson`)가 기대하는 모든 필드명/타입
  (`GetNum`/`GetStr`/`TryGetObjectField`/`TryGetArrayField`)과 이 JSON의 키를 하나하나
  대조 — 전부 일치, 파싱 실패 여지 없음.
- 지형 CSV 로더(`LoadTerrainCsv`)의 grid 자동감지 로직(iy가 바뀌는 지점을 보고 GridX
  추정, raster order 요구: iy outer / ix inner)과 실제 생성 스크립트의 행 순서 일치 확인.
  4141행 = 101×41, 나머지 없이 정확히 나눠떨어짐 확인.

`HOLOOCEAN_SHIPWRECK_SCENE_PRESET=mado_dangampo_environment_v1.json`으로 직접 로드해서
엔진 로그로 확인하는 건 아직 안 했습니다 — 실행 후 이 섹션을 실제 로그로 교체해야 합니다.
캡처하면 재질/닻돌/reef cue/wreck이 전부 없어서 균질한 텍스처 + 서→동 경사에 따른
스와스 기하 변화만 나오는 게 정상입니다 (마도 두 환경과 달리 재질 대비나 타겟 오브젝트가
없다는 게 이 환경의 현재 특성입니다).

## 엔진 패치 적용

마도 Ⅰ지구와 동일하게, 이 환경은 별도 C++ 패치가 없습니다 —
`mado_report_environment_v1/patches/engine.diff`(`MadoSceneConfig.h/.cpp` 신규 +
`HolodeckGameMode.cpp` 수정)와 `mado_report_environment_v1/patches/FIELD_IMPEDANCE_HOOK.md`
(`GetFieldImpedanceAtLocation()` 적용 지침)를 그대로 공유합니다. 이 폴더에는 **씬
데이터만** 있습니다:

- `patches/engine/Content/Config/mado_scenes/mado_dangampo_environment_v1.json`
- `patches/engine/Content/Config/mado_terrain/mado_dangampo_environment_v1_terrain.csv`

적용 순서: `mado_report_environment_v1/patches/engine`을 먼저 적용(엔진 소스+빌드)한 뒤,
이 폴더의 두 파일을 같은 상대경로(`Content/Config/mado_scenes/`,
`Content/Config/mado_terrain/`)에 추가로 복사하면 됩니다. 리빌드는 필요 없습니다.

## 역할 분담

`mado_report_environment_v1/README.md`의 "역할 분담"과 동일 — 이 저장소는 환경/씬 제작만
담당하고, SSS 취득 파이프라인은 별도 담당자가 관리합니다.

## 실행 예시

**주의: 이 환경은 마도 두 환경과 규모가 다릅니다(500×200m vs 100~140m급).**
`--x-min`/`--x-max`/`--y-tracks` 기본값은 마도 기준으로 맞춰져 있어서, 아래처럼 명시적으로
지정하지 않으면 지형 밖을 조사하게 됩니다. `--water-profile`도 기본값(`mado_ctd`)이 아니라
**반드시 `dangampo_ctd`를 명시**하세요 (위 "이 환경이 반영한 것" 물성 항목 참고).

```powershell
& '.venv_holoocean\Scripts\python.exe' `
  '04_code\environment_data\run_khoa_environment_raycast_survey_v1.py' `
  --output-dir '06_results\mado_dangampo_environment_v1' `
  --binary-path 'C:\path\to\HoloOcean\2.4.0\worlds\Ocean\Windows\Holodeck\Binaries\Win64\Holodeck.exe' `
  --scene-proxy mado_dangampo_environment_v1.json `
  --terrain-profile khoa `
  --water-profile dangampo_ctd `
  --x-min -250 --x-max 250 `
  --y-tracks="-80,-40,0,40,80" `
  --rows-per-pass 600 `
  --sonar-hz 10 --range-bins 1000 --range-min 0.5 --range-max 60.0 `
  --azimuth 85.0 --elevation 0.25 `
  --azimuth-ray-count 34000 --elevation-ray-count 4
```

`HOLOOCEAN_SHIPWRECK_SCENE_PRESET=mado_dangampo_environment_v1.json` 환경변수를 실행 전에
반드시 설정해야 합니다. `--water-profile dangampo_ctd`를 빼먹으면 마도4호선 CTD 값이 잘못
적용됩니다 (위 참고).
