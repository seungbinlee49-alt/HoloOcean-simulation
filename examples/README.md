# Example Outputs

## `mado_report_environment_v1`

`mado_report_environment_v1` 환경에서 7-pass lawnmower survey(`--y-tracks="-48,-32,-16,0,16,32,48"`,
`--range-bins 1000 --range-min 0.5 --range-max 60.0 --azimuth 85.0 --azimuth-ray-count 34000`)로 캡처한
대표 결과입니다.

포함 이미지:

- `khoa_environment_raycast_survey_full.png`: 원시 raycast waterfall (TVG/로그 스케일 적용 전)
- `tvg_normalized_speckled_display.png`: range-gain 정규화(pass별 독립 보정) + log-scale 표시 +
  speckle noise를 적용한 최종 표시본. 실제 SSS 디스플레이와 비교하려면 이 이미지를 보세요.

이 캡처는 `--azimuth-ray-count 34000`로 찍었습니다. 8500으로 찍으면 nadir 근처에 동심원 형태의 aliasing
아티팩트가 나타납니다 (환경 README의 "알려진 제약" 참고) — 재질/지형 문제가 아니라 raycast 샘플링 밀도
문제이니, 다른 range bin/range 설정을 쓸 때는 이 권장값을 다시 계산해야 합니다.
