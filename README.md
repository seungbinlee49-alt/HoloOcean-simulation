# HoloOcean Shipwreck Environment

HoloOcean 2.4.0 (`RaycastSidescanSonar` 기준) 위에서, 태안-마도 인근 해역 실측/보고서 자료를 반영해
해저 지형·재질 씬을 만드는 저장소입니다.

## 역할 분담

이 저장소는 **환경/씬 제작**을 담당합니다. SSS(사이드스캔 소나) 취득 파이프라인(양측 출력, 빔 지향성,
GT 마스크 생성)은 별도 담당자가 관리합니다. 이 저장소에서 만든 환경을 pull 받아 SSS 취득 파이프라인에
얹는 쪽이 지켜야 할 통합 규약은 [`SCENE_INTEGRATION_GUIDE.md`](SCENE_INTEGRATION_GUIDE.md)에 정리했습니다.

씬을 새로 받아갈 때마다 "이 환경 구성이 왜 이렇게 되어 있는지" 헷갈리는 일이 없도록, 각 환경 폴더 README에
반영 근거·좌표 범위·알려진 제약(예: 난파선 액터 유무, 지형 평탄 여부)을 명시했습니다.

## 폴더 구조

```text
10_holoocean_sim/
  environments/            # 환경(씬)별 폴더 — 여러 개 씬을 이 아래에 계속 추가합니다
    mado_report_environment_v1/
      README.md            # 이 환경의 반영 근거, 좌표, SCENE_INTEGRATION_GUIDE 체크리스트 대비 현황
      patches/engine/       # HoloOcean 2.4.0 소스에 적용할 C++ 패치

04_code/
  environment_data/
    run_khoa_environment_raycast_survey_v1.py   # 조사 경로 생성 + Raycast SSS 캡처
    derive_kigam_seabed_material_v1.py
    fetch_khoa_bathymetry.py
    make_khoa_location_map.py
  visualization/
    check_khoa_survey_ground_contact_24.py

03_data/
  khoa_bathymetry/                    # KHOA 실측 수심점 + 보간 heightfield
  kigam_marine_geology_shp_1994/      # KIGAM 표층퇴적물 판정 (baseline Very Fine Sand 근거)
  khoa_observation_current_20260722/  # KHOA 부이 관측 (수온/염분/유향/유속)
  taean_mado_2021_report_analysis/    # 2021 문화재청 마도해역 시굴조사 보고서 텍스트 (구역/재질/닻돌 근거)
```

## 실행 준비

```powershell
python -m venv .venv_holoocean
.\.venv_holoocean\Scripts\pip install -r requirements.txt
```

`holoocean` Python client는 사용 중인 HoloOcean 2.4.0 소스/런타임과 맞춰 별도로 설치하거나
`PYTHONPATH`에 연결해야 합니다.

이 저장소는 HoloOcean/Unreal 전체 소스와 빌드 결과물을 포함하지 않습니다. 실행하려면 다음 중 하나가
필요합니다.

1. HoloOcean 2.4.0 소스에 `10_holoocean_sim/environments/<환경명>/patches` 아래 파일을 적용한 뒤
   Unreal Engine 5.3 계열에서 다시 빌드
2. 같은 패치가 이미 적용된 HoloOcean 2.4.0 runtime binary 사용

## 현재 환경

[`10_holoocean_sim/environments/mado_report_environment_v1`](10_holoocean_sim/environments/mado_report_environment_v1/README.md) —
100 x 120m, 보고서 근거 기반 8개 재질 구역 + KHOA 기복 지형 + 닻돌 16기.
자세한 내용, 좌표, 반영 근거, 알려진 제약은 해당 README를 확인하세요.

## 포함하지 않은 것

- HoloOcean/Unreal 전체 소스와 빌드 결과
- SSS 취득 파이프라인 소스 (`RaycastSidescanSonar.cpp/.h`, `Octree.cpp`, `client/src/holoocean/sensors.py`) —
  별도 담당자가 관리
- SSS 디스플레이 후처리 (TVG 정규화, log-scale, speckle noise 등) — 취득 파이프라인 쪽 범위
- API key
- 원본 KIGAM zip/shapefile 전체, 2021 보고서 원본 PDF (텍스트 추출본만 포함)
- 대용량 raw `.npy` 캡처 결과

## 참고 출처

- HoloOcean 2.4.0 RaycastSidescanSonar source/API
- 문화재청, 태안 마도해역 해양문화재 시굴조사 보고서 (2021)
- KHOA/data.go.kr 자연과학용 수심정보 조회 API, KHOA 부이 관측자료 API
- KIGAM 지오빅데이터 오픈플랫폼, 태안반도 서부해역 해저지질 자료
- Applied Physics Laboratory, University of Washington, *High-Frequency Ocean Environmental Acoustic
  Models Handbook*, TR9407, Table 2, DTIC ADB199453
