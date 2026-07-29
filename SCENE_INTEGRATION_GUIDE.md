# 환경씬 제작 지침서 — SSS 소나 프로젝트와 통합하기 위한 요구사항

> **대상**: HoloOcean 환경/씬(지형·해류·수중환경)을 제작하는 쪽
> **목적**: 제작된 씬을 SSS(사이드스캔 소나) 취득 파이프라인에 **수정 없이 바로 얹기** 위한 규약
> **기준**: HoloOcean 2.4.0 / UE 5.3.2, `RaycastSidescanSonar` (CPU 레이 트레이싱)

---

## 0. 30초 요약 (체크리스트)

| # | 항목 | 안 지키면 |
| --- | --- | --- |
| 1 | 소나가 봐야 할 모든 메시에 **`CTF_USE_COMPLEX_AS_SIMPLE`** 콜리전 설정 | **모든 물체가 똑같이 새하얗게** 나옴 (재질 구분 소멸) |
| 2 | 사용한 재질을 **`materials.csv`에 등록** (밀도, 음속) | 해당 물체가 **기본 임피던스(1e8)** 로 과반사 |
| 3 | 재질 **애셋 이름**을 의미 있게 (`M_Sand_Fine` 등), 씬 전체에서 일관되게 | 등록·관리 불가 |
| 4 | **난파선(정답 객체) 액터에 `wreck` 태그 부여** (§3.5) | **GT 라벨 마스크가 전부 비어서 나옴** |
| 5 | 지형이 **평탄하지 않다면 반드시 사전 고지** | 고도 기반 보정 로직이 어긋남 (별도 개발 필요) |
| 6 | 씬 범위를 **`config.json`의 `env_min`/`env_max`** 에 정확히 기입 | 주행 경로 자동 산출이 씬 밖으로 나감 |
| 7 | **엔진 소스는 SSS 패치본을 사용** (아래 §5) | SSS 기능(양측 출력/빔지향성/GT라벨)이 **아예 없음** |

**가장 흔한 사고**: 1번과 2번. 둘 다 "에러가 안 나고 조용히 잘못된 이미지가 나오는" 유형이라 특히 주의.

---

## 1. 소나가 씬을 읽는 방식 (왜 이런 요구사항이 있는지)

SSS 센서는 매 tick 부채꼴로 **레이를 발사**하고, 맞은 지점에서 세 가지를 읽습니다.

```cpp
// HolodeckRaycastSonar.cpp :: ComputeDetection
Detection.distance      = HitInfo.Distance;                      // 거리
Detection.cos_inc_angle = dot(VecInc, HitInfo.ImpactNormal);     // 입사각 (표면 법선 필요)
Detection.material_type = Material->GetName();                   // 재질 이름
```

그리고 재질 이름으로 임피던스를 찾아 **반사 강도**를 계산합니다.

```cpp
z   = GetImpedanceFromMap(material);      // 재질 임피던스 (materials.csv)
Zw  = WaterDensity * WaterSpeedSound;     // 물 임피던스
R   = (z - Zw) / (z + Zw);                // 반사계수
val = R*R * cos_inc_angle;                // 최종 후방산란 세기
```

> **핵심**: 소나는 **렌더링(그래픽)을 전혀 보지 않습니다.** 오직 **콜리전 지오메트리 + 표면 법선 + 재질 애셋 이름**만 봅니다.
> 따라서 "예쁘게 보이는 씬"과 "소나에 제대로 잡히는 씬"은 별개이며, 아래 규약이 필요합니다.

---

## 2. 【필수】 콜리전 설정

### 2.1 `CTF_USE_COMPLEX_AS_SIMPLE` — 가장 중요

소나 레이는 **`bTraceComplex = false`** 로 트레이스합니다. 이 상태에서 메시가 단순 콜리전(박스/캡슐 근사)을 쓰면 **`FaceIndex`가 무효**가 되어 재질 조회가 실패합니다.

- 실패 시 재질 = `"Unknown"` → 기본 임피던스 **1e8** → 반사계수 R ≈ 1 → **전부 최대 밝기**
- 형상도 단순 콜리전 모양(박스)대로 잡혀 실제 실루엣이 사라집니다

