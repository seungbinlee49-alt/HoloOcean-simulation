# Environments

이 저장소는 여러 개의 독립적인 HoloOcean 씬(환경)을 폴더 단위로 관리합니다. 각 환경 폴더는 자체
엔진 패치, 좌표/재질 설명, 알려진 제약을 담은 README를 가집니다.

| 환경 | 규모 | 상태 |
| --- | --- | --- |
| [`mado_report_environment_v1`](mado_report_environment_v1/README.md) | 100 x 120m | 현재 기본. 보고서 근거 기반 해저 재질 + KHOA 기복 지형. 난파선 액터 없음 |

새 환경을 추가할 때는 `<name>/patches/engine/...`, `<name>/README.md` 형태로 같은 구조를 따라주세요.
