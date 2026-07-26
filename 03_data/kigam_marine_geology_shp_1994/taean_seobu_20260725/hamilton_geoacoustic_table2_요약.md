# 퇴적물 종류별 지음향 물성표 (APL-UW TR9407 Table 2 발췌)

DTIC 원문 링크(`https://apps.dtic.mil/sti/tr/pdf/ADB199453.pdf`)가 가끔 유지보수 페이지로 리다이렉트되는 것으로 확인되어, 원문 Table 2 (p. IV-6) 스캔 이미지를 직접 캡처해서 별도 파일로 전달한다. 이 문서는 그 표에서 우리 조사구역과 관련된 행을 텍스트로도 옮겨둔 것이다.

## 원문 서지사항

- Applied Physics Laboratory, University of Washington (1994), *High-Frequency Ocean Environmental Acoustic Models Handbook*, Technical Report APL-UW TR 9407.
- DTIC Accession Number: ADB199453 (공개, 비밀분류 아님)
- 원문 Table 2, "Model inputs in terms of sediment name", p. IV-6
- 이 표는 Hamilton, E.L. (1980), "Geoacoustic modeling of the sea floor," *J. Acoust. Soc. Am.* 68(5), 1313-1340 의 지음향 퇴적물 모델을 표준 퇴적물명 기준으로 재정리한 표준 2차 자료임 (원 논문 자체는 유료 저널 게재본만 존재: https://pubs.aip.org/asa/jasa/article/68/5/1313/685821)

## 표 값 정의

- ρ (density ratio): 퇴적물 밀도 / 물 밀도
- ν (sound speed ratio): 퇴적물 음속 / 물 음속
- 실제 물성값 = 비율 × 기준 물 값

## 발췌 표 (모래~실트 계열, 우리 조사구역과 인접한 행 포함)

| Mz (phi) | 퇴적물명 | ρ (밀도비) | ν (음속비) |
|---:|---|---:|---:|
| 1.5 | Medium Sand | 1.845 | 1.1782 |
| 2.5 | Fine Sand, Silty Sand | 1.451 | 1.1073 |
| 3.0 | Muddy Sand | 1.339 | 1.0800 |
| **3.5** | **Very Fine Sand** ← 우리 조사구역 값 | **1.268** | **1.0568** |
| 4.0 | Clayey Sand | 1.224 | 1.0364 |
| 4.5 | Coarse Silt | 1.195 | 1.0179 |
| 5.0 | Sandy Silt, Gravelly Mud | 1.169 | 0.9999 |

## 우리 프로젝트에 적용한 계산

기준 물 값 (이 프로젝트가 다른 SSS 실행에서도 쓰는 값, `환경_반영_근거_정리.md` 참고): 밀도 1024 kg/m³, 음속 1480 m/s.

```
density = 1.268 x 1024 = 1298 kg/m^3
speed   = 1.0568 x 1480 = 1564 m/s
impedance = 1298 x 1564 / 1e6 = 2.030 MRayl
```

`materials.csv`의 `ShipwreckProjectSeabed` 행에 반영한 값: `1298, 1564`.

## 원문을 직접 보고 싶을 때

1. 이번에 전달한 스크린샷 파일들 (`apluw_table2_zoom.png`: Table 2 확대본, 원본 페이지 전체 캡처본)을 그대로 보면 된다.
2. DTIC 링크가 열리면: https://apps.dtic.mil/sti/tr/pdf/ADB199453.pdf (전체 210페이지 스캔본, Table 2는 p. IV-6, PDF 물리 페이지로는 128번째 페이지)
3. DTIC가 유지보수 중이면 나중에 다시 시도하거나, University of Washington APL 자체 페이지에서 "APL-UW TR 9407"으로 검색.