**요구사항**: 소나에 잡혀야 하는 모든 StaticMesh의 `body_setup.collision_trace_flag` 를
**`CTF_USE_COMPLEX_AS_SIMPLE`** 로 설정.

에디터: StaticMesh 열기 → *Collision Complexity* → **`Use Complex Collision As Simple`**

Python(커맨드렛) 예시:

```python
bs = mesh.get_editor_property("body_setup")
bs.set_editor_property("collision_trace_flag",
                       unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
```

> Landscape 액터는 기본적으로 복합 콜리전을 쓰므로 대개 문제없지만, **StaticMesh로 만든 지형/암반/난파선/구조물은 반드시 설정**해야 합니다.

### 2.2 콜리전 채널

소나는 전용 트레이스 채널을 사용합니다 (`Config/DefaultEngine.ini` 정의).

| 채널 | 이름 | 용도 |
| --- | --- | --- |
| `ECC_GameTraceChannel3` | `Raycast` | 소나 기본 |
| `ECC_GameTraceChannel4` | `RaycastIgnorePlants` | `IgnorePlants=true` 일 때 |

- 두 채널 모두 기본 응답이 **Block** 이므로, 일반적인 StaticMesh는 **별도 설정 불필요**합니다.
- **소나에 안 잡히게 하고 싶은 것**(순수 장식, 파티클, 시각용 프록시)은 이 채널 응답을 **Ignore** 로 설정하세요.
- **해초/산호**를 "소나에서 선택적으로 무시" 하고 싶다면 `RaycastIgnorePlants`(채널4) 응답을 Ignore 로 두세요. 그러면 취득 시 `IgnorePlants` 옵션으로 on/off 할 수 있습니다.

### 2.3 물 / 수면

- **물(WaterZone, FFT 파도 표면)은 소나 채널을 Block 하면 안 됩니다.** 현재 기본 설정은 통과이며, 이 덕분에 파도를 켜도 소나가 정상 동작합니다.
- 물을 소나가 감지하면 전 구간이 수면 반사로 덮여 **이미지가 완전히 망가집니다.**

---

## 3. 【필수】 재질 등록 (`materials.csv`)

### 3.1 형식

`engine/Content/Config/materials.csv` — 헤더 + `재질이름, 밀도(kg/m³), 음속(m/s)`

```csv
Material, Density kg/m^3, Speed of Sound m/s
M_Brown_Sand, 1900, 1650
M_Metal_Steel, 7700, 5900
M_Wood_Pine, 650, 3300
M_CobbleStone_Rough, 2600, 4500
```

- **이름은 UE 재질 애셋 이름**(`Material->GetName()`)입니다. 경로가 아니라 애셋명입니다. (예: `M_Brown_Sand`)
- 머티리얼 **인스턴스**를 쓰면 인스턴스 이름(`MI_...`)이 기록되므로, 인스턴스도 각각 등록해야 합니다.

### 3.2 미등록 시 동작 (자동 발견 기능)

미등록 재질을 만나면 엔진이:
1. 로그 경고 `Material %s not found in materials map.`
2. **기본 임피던스 1e8** 사용 (→ 과도하게 밝게 나옴)
3. **`materials.csv` 에 `<이름>, 10000, 10000` 줄을 자동 추가**

> **활용법**: 새 씬을 처음 한 번 돌려보면 csv에 미등록 재질이 전부 자동으로 추가됩니다.
> 그 줄들의 `10000, 10000` 을 **실제 물성치로 고쳐주기만** 하면 됩니다. (등록 누락 확인용으로도 유용)

### 3.3 권장 물성치 (참고값)

| 재질 유형 | 밀도 kg/m³ | 음속 m/s | 소나 상 특징 |
| --- | --- | --- | --- |
| 미세사/니질 해저 | 1500~1900 | 1500~1650 | 어두움 (물과 임피던스 유사) |
| 조립사/자갈 | 1900~2600 | 1650~4500 | 중간~밝음 |
| 암반 | 2600~3200 | 4500~5000 | 밝음 |
| 콘크리트 | 2400 | 3700 | 밝음 |
| 목재(선체) | 650~1000 | 3300~5000 | **어두움** (실제 SSS와 동일) |
| 강철(선체) | 7700~8000 | 3150~5900 | **가장 밝음** |
| 해양생물 착생/산호 | 2000 | 2500 | 중간 |

