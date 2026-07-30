# Environments

이 저장소는 여러 개의 독립적인 HoloOcean 씬(환경)을 폴더 단위로 관리합니다. 각 환경 폴더는 자체
엔진 패치, 좌표/재질 설명, 알려진 제약을 담은 README를 가집니다.

| 환경 | 규모 | 상태 |
| --- | --- | --- |
| [`mado_report_environment_v1`](mado_report_environment_v1/README.md) | 100 x 120m | 마도 Ⅱ지구. 보고서 근거 기반 해저 재질(8개 구역) + KHOA 기복 지형 + 닻돌 16기. 난파선 액터 없음 |
| [`mado_district1_environment_v1`](mado_district1_environment_v1/README.md) | ~140 x 160m | 마도 Ⅰ지구. 같은 보고서의 다른 실제 조사구역 (수심 약 2배 깊음, 균질 재질). 난파선 액터 없음. 위 환경과 엔진 패치 공유, 씬 데이터만 별도 |
| [`mado_dangampo_environment_v1`](mado_dangampo_environment_v1/README.md) | 500 x 200m | 태안 당암포해역 (별도 보고서, 천수만/안면수도 인근 — 마도와 다른 사이트). 서→동 4.5m→9.5m 수심 경사(보고서 서술 선형보간) + 균질 Cobble/Gravel/Pebble 재질. 닻돌 근거 1기 있으나 의도적 미반영, 난파선 액터 없음. 위 환경들과 엔진 패치 공유, 씬 데이터만 별도. 아직 엔진 로드 테스트 전 |

새 환경을 추가할 때는 같은 구조를 따라주세요: `<name>/README.md`, 씬 데이터는
`<name>/patches/engine/Content/...`. 엔진 C++ 코드 수정은 whole-file이 아니라
`<name>/patches/engine.diff`(diff)로 주고, 취득 측이 이미 건드리는 공용 파일(예:
`HolodeckRaycastSonar.cpp`)처럼 diff가 충돌할 수 있는 부분만 적용 지침 문서(예:
`mado_report_environment_v1/patches/FIELD_IMPEDANCE_HOOK.md`)로 따로 뺍니다 — 자세한 이유는
`FIELD_IMPLEMENTATION_GUIDE.md` §5.4 참고.
