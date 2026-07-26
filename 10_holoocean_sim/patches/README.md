# HoloOcean 2.4.0 Patch Files

이 폴더는 HoloOcean 2.4.0 소스에 적용해야 하는 커스텀 패치 파일입니다.

같은 상대경로로 복사한 뒤 Unreal Engine 5.3 계열에서 HoloOcean을 다시 빌드합니다.

## Included Changes

- `HolodeckGameMode.cpp`
  - `khoa_survey_scene_v1` scene preset 추가
  - KHOA smooth bathymetry terrain mesh 생성
  - 6종 난파선 proxy와 rock, gear/rope 혼동 객체 배치
  - 객체 bottom bounds 기준으로 terrain에 ground contact snap

- `ShipwreckKhoaSmoothTerrainData.generated.h`
  - KHOA 보간 수심 기반 heightfield 데이터

- `HolodeckRaycastSonar.cpp`
  - Raycast hit actor를 `ShipwreckProjectSeabed`, `ShipwreckProjectWreck`, `ShipwreckProjectClutter`로 분류

- `RaycastSidescanSonar.cpp`, `RaycastSidescanSonar.h`
  - Raycast SSS baseline source copy

- `materials.csv`
  - 프로젝트용 seabed/wreck/clutter acoustic material rows 포함

## Important

패치가 적용되지 않은 공식 runtime에서는 `khoa_survey_scene_v1`이 존재하지 않으므로, Python 실행은 되더라도 커스텀 terrain/wreck scene이 생성되지 않습니다.

