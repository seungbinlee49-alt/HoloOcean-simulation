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
- 수온/염분 기반 `WaterDensity`, `WaterSpeedSound` 계산에 사용
- 유향/유속은 기본 baseline이 아니라 optional drift/yaw proxy와 stress-test에 사용

