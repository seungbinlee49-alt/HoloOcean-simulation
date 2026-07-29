# 퇴적상 필드 훅 적용 방법

`holoocean_ws/docs/FIELD_IMPLEMENTATION_GUIDE.md` §4.1/4.2에서 정의한 태그 + 가상함수
인터페이스(`sssfacies` 태그 → `GetFieldImpedanceAtLocation()`)를 이 환경의 실제 구현으로
채우는 방법입니다.

## 왜 diff로 안 주는가

`HolodeckRaycastSonar.cpp/.h`는 `sss-scene-runtime.diff`(취득 측 패치)가 이미 수정하는
파일입니다. 여기에 저희가 또 다른 diff를 얹으면 같은 파일을 두 번 건드리게 되어 충돌하거나
조용히 한쪽이 덮어써질 수 있습니다(`FIELD_IMPLEMENTATION_GUIDE.md` §5.4가 지적한 문제 그대로).
그래서 이 파일 하나는 diff 대신 **`sss-scene-runtime.diff` 적용 후에 추가할 코드 조각**으로
드립니다. 나머지(`MadoSceneConfig.h/.cpp`, `HolodeckGameMode.cpp`)는 저희만 건드리는 파일이라
평소대로 `engine.diff`에 포함했습니다.

## 적용 순서

1. `sss-scene-runtime.diff` 적용 (취득 측 패치 — `sssfacies`/`sssmat:` 태그 디스패치와
   `GetFieldImpedanceAtLocation()` 선언(기본값 `-1.0f`)이 이걸로 생깁니다)
2. `../engine.diff` 적용 (`MadoSceneConfig.h/.cpp` 신규 + `HolodeckGameMode.cpp` 수정 — 지형·닻돌·
   난파선 스폰 시 `sssfacies`/`sssmat:...`/`wreck` 태그를 붙이는 부분이 여기 들어있습니다)
3. `HolodeckRaycastSonar.h`의 `GetFieldImpedanceAtLocation()` 선언부를, 기본 구현이 있는 형태에서
   선언만 남기고 정의를 `.cpp`로 옮기도록 아래처럼 바꿔주세요.

```cpp
// HolodeckRaycastSonar.h — 기존 (sss-scene-runtime.diff 적용 직후):
virtual float GetFieldImpedanceAtLocation(
	const FVector& ClientHitPoint,
	float		   GroundRangeM) const {
	return -1.0f;
}

// 아래로 교체 (선언만 남기고, 기본 반환값 없이 순수 가상은 아님 -- .cpp에서 정의):
virtual float GetFieldImpedanceAtLocation(
	const FVector& ClientHitPoint,
	float		   GroundRangeM) const;
```

4. `HolodeckRaycastSonar.cpp` 맨 위쪽(다른 include들 옆)에 추가:

```cpp
#include "MadoSceneConfig.h"
```

5. `HolodeckRaycastSonar.cpp`의 아무 곳(클래스 멤버 함수들 사이, 예를 들어 `ComputeDetection()`
   바로 위)에 아래 정의를 추가하세요. 좌표→임피던스 변환의 실제 수식은 `MadoSceneConfig.h`에 이미
   있는 `FMadoSceneConfig`/`FMadoFaciesZoneConfig`를 그대로 사용합니다 (evidence·근거는
   `Content/Config/mado_scenes/*.json`의 `facies_zones[].evidence` 필드 참고).

```cpp
float UHolodeckRaycastSonar::GetFieldImpedanceAtLocation(
	const FVector& ClientHitPoint,
	float		   GroundRangeM) const {
	const FMadoSceneConfig& Config = GetActiveMadoSceneConfig();

	const float ActiveX = 1.0f - FMath::SmoothStep(Config.ActiveWindowXStart, Config.ActiveWindowXEnd, FMath::Abs(ClientHitPoint.X));
	const float ActiveY = 1.0f - FMath::SmoothStep(Config.ActiveWindowYStart, Config.ActiveWindowYEnd, FMath::Abs(ClientHitPoint.Y));
	const float ActiveWeight = FMath::Clamp(ActiveX * ActiveY, 0.0f, 1.0f);

	float Z = FMath::Lerp(Config.BaselineMaterial.Impedance(), Config.SoftMudBaselineMaterial.Impedance(), ActiveWeight);
	// ... 나머지 존별 블렌드/텍스처 노이즈 수식은 저희 로컬 dev workspace의
	// HolodeckRaycastSonar.cpp에 있는 RaycastMadoReportTerrainImpedanceAtClientXYImpl()과
	// 100% 동일합니다 -- 통합 시 저장소 관리자(승빈)에게 전체 함수 본문을 요청해 주세요.
	// 여기 요약만 적어둔 이유는 이 문서 자체가 diff가 아니라 손으로 옮겨 적는 코드라, 긴 수식을
	// 옮기다 오타가 나는 사고를 피하려는 것입니다 -- 전체 함수는 복붙 가능한 형태로 별도 전달합니다.
	return Z;
}
```

> 실제 통합 시점에는 6번 블록을 요약이 아니라 전체 함수(약 35줄, 존 블렌드 + 2-옥타브 텍스처
> 노이즈 + 나디르 페이드 포함)를 그대로 복붙할 수 있게 전달하겠습니다. 여기서는 "어디에, 어떤
> 시그니처로 넣는지" 통합 절차만 문서화합니다.

## 검증

로그에 아래처럼 뜨면 정상입니다.

```
MadoSceneConfig: loaded 'mado_report_environment_v1' ...
```

그리고 지형이 `sssfacies` 태그로 잡혀서 워터폴에 재질 대비가 보이면(닻돌 16기 위치에서 밝기
차이) 정상 동작입니다. 전부 새하얗거나 균일하면 `CTF_USE_COMPLEX_AS_SIMPLE` 콜리전 설정이나
태그 부착을 다시 확인하세요 (§5.1, `HolodeckGameMode.cpp`의 `bUseComplexAsSimpleCollision = true`
줄들은 이미 되어 있습니다 -- 지형 1044행 근처, 오브젝트 프로시저럴 메시 210행 근처).