> 물(해수) 임피던스 기준 = `1024 × 1500`. 재질 임피던스가 이 값에 가까울수록 어둡게, 멀수록 밝게 나옵니다.

---

## 3.5 【필수】 GT 라벨링용 액터 태그

취득 시 **난파선 이진 마스크(GT)** 를 자동 생성합니다. 소나는 레이가 맞은 **액터의 UE 태그**를 보고
정답 여부를 판정하므로, 씬 쪽에서 **정답 객체에 태그를 붙여줘야** 합니다.

### 요구사항

**난파선(정답으로 삼을 객체) 액터에 `wreck` 태그를 부여**하세요. 태그가 없으면 "배경(other)"으로 처리됩니다.

에디터: 액터 선택 → *Details* → *Actor* → **Tags** 배열에 `wreck` 추가

Python(커맨드렛) 예시:

```python
actor.set_editor_property("tags", ["wreck"])
```

### 동작 방식

```cpp
// RaycastSidescanSonar.cpp :: ShootRay
Detection.is_target = HitActor && HitActor->ActorHasTag(LabelTag);   // LabelTag 기본값 "wreck"
```

- 레이가 실제로 맞은 지점에만 라벨이 찍히므로 **가려짐·그림자·layover가 자동으로 반영**됩니다.
- 강도 워터폴과 **완전히 같은 레이/빈**에 누적되어 **픽셀 단위로 정합**됩니다.
- 결과물 폴더 구조 (강도와 마스크가 **같은 파일명**으로 1:1 대응):

```
data/images/<YYYYMMDD-HHMMSS>/
    raw/         워터폴 강도 (1 ping = 1 픽셀 줄)
    trueaspect/  실측 비율 리샘플본
    label/       ★ 이진 GT 마스크 (흰색=난파선, 검정=배경) — raw/ 와 동일 크기·정합
```

### 주의사항

- **한 난파선 = 한 액터**로 두는 것을 권장합니다. 여러 조각으로 나뉘면 **조각마다 전부** 태그가 필요합니다.
- 난파선 **잔해·파편**도 정답에 포함하려면 동일하게 태그를 붙이세요. (논문 기준: 선체 + 관련 debris를 "shipwreck"으로 라벨)
- 해저·암반·해초 등 **배경 객체에는 태그를 붙이지 마세요.**
- 태그 이름을 바꾸고 싶으면 취득 설정 `gt_label_tag` 로 변경 가능합니다(기본 `wreck`). 씬과 설정이 **반드시 일치**해야 합니다.
- **그림자 영역은 라벨에 포함되지 않습니다**(레이가 도달하지 않으므로). 이는 의도된 동작입니다.

---

## 4. 씬 구성 규약

### 4.1 좌표·스케일

- **UE 단위 cm, Z-up**. 취득 스크립트는 m 단위로 다루며 내부 변환합니다.
- 해저는 **z 음수 영역**에 두고, 수면을 z=0 으로 잡아주세요. (현재 파이프라인 가정: 수면 0, 해저 −20 m)

### 4.2 씬 범위를 `config.json` 에 정확히 기입

패키지의 `Content/Config/config.json`:

```json
{
  "name": "<패키지명>",
  "worlds": [
    {
      "name": "<월드명>",
      "pre_start_steps": 20,
      "env_min": [-170.0, -130.0, -25.0],
      "env_max": [ 170.0,  130.0,   5.0]
    }
  ]
}
```

- 취득 자동화(잔디깎이 경로 생성)가 이 범위를 참고할 수 있도록 **실제 지형 범위와 일치**시켜 주세요.
- 범위가 틀리면 조사 경로가 지형 밖으로 나가 **빈(검은) 데이터**가 생깁니다.

### 4.3 지형 기복 — 사전 고지 필요 ⚠️

