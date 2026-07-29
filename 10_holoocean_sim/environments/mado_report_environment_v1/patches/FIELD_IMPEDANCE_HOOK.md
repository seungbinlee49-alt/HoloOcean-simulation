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

5. `HolodeckRaycastSonar.cpp`의 anonymous namespace(익명 네임스페이스, 파일 상단 `namespace { ... }`
   블록) 안에 아래 헬퍼 함수들을 추가하세요 -- 존 블렌드, domain-warp로 불규칙해진 타원 경계,
   2-옥타브 텍스처 노이즈, 나디르 페이드까지 저희 로컬 dev workspace 코드와 100% 동일합니다
   (그대로 복붙 가능). evidence·근거는 `Content/Config/mado_scenes/*.json`의
   `facies_zones[].evidence` 필드에 있습니다.

```cpp
namespace {

float RaycastMadoReportEllipseScore(
	float X, float Y, float CenterX, float CenterY, float YawDeg, float RadiusX, float RadiusY) {
	const float Dx = X - CenterX;
	const float Dy = Y - CenterY;
	const float Theta = FMath::DegreesToRadians(YawDeg);
	const float CosTheta = FMath::Cos(Theta);
	const float SinTheta = FMath::Sin(Theta);
	const float LocalX = Dx * CosTheta + Dy * SinTheta;
	const float LocalY = -Dx * SinTheta + Dy * CosTheta;
	const float Rx = FMath::Max(RadiusX, 0.001f);
	const float Ry = FMath::Max(RadiusY, 0.001f);
	return (LocalX * LocalX) / (Rx * Rx) + (LocalY * LocalY) / (Ry * Ry);
}

float RaycastSmoothStep(float Edge0, float Edge1, float X) {
	const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float RaycastHash01(float X, float Y) {
	const float CellX = FMath::FloorToFloat(X * 1.7f);
	const float CellY = FMath::FloorToFloat(Y * 1.7f);
	const float V = FMath::Sin(CellX * 12.9898f + CellY * 78.233f) * 43758.5453f;
	return FMath::Frac(FMath::Abs(V));
}

// Smooth (bilinearly-interpolated, continuous) value noise, used both to warp zone boundaries
// away from perfect ellipses and to add internal texture within a zone.
float RaycastValueNoise(float X, float Y, float CellSize) {
	const float GX = X / FMath::Max(CellSize, 0.001f);
	const float GY = Y / FMath::Max(CellSize, 0.001f);
	const float X0 = FMath::FloorToFloat(GX);
	const float Y0 = FMath::FloorToFloat(GY);
	const float TX = GX - X0;
	const float TY = GY - Y0;
	const float H00 = RaycastHash01(X0, Y0);
	const float H10 = RaycastHash01(X0 + 1.0f, Y0);
	const float H01 = RaycastHash01(X0, Y0 + 1.0f);
	const float H11 = RaycastHash01(X0 + 1.0f, Y0 + 1.0f);
	const float SX = RaycastSmoothStep(0.0f, 1.0f, TX);
	const float SY = RaycastSmoothStep(0.0f, 1.0f, TY);
	const float Top = FMath::Lerp(H00, H10, SX);
	const float Bottom = FMath::Lerp(H01, H11, SX);
	return FMath::Lerp(Top, Bottom, SY);
}

float RaycastMadoZoneWeight(
	float X, float Y, float CenterX, float CenterY, float YawDeg, float RadiusX, float RadiusY,
	float DomainWarpCellSize = 7.0f, float DomainWarpFraction = 0.22f) {
	// Domain-warp the sample point before scoring against the ellipse, so the zone boundary
	// reads as an irregular, organic sediment-facies edge instead of a mathematically perfect
	// ellipse. Two independent noise samples (different offsets) avoid a simple radial wobble.
	const float WarpAmpX = RadiusX * DomainWarpFraction;
	const float WarpAmpY = RadiusY * DomainWarpFraction;
	const float WarpedX = X + (RaycastValueNoise(X, Y, DomainWarpCellSize) - 0.5f) * 2.0f * WarpAmpX;
	const float WarpedY = Y + (RaycastValueNoise(X + 91.7f, Y + 57.3f, DomainWarpCellSize) - 0.5f) * 2.0f * WarpAmpY;
	const float Score = RaycastMadoReportEllipseScore(WarpedX, WarpedY, CenterX, CenterY, YawDeg, RadiusX, RadiusY);
	return 1.0f - RaycastSmoothStep(0.35f, 1.60f, Score);
}

float RaycastBlendImpedance(float BaseZ, float TargetZ, float Weight, float Strength) {
	return FMath::Lerp(BaseZ, TargetZ, FMath::Clamp(Weight * Strength, 0.0f, 1.0f));
}

// The actual field computation. Zone centers/radii, target impedances, blend strength, baseline
// materials, and texture-noise parameters all come from GetActiveMadoSceneConfig() (MadoSceneConfig.h),
// so a new scene variant is a JSON edit, not a rebuild.
float RaycastMadoReportTerrainImpedanceAtClientXYImpl(float X, float Y, float GroundRangeM) {
	const FMadoSceneConfig& Config = GetActiveMadoSceneConfig();

	const float ActiveX = 1.0f - RaycastSmoothStep(Config.ActiveWindowXStart, Config.ActiveWindowXEnd, FMath::Abs(X));
	const float ActiveY = 1.0f - RaycastSmoothStep(Config.ActiveWindowYStart, Config.ActiveWindowYEnd, FMath::Abs(Y));
	const float ActiveWeight = FMath::Clamp(ActiveX * ActiveY, 0.0f, 1.0f);

	float Z = FMath::Lerp(Config.BaselineMaterial.Impedance(), Config.SoftMudBaselineMaterial.Impedance(), ActiveWeight);
	float MaxZoneWeight = ActiveWeight;

	for (const FMadoFaciesZoneConfig& Zone : Config.FaciesZones) {
		const float Weight = RaycastMadoZoneWeight(
			X, Y, Zone.CenterX, Zone.CenterY, Zone.YawDeg, Zone.RadiusX, Zone.RadiusY,
			Config.DomainWarpCellSizeM, Config.DomainWarpFractionOfRadius);
		Z = RaycastBlendImpedance(Z, Zone.TargetMaterial.Impedance(), Weight, Config.BlendStrength);
		MaxZoneWeight = FMath::Max(MaxZoneWeight, Weight);
	}

	// Two-octave smooth noise (fine mottling + coarser patchiness) for internal texture within a
	// zone.
	const FMadoTextureNoiseConfig& T = Config.TextureNoise;
	const float FineNoise = RaycastValueNoise(X, Y, T.FineCellSizeM);
	const float CoarseNoise = RaycastValueNoise(X * T.CoarseFreqScale + 13.0f, Y * T.CoarseFreqScale + 7.0f, T.CoarseCellSizeM);
	const float TextureNoise = ((FineNoise * T.FineWeight + CoarseNoise * T.CoarseWeight) - 0.5f) * 2.0f;
	// Near nadir (small ground range from the sensor track), the slant-range-to-ground-range
	// mapping is nearly singular, so consecutive range bins jump across many texture-noise grid
	// cells' worth of world distance; sampling a spatially-periodic noise field under that much
	// foreshortening aliases into a "zone plate" concentric-ring pattern. Fading the texture
	// amplitude out near nadir removes the ill-conditioned sampling regime; it also matches real
	// SSS, where the near-nadir return is dominated by specular reflection, not fine texture.
	const float NearNadirFade = RaycastSmoothStep(T.NearNadirFadeStartM, T.NearNadirFadeEndM, GroundRangeM);
	const float TextureAmp = (T.AmpBase + T.AmpZoneWeightScale * FMath::Clamp(MaxZoneWeight, 0.0f, 1.0f)) * NearNadirFade;
	Z *= 1.0f + TextureNoise * TextureAmp;

	return Z;
}

} // namespace

float UHolodeckRaycastSonar::GetFieldImpedanceAtLocation(
	const FVector& ClientHitPoint,
	float		   GroundRangeM) const {
	// Only the Mado report/district facies field is implemented right now; GetActiveMadoSceneConfig()
	// already resolves the correct scene from HOLOOCEAN_SHIPWRECK_SCENE_PRESET, so no per-scene
	// branching is needed here even though this environment repo ships two scenes (District I/II).
	return RaycastMadoReportTerrainImpedanceAtClientXYImpl(ClientHitPoint.X, ClientHitPoint.Y, GroundRangeM);
}
```

이게 전체입니다 -- 요약이 아니라 그대로 복붙해서 쓰시면 됩니다. `HolodeckRaycastSonar.cpp`가 이미
`namespace { ... }` 블록을 갖고 있다면 그 안에 헬퍼들을 합치고, `GetFieldImpedanceAtLocation` 정의는
네임스페이스 밖(파일 스코프)에 두세요.

## 검증

로그에 아래처럼 뜨면 정상입니다.

```
MadoSceneConfig: loaded 'mado_report_environment_v1' ...
```

그리고 지형이 `sssfacies` 태그로 잡혀서 워터폴에 재질 대비가 보이면(닻돌 16기 위치에서 밝기
차이) 정상 동작입니다. 전부 새하얗거나 균일하면 `CTF_USE_COMPLEX_AS_SIMPLE` 콜리전 설정이나
태그 부착을 다시 확인하세요 (§5.1, `HolodeckGameMode.cpp`의 `bUseComplexAsSimpleCollision = true`
줄들은 이미 되어 있습니다 -- 지형 1044행 근처, 오브젝트 프로시저럴 메시 210행 근처).
