# Current and Material Notes

## Material

KIGAM 태안반도 서부해역 자료에서 조사구역의 퇴적물 분류가 `Very Fine Sand`로 판정되었습니다.

APL-UW TR9407 Table 2의 Hamilton 계열 지음향 ratio를 적용했습니다.

```text
Very Fine Sand
density ratio = 1.268
sound speed ratio = 1.0568
reference water density = 1024 kg/m^3
reference water sound speed = 1480 m/s
seabed density = 1298 kg/m^3
seabed sound speed = 1564 m/s
seabed impedance = 2.030 MRayl
```

HoloOcean RaycastSidescanSonar는 ray hit material type을 기준으로 `materials.csv`에서 density와 sound speed를 읽고, `density * speed` 임피던스를 반사강도 계산에 사용합니다.

## Current

KHOA 부이 관측값에는 유향과 유속이 있습니다. 다만 현재 코드는 AUV 동역학/제어기 모델에 조류 힘을 넣는 구조가 아니라, 위치 경로에 drift/yaw perturbation을 더하는 kinematic proxy입니다.

따라서 기본 baseline에서는 drift/yaw를 끄고, 별도 옵션으로만 사용합니다.

권장 구분:

- baseline: 수심 + 재질 + 물성 + Raycast SSS 검증
- weak-current proxy: 작은 누적 drift/yaw로 현실적인 흔들림만 추가
- full-current stress-test: 제어기 없이 원유속을 넣으면 survey가 무너질 수 있음을 확인