현재 SSS 처리 파이프라인은 **평탄 해저(고정 고도)** 를 가정한 부분이 있습니다.

- slant→ground 보정: `ground = √(slant² − altitude²)` — 단일 고도 사용
- 관측거리(`range_max`) 자동 산출 — 단일 고도 기반

**지형에 기복이 있으면** 이 로직들이 어긋나므로, 소나 쪽에서 **ping별 고도 추정(first bottom return 검출)** 으로 개선하는 작업이 별도로 필요합니다.

→ **기복 지형을 만들 계획이면 미리 알려주세요.** (씬이 잘못된 게 아니라, 소나 쪽 대응 개발이 필요한 사안입니다.)

### 4.4 소나 이미지 품질을 위한 권장사항

- **메시 삼각형 밀도**: 소나 해상도(수 cm)보다 지나치게 성긴 메시는 계단현상을 만듭니다. 반대로 과도하게 조밀하면(수십만 tris) 레이 트레이싱이 느려집니다. 물체당 **수만 tris 수준**을 권장합니다.
- **표면 법선**: 입사각 계산에 직접 쓰이므로, 법선이 뒤집히거나 깨진 메시는 밝기가 이상해집니다.
- **두께 있는 지오메트리**: 종잇장처럼 얇은 면(단면 플레인)은 뒷면에서 법선이 반대라 부자연스럽습니다.
- **바닥에 묻힌 물체**: 실제 난파선처럼 일부를 해저에 묻으면 그림자·형상이 자연스럽습니다.

### 4.5 AzimuthRayCount — 동심원(concentric-ring) 에일리어싱 아티팩트 방지 ⚠️

`RaycastSidescanSonar`는 매 ping마다 부채꼴로 쏜 레이를 `RangeBins`개의 히스토그램 컬럼에 누적합니다.
레이 각도 간격(`AzimuthRayCount`)이 `RangeBins`에 비해 너무 성기면, **특정 range bin이 매 ping마다
실제 히트를 하나도 못 받는** 현상이 생깁니다. 이 bin들은 range-gain 정규화(컬럼별 중앙값으로 나누는 과정)
에서 노이즈만 증폭되어, 워터폴 이미지에 **정적인 동심원/호(弧) 무늬**로 나타납니다.

**실측 검증 결과** (`04_code/environment_data/run_khoa_environment_raycast_survey_v1.py`,
`RangeBins=1000`, `RangeMax` 50-60m, 고도 ~4.7m 기준):

| Azimuth | AzimuthRayCount | 레이 간격 | 결과 |
| --- | --- | --- | --- |
| 85° | 8500 | 0.0100°/ray | **동심원 아티팩트 확인됨** |
| 170° | 68000 | 0.0025°/ray | 아티팩트 없음, 클린 (확인됨) |

이 스크립트는 `--azimuth-ray-count`를 지정하지 않으면 `recommended_azimuth_ray_count()` 함수가
`0.0025°/ray` 기준(위 표의 검증된 안전값)을 `RangeBins`에 비례 스케일링해 자동 계산합니다. 이 스케일링은
**이론적으로는 타당하지만 두 지점 외에는 별도 검증되지 않은 외삽**이므로, `RangeMax`나 고도가 검증 구간
(50-60m / 4-5m)에서 크게 벗어나는 씬이라면 결과 이미지를 눈으로 확인해 동심원이 없는지 재검증하세요.

> 단순 평면-해저 기하(secant 모델)로 필요 레이 수를 이론적으로 추정하면 실측치보다 약 50배 적게
> 나옵니다 — 즉 순수 기하 계산만으로는 이 아티팩트를 예측할 수 없고, **실측 검증이 필수**입니다
> (아마도 AUV 메시의 SonarSocket 실제 장착 각도가 소스코드만으로는 알 수 없기 때문으로 추정).

---

## 5. 【중요】 엔진 소스 통합

### 5.1 상황

- HoloOcean 월드 패키지(zip)에는 **엔진 실행 바이너리가 통째로 포함**됩니다 (`Linux/Holodeck/Binaries/Linux/Holodeck`).
- 본 프로젝트의 SSS 기능은 **C++ 소스 수정**으로 구현되어 있어 **그 바이너리 안에 구워집니다.**

