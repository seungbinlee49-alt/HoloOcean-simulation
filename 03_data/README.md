# Data Summary

이 폴더에는 재현에 필요한 처리 결과만 포함했습니다.

원본 API key, 원본 KIGAM zip/shapefile 전체, 대용량 raw 결과는 포함하지 않았습니다.

## KHOA Bathymetry

`khoa_bathymetry/taean_mado_excavation_reference_20260719`

- 태안/마도 인근 bbox의 수심점
- HoloOcean local coordinate로 변환된 point table
- smooth bathymetry heightfield

## KIGAM Marine Geology

`kigam_marine_geology_shp_1994/taean_seobu_20260725`

- 조사구역과 겹치는 표층퇴적물 판정 결과
- `Very Fine Sand` 기반 seabed material derivation
- APL-UW TR9407 Table 2 기반 음향 임피던스 근거 표

## KHOA Observation / Current

`khoa_observation_current_20260722`

- 태안/마도 인근 부이 관측 요약
- `mado_report_environment_v1`은 이 부이의 원시 유속을 그대로 쓰지 않고, Mado-2호선(2011) 보고서 실측
  유속표(0.0247~0.2151 m/s)를 근거로 한 0.115 m/s를 기본 조류 속도로 사용합니다. 방향 proxy로만 부이 관측을
  참고합니다.

## 2021 마도해역 시굴조사 보고서 (Taean Mado 2021 Report)

`taean_mado_2021_report_analysis/2021_taean_mado_report_extracted_text.txt`

- 문화재청 2021년 태안 마도해역 해양문화재 시굴조사 보고서에서 추출한 텍스트 (원본 PDF는 포함하지 않음)
- `mado_report_environment_v1`의 8개 해저 재질 구역(서이상/동이상/18F/18H/19-B/C/18E 등) 좌표·재질 기술,
  닻돌 16기의 실측 치수(길이/폭/두께), Mado-2호선 실측 유속표의 1차 근거 문서입니다.
- 각 구역/치수가 코드에서 어떻게 반영됐는지는
  `10_holoocean_sim/environments/mado_report_environment_v1/README.md`와
  `Content/Config/mado_scenes/mado_report_environment_v1.json`의 `facies_zones[].evidence` 필드에
  보고서 좌표와 함께 남겨뒀습니다 (config-driven으로 바뀌면서 C++ 인라인 주석에서 JSON으로 이동).

