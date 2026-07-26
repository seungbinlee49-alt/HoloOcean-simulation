# Current and Material Notes

## Seabed Material

KIGAM 태안반도 서부해역 해저지질 자료에서 현재 조사구역의 표층퇴적물은 `Very Fine Sand`로 판정했습니다.

해저 재질의 음향 물성은 APL-UW TR9407 *High-Frequency Ocean Environmental Acoustic Models Handbook* Table 2에 정리된 Hamilton 계열 값을 사용했습니다.

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

HoloOcean Raycast SSS에서는 ray hit의 material type을 기준으로 `materials.csv`에서 density와 sound speed를 읽고, `density * sound_speed`로 계산한 acoustic impedance를 반사 강도 계산에 사용합니다.

## Current

KHOA 부이 관측값에는 유향과 유속이 포함됩니다. 다만 현재 코드는 AUV 동역학 제어기에 조류 힘을 직접 넣는 구조가 아니라, 위치 경로에 drift/yaw perturbation을 더하는 kinematic proxy입니다.

따라서 기본 baseline에서는 drift/yaw를 끄고, 수심 + 재질 + 물성 + Raycast SSS geometry 검증에 집중합니다.

권장 구분:

- `baseline`: 수심, 해저 재질, 수온/염분 기반 물성, Raycast SSS 검증
- `weak-current proxy`: 작은 drift/yaw로 현실적인 흔들림만 추가
- `full-current stress-test`: 제어기 없이 관측 유속을 그대로 누적하면 survey가 무너질 수 있음을 확인