> 따라서 **순정 HoloOcean으로 쿠킹한 씬 패키지를 그대로 쓰면 SSS 기능이 존재하지 않습니다.**
> (양측 port/starboard 출력 없음 → 버퍼 크기 불일치 오류, 빔 지향성 없음)

### 5.2 반드시 함께 가져가야 하는 수정 파일

| 파일 | 내용 |
| --- | --- |
| `engine/Source/Holodeck/Sensors/Private/RaycastSidescanSonar.cpp` | 양측 히스토그램 완성, 빔 지향성 `BeamPattern2Way()`, viz |
| `engine/Source/Holodeck/Sensors/Public/RaycastSidescanSonar.h` | 위 선언 + `UseBeamPattern` |
| `engine/Source/Holodeck/General/Private/Octree.cpp` | `getMaterialName` 크래시 수정 |
| `engine/Content/Config/materials.csv` | 재질 물성 테이블 |
| `client/src/holoocean/sensors.py` | `data_shape = range_bins * 2` (양측 출력 대응) |

### 5.3 통합 절차 (한 번만)

1. 환경씬 제작 워크스페이스의 **엔진 소스 트리에 위 파일들을 적용**
2. 완성된 씬(.umap, Content)을 그 트리에 배치
3. **패치된 소스로 재쿠킹/패키징** → 이 패키지를 설치해 사용

> 씬을 수정할 때마다 반복할 일이 아니라, **소스 트리를 한 번 합치면 이후엔 콘텐츠 재쿠킹만** 하면 됩니다.

---

## 6. 인수인계 시 함께 전달해 주세요

- [ ] 월드 이름 / 패키지 이름
- [ ] 지형 범위 (`env_min`/`env_max`) 와 **해저 깊이**, 기복 유무·최대 고저차
- [ ] 사용한 **재질 목록** (애셋 이름) + 의도한 물성(모래/암반/펄 등)
- [ ] 해류·파도 등 **환경 설정 파라미터** (시나리오에 넣어야 하는 키)
- [ ] 소나에서 **무시되길 원하는 오브젝트** 목록 (장식용 등)
- [ ] `CTF_USE_COMPLEX_AS_SIMPLE` 적용 완료 여부
- [ ] **`wreck` 태그를 부여한 액터 목록** (GT 정답 대상) 및 태그 적용 완료 여부

---

## 7. 검증 방법 (씬 제작 쪽에서 자체 확인)

씬을 넘기기 전에 아래로 빠르게 확인할 수 있습니다.

```bash
# 1) 단일 직선 주행으로 워터폴 1장 뽑기 (빠름)
python3 scripts/acquire_sss.py ideal --survey single

# 2) 시각적으로 확인 (초록 부채꼴 = 소나 범위)
DISPLAY=:0 python3 scripts/view_survey.py ideal
```

**정상 신호**
- 재질이 다른 물체들의 **밝기가 서로 다르게** 나온다 → 재질 등록 OK
- 물체 뒤에 **검은 그림자**가 생긴다 → 콜리전/형상 OK
- 중앙에 **검은 나디르 밴드**가 있다 → 정상 기하
- `label/` 폴더의 마스크에서 **난파선 위치에만 흰 영역**이 있다 → 태그 OK
- 실행 로그에 `wreck 픽셀 N%` 가 **0이 아닌 값**으로 찍힌다 → GT 정상

**이상 신호**
- 모든 물체가 **똑같이 새하얗다** → `CTF_USE_COMPLEX_AS_SIMPLE` 누락 또는 재질 미등록
- 물체가 **박스 모양**으로 뭉개진다 → 단순 콜리전 사용 중
- 전 화면이 밝게 덮인다 → 물/수면이 소나 채널을 Block 하고 있음
- 실행 로그에 `Material ... not found in materials map` → 재질 미등록 (해당 이름 확인)
- **`wreck 픽셀 0.00%`** → 난파선 액터에 `wreck` 태그가 없음 (§3.5)
- 마스크에 **배경까지 하얗다** → 배경 객체에 태그가 잘못 붙어 있음
