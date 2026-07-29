# Environments

이 저장소는 여러 개의 독립적인 HoloOcean 씬(환경)을 폴더 단위로 관리합니다. 각 환경 폴더는 자체
엔진 패치, 좌표/재질 설명, 알려진 제약을 담은 README를 가집니다.

| 환경 | 규모 | 상태 |
| --- | --- | --- |
| [`mado_report_environment_v1`](mado_report_environment_v1/README.md) | 100 x 120m | 마도 Ⅱ지구. 보고서 근거 기반 해저 재질(8개 구역) + KHOA 기복 지형 + 닻돌 16기. 난파선 액터 없음 |
| [`mado_district1_environment_v1`](mado_district1_environment_v1/README.md) | ~140 x 160m | 마도 Ⅰ지구. 같은 보고서의 다른 실제 조사구역 (수심 약 2배 깊음, 균질 재질, 저판재 파편 1점). 위 환경과 엔진 패치 공유, 씬 데이터만 별도 |

새 환경을 추가할 때는 `<name>/patches/engine/...`, `<name>/README.md` 형태로 같은 구조를 따라주세요.
