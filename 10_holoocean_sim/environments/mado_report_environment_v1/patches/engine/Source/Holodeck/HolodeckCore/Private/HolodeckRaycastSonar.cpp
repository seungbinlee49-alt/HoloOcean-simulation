// Written by Blake Romrell, FRoST Lab, BYU
#include "HolodeckRaycastSonar.h"
#pragma push_macro("check")
#undef check
#include "boost/histogram.hpp"
#undef check
#pragma pop_macro("check")
#include "Holodeck.h"
#include "Benchmarker.h"
#include "HolodeckBuoyantAgent.h"
#include "Conversion.h"

namespace {

float RaycastMadoReportEllipseScore(
	float X,
	float Y,
	float CenterX,
	float CenterY,
	float YawDeg,
	float RadiusX,
	float RadiusY);

bool RaycastMadoReportFaciesSceneActive() {
	static const bool bEnabled = []() {
		const FString ScenePreset =
			FPlatformMisc::GetEnvironmentVariable(TEXT("HOLOOCEAN_SHIPWRECK_SCENE_PRESET"));
		return ScenePreset.Equals(TEXT("mado_report_environment_v1"), ESearchCase::IgnoreCase) ||
			   ScenePreset.Equals(TEXT("mado_report_environment_visual_debug_v1"), ESearchCase::IgnoreCase);
	}();
	return bEnabled;
}

bool RaycastMadoReportHardFaciesOverlayEnabled() {
	static const bool bEnabled = []() {
		const FString EnvValue =
			FPlatformMisc::GetEnvironmentVariable(TEXT("HOLOOCEAN_SHIPWRECK_ENABLE_HARD_FACIES_ACOUSTIC_MAP"));
		return EnvValue == TEXT("1") || EnvValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}();
	return bEnabled;
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
// away from perfect ellipses and to add internal texture within a zone. RaycastHash01 alone is
// blocky/discontinuous per cell and not suitable for either use directly.
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
	float X,
	float Y,
	float CenterX,
	float CenterY,
	float YawDeg,
	float RadiusX,
	float RadiusY) {
	// Domain-warp the sample point before scoring against the ellipse, so the zone boundary
	// reads as an irregular, organic sediment-facies edge instead of a mathematically perfect
	// ellipse. Two independent noise samples (different offsets) avoid a simple radial wobble.
	const float WarpAmpX = RadiusX * 0.22f;
	const float WarpAmpY = RadiusY * 0.22f;
	const float WarpedX = X + (RaycastValueNoise(X, Y, 7.0f) - 0.5f) * 2.0f * WarpAmpX;
	const float WarpedY = Y + (RaycastValueNoise(X + 91.7f, Y + 57.3f, 7.0f) - 0.5f) * 2.0f * WarpAmpY;
	const float Score = RaycastMadoReportEllipseScore(WarpedX, WarpedY, CenterX, CenterY, YawDeg, RadiusX, RadiusY);
	return 1.0f - RaycastSmoothStep(0.35f, 1.60f, Score);
}

float RaycastBlendImpedance(float BaseZ, float TargetZ, float Weight, float Strength) {
	return FMath::Lerp(BaseZ, TargetZ, FMath::Clamp(Weight * Strength, 0.0f, 1.0f));
}

float RaycastMadoReportEllipseScore(
	float X,
	float Y,
	float CenterX,
	float CenterY,
	float YawDeg,
	float RadiusX,
	float RadiusY) {
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

bool RaycastInsideMadoReportEllipse(
	float X,
	float Y,
	float CenterX,
	float CenterY,
	float YawDeg,
	float RadiusX,
	float RadiusY) {
	return RaycastMadoReportEllipseScore(X, Y, CenterX, CenterY, YawDeg, RadiusX, RadiusY) <= 1.0f;
}

FString RaycastMadoReportTerrainFaciesMaterialAtClientXY(float X, float Y) {
	// Kept in sync with the active blend function's geometry (50x20m named blocks, 20x20m west
	// anomaly, 30x30m expanded east anomaly, 19-B/19-C split). This binary path only has 4
	// material buckets (vs. 7 in the blend path), so 19-B/19-C and plain 18-G/east-anomaly firm
	// mud all collapse to the single HardMud bucket here; only the west anomaly's real gravel
	// content earns HardMudGravel.
	if (RaycastInsideMadoReportEllipse(X, Y, -38.0f, 2.0f, 4.0f, 10.0f, 10.0f)) {
		return TEXT("ShipwreckProjectHardMudGravel");
	}
	// East anomaly, 19-B, 19-C, 18-G: report text describes only black/dark-gray hard mud or
	// rubble+shell mud, unlike the west anomaly's real gravel/river stones -> HardMud.
	if (RaycastInsideMadoReportEllipse(X, Y, 42.0f, -31.0f, -9.0f, 15.0f, 15.0f) ||
		RaycastInsideMadoReportEllipse(X, Y, -42.0f, 17.0f, -16.0f, 25.0f, 10.0f) ||
		RaycastInsideMadoReportEllipse(X, Y, -42.0f, 39.0f, -16.0f, 25.0f, 10.0f) ||
		RaycastInsideMadoReportEllipse(X, Y, 27.0f, -17.0f, 18.0f, 25.0f, 10.0f)) {
		return TEXT("ShipwreckProjectHardMud");
	}
	// WestSouthTransition zone removed here too: no report coordinate or measured survey
	// grid, only a general directional-trend sentence from the 19-A block description.
	if (RaycastInsideMadoReportEllipse(X, Y, 16.0f, -7.0f, 12.0f, 25.0f, 10.0f) ||
		RaycastInsideMadoReportEllipse(X, Y, 39.0f, 25.0f, -10.0f, 25.0f, 10.0f) ||
		RaycastInsideMadoReportEllipse(X, Y, -14.0f, -30.0f, 7.0f, 25.0f, 10.0f)) {
		return TEXT("ShipwreckProjectShellMud");
	}
	if (RaycastInsideMadoReportEllipse(X, Y, 0.0f, 0.0f, -4.0f, 56.0f, 46.0f)) {
		return TEXT("ShipwreckProjectSoftMud");
	}
	return TEXT("ShipwreckProjectSeabed");
}

FString RaycastMadoReportTerrainImpedanceMaterialAtClientXY(float X, float Y, float GroundRangeM) {
	// Target impedances are read directly from the primary source scan
	// (docs/presentation_assets/sources/APL_UW_TR9407_Table2_original_scan.png, Hamilton 1980
	// geoacoustic model as tabulated in APL-UW TR9407 Table 2, "Model inputs in terms of sediment
	// name"). Each zone below is mapped to the Hamilton grain-size row that best matches its
	// *surface* (top few cm -- the only depth an SSS raycast actually sees) description in the
	// 2021 report text, not its full sub-surface stratigraphy. Reference water values 1024
	// kg/m^3, 1480 m/s (same as the FineSand baseline derivation).
	constexpr float FineSandZ = 1298.0f * 1564.0f; // Mz 3.5 Very Fine Sand (rho 1.268, nu 1.0568)
	// Mz 7.5 Very Fine Silt (rho 1.147, nu 0.9837): report only says "무른 개흙" (soft mud) for
	// the 18-A~19-A general background, no shell/gravel mentioned -> finest/purest mud row.
	constexpr float SoftMudZ = 1175.0f * 1456.0f;
	// Mz 5.5 Medium Silt/Sand-Silt-Clay (rho 1.149, nu 0.9885): 18-F surface (top 20cm) is
	// "패각과 무른개흙" (shell + soft mud) -- moderate shell content, one step coarser than pure
	// soft mud.
	constexpr float ShellMud18FZ = 1177.0f * 1463.0f;
	// Mz 5.0 Sandy Silt/Gravelly Mud (rho 1.169, nu 0.9999): 18-H surface is "패각이 다수 함유된
	// 무른 개흙" (shell content explicitly denser/more abundant than 18-F) -> the coarsest row
	// still inside the mud family, whose own name ("Gravelly Mud") matches a shell-hash-rich mud.
	constexpr float ShellMud18HZ = 1197.0f * 1480.0f;
	// Mz 6.0 Sandy Mud (rho 1.149, nu 0.9873): 18-E surface (top ~1m) is a disturbed layer of
	// pine needles, shell, and modern refuse -- mixed but not shell-dominant like 18-H.
	constexpr float DisturbedShellMud18EZ = 1177.0f * 1461.0f;
	// Mz 6.5 Fine Silt/Clayey Silt (rho 1.148, nu 0.9861): plain "단단한 개흙" (firm mud) with no
	// shell/gravel mentioned -- used for both 18-G and the East anomaly, which the report
	// describes identically (black/dark-gray firm mud, no coarse component). Hamilton's table has
	// no separate firmness axis, so "firm" mud is represented by the same grain-size row as other
	// shell/gravel-free mud, not a heavier one.
	constexpr float HardMudPlainZ = 1176.0f * 1459.0f;
	// Mz 4.5 Coarse Silt (rho 1.195, nu 1.0179): 19-B/19-C are "할석+패각 섞인 단단한 개흙" (firm
	// mud mixed with broken rock rubble + shell) -- coarser than plain firm mud, but the rubble is
	// described as a minor admixture, not a gravel-dominant matrix, so this stops short of the
	// gravel rows used for the anomaly sites.
	constexpr float HardMud19BCZ = 1224.0f * 1506.0f;
	// Mz 1.0 Gravelly Muddy Sand (rho 2.151, nu 1.2241): West anomaly is explicitly "자갈+강돌
	// 섞인 단단한 개흙" (gravel + river-stone mixed into firm mud) -- real coarse clasts, but
	// still mud-matrix-dominant per the report text, not a pure gravel bed.
	constexpr float HardMudGravelWestZ = 2203.0f * 1812.0f;

	const float ActiveX = 1.0f - RaycastSmoothStep(60.0f, 72.0f, FMath::Abs(X));
	const float ActiveY = 1.0f - RaycastSmoothStep(50.0f, 62.0f, FMath::Abs(Y));
	const float ActiveWeight = FMath::Clamp(ActiveX * ActiveY, 0.0f, 1.0f);

	float Z = FMath::Lerp(FineSandZ, SoftMudZ, ActiveWeight);
	float MaxZoneWeight = ActiveWeight;

	// West anomaly (N36.41.17.9 E126.08.24.4, real 20x20m grid per report text) -> radius 10x10.
	const float WestAnomalyW = RaycastMadoZoneWeight(X, Y, -38.0f, 2.0f, 4.0f, 10.0f, 10.0f);
	// East anomaly (N36.41.18.3 E126.08.25.6, report text: grid expanded 10m each side from
	// 20x20m to a 30x30m grid after additional finds) -> radius 15x15.
	const float EastAnomalyW = RaycastMadoZoneWeight(X, Y, 42.0f, -31.0f, -9.0f, 15.0f, 15.0f);
	// WestSouthTransition zone removed: no report coordinate or measured survey grid, only a
	// general directional-trend sentence from the 19-A block description.
	// 19-B and 19-C are two separate 50x20m excavation blocks (report: "1개의 조사구역을
	// 50x20m으로 설정"), not one combined zone. The report does not give their relative layout,
	// so they are placed as two adjacent 50x20m rectangles around the same approximate center
	// used previously; only their existence/size/material is report-evidenced, not this exact
	// adjacency.
	const float NineteenBW = RaycastMadoZoneWeight(X, Y, -42.0f, 17.0f, -16.0f, 25.0f, 10.0f);
	const float NineteenCW = RaycastMadoZoneWeight(X, Y, -42.0f, 39.0f, -16.0f, 25.0f, 10.0f);
	// 18-F, 18-H, 18-E, 18-G are each a real 50x20m excavation block (same report sentence as
	// above) -> radius 25x10.
	const float HardMud18GW = RaycastMadoZoneWeight(X, Y, 27.0f, -17.0f, 18.0f, 25.0f, 10.0f);
	const float Shell18FW = RaycastMadoZoneWeight(X, Y, 16.0f, -7.0f, 12.0f, 25.0f, 10.0f);
	const float Shell18HW = RaycastMadoZoneWeight(X, Y, 39.0f, 25.0f, -10.0f, 25.0f, 10.0f);
	const float Disturbed18EW = RaycastMadoZoneWeight(X, Y, -14.0f, -30.0f, 7.0f, 25.0f, 10.0f);

	// Blend strength: 0.08-0.10 was too weak to register at all (measured, no detectable
	// contrast). 0.95 overshot the other way -- real SSS reference imagery shows the seabed as
	// fairly uniform fine grain, not big bright blobs; the things that visually dominate real
	// captures are actual 3D objects (wrecks, rocks) with sharp shadows, not sediment facies.
	// 0.35 keeps zones detectable (real, evidence-based contrast) without them visually
	// dominating the frame the way flat objects never should.
	constexpr float BlendStrength = 0.35f;
	Z = RaycastBlendImpedance(Z, ShellMud18FZ, Shell18FW, BlendStrength);
	Z = RaycastBlendImpedance(Z, ShellMud18HZ, Shell18HW, BlendStrength);
	Z = RaycastBlendImpedance(Z, DisturbedShellMud18EZ, Disturbed18EW, BlendStrength);
	Z = RaycastBlendImpedance(Z, HardMudPlainZ, HardMud18GW, BlendStrength);
	Z = RaycastBlendImpedance(Z, HardMud19BCZ, NineteenBW, BlendStrength);
	Z = RaycastBlendImpedance(Z, HardMud19BCZ, NineteenCW, BlendStrength);
	Z = RaycastBlendImpedance(Z, HardMudGravelWestZ, WestAnomalyW, BlendStrength);
	Z = RaycastBlendImpedance(Z, HardMudPlainZ, EastAnomalyW, BlendStrength);

	MaxZoneWeight = FMath::Max(MaxZoneWeight, WestAnomalyW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, EastAnomalyW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, NineteenBW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, NineteenCW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, HardMud18GW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, Shell18FW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, Shell18HW);
	MaxZoneWeight = FMath::Max(MaxZoneWeight, Disturbed18EW);
	// Two-octave smooth noise (fine mottling + coarser patchiness) for internal texture within
	// a zone, replacing the earlier single-cell hash (blocky, and amplitude was 0.04-0.1% --
	// essentially invisible). Real sediment facies are not uniform even within one class.
	// Cell size matters relative to the sensor's own resolution: at RangeBins=1000 over ~50m,
	// one pixel is ~5cm in range; a 1.6m noise cell used previously spanned ~32 range pixels x
	// ~8 along-track pixels, reading as blurry blotches rather than grain. 0.3m/1.0m keeps the
	// two-octave structure but at a scale that decorrelates over a handful of pixels, closer to
	// real backscatter speckle correlation length.
	const float FineNoise = RaycastValueNoise(X, Y, 0.3f);
	const float CoarseNoise = RaycastValueNoise(X * 0.3f + 13.0f, Y * 0.3f + 7.0f, 0.3f);
	const float TextureNoise = ((FineNoise * 0.6f + CoarseNoise * 0.4f) - 0.5f) * 2.0f;
	// Near nadir (small ground range from the sensor track), the slant-range-to-ground-range
	// mapping is nearly singular (d(ground_range)/d(slant_range) -> large as slant_range ->
	// altitude), so consecutive range bins jump across many texture-noise grid cells' worth of
	// world distance. Sampling a spatially-periodic noise field under that much foreshortening
	// aliases into a fixed "zone plate" concentric-ring pattern -- confirmed by it appearing
	// identically in the raw pre-TVG intensity regardless of terrain smoothing. Fading the
	// texture amplitude out near nadir removes the ill-conditioned sampling regime; it also
	// matches real SSS, where the near-nadir return is dominated by strong specular reflection,
	// not fine backscatter texture.
	const float NearNadirFade = RaycastSmoothStep(0.0f, 5.0f, GroundRangeM);
	const float TextureAmp = (0.03f + 0.05f * FMath::Clamp(MaxZoneWeight, 0.0f, 1.0f)) * NearNadirFade;
	Z *= 1.0f + TextureNoise * TextureAmp;

	return FString::Printf(TEXT("ShipwreckProjectImpedance_%d"), FMath::RoundToInt(Z));
}

} // namespace

FVector UHolodeckRaycastSonar::spherToEuc(
	float	   r,
	float	   theta,
	float	   phi,
	FTransform SensortoWorld) {
	float sinphi, cosphi;
	FMath::SinCos(&sinphi, &cosphi, FMath::DegreesToRadians(phi));
	float sintheta, costheta;
	FMath::SinCos(&sintheta, &costheta, FMath::DegreesToRadians(theta));

	float x = r * sinphi * costheta;
	float y = r * sinphi * sintheta;
	float z = r * cosphi;
	return UKismetMathLibrary::TransformLocation(SensortoWorld, FVector(x, y, z));
}

void UHolodeckRaycastSonar::ParseSensorParms(FString ParmsJson) {
	Super::ParseSensorParms(ParmsJson);

	TSharedPtr<FJsonObject>		   JsonParsed;
	TSharedRef<TJsonReader<TCHAR>> JsonReader =
		TJsonReaderFactory<TCHAR>::Create(ParmsJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonParsed)) {
		// Geometry Parameters
		if (JsonParsed->HasTypedField<EJson::Number>("RangeMin")) {
			RangeMin = JsonParsed->GetNumberField("RangeMin")
				* 100; // Convert from m to cm by multiplying by 100 (python uses m,
					   // UE/C++ uses cm)
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'RangeMin'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("RangeMax")) {
			RangeMax = JsonParsed->GetNumberField("RangeMax")
				* 100; // Convert from m to cm by multiplying by 100 (python uses m,
					   // UE/C++ uses cm)
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'RangeMax'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("Azimuth")) {
			Azimuth = JsonParsed->GetNumberField("Azimuth");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'Azimuth'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("Elevation")) {
			Elevation = JsonParsed->GetNumberField("Elevation");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'Elevation'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("RangeBins")) {
			RangeBins = JsonParsed->GetNumberField("RangeBins");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'RangeBins'."));
		}

		// Visualization Parameters
		if (JsonParsed->HasTypedField<EJson::Boolean>("ViewRegion")) {
			ViewRegion = JsonParsed->GetBoolField("ViewRegion");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'ViewRegion'."));
		}
		if (JsonParsed->HasTypedField<EJson::Boolean>("ViewRays")) {
			ViewRays = JsonParsed->GetBoolField("ViewRays");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'ViewRays'."));
		}
		if (JsonParsed->HasTypedField<EJson::Boolean>("ViewPoints")) {
			ViewPoints = JsonParsed->GetBoolField("ViewPoints");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'ViewPoints'."));
		}

		// Performance Parameters
		if (JsonParsed->HasTypedField<EJson::Number>("WaterDensity")) {
			WaterDensity = JsonParsed->GetNumberField("WaterDensity");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'WaterDensity'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("WaterSpeedSound")) {
			WaterSpeedSound = JsonParsed->GetNumberField("WaterSpeedSound");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'WaterSpeedSound'."));
		}

		// Misc Parameters
		if (JsonParsed->HasTypedField<EJson::Number>("TicksPerCapture")) {
			TicksPerCapture = JsonParsed->GetIntegerField("TicksPerCapture");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"UHolodeckRaycastSonar::ParseSensorParms:: Missing configuration for 'TicksPerCapture'."));
		}

		if (JsonParsed->HasTypedField<EJson::Boolean>("IgnorePlants")) {
			bIgnorePlants = JsonParsed->GetBoolField("IgnorePlants");
		}
	}

	WaterImpedance = WaterDensity * WaterSpeedSound;

	LoadMaterials();

	return;
}

void UHolodeckRaycastSonar::LoadMaterials() {
	// Load material lookup table
	FString filePath = FPaths::ProjectDir() + "../../materials.csv";
	if (!FPaths::FileExists(filePath)) {
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT("UHolodeckRaycastSonar::LoadMaterials:: File %s does not exist."),
			*filePath);
		return;
	}
	TArray<FString> lines;
	FFileHelper::LoadANSITextFileToStrings(*filePath, NULL, lines);
	for (int i = 1; i < lines.Num(); i++) {
		// Split line into elements
		TArray<FString> stringArray = {};
		lines[i].ParseIntoArray(stringArray, TEXT(","), false);
		// Put elements into lookup table
		FString key = stringArray[0];
		if (stringArray.Num() == 3) {
			float z = FCString::Atof(*stringArray[1]) * FCString::Atof(*stringArray[2]);
			Materials.Add(key, z);
		}
	}
}

float UHolodeckRaycastSonar::GetImpedanceFromMap(FString material) {
	float impedance;
	if (material.IsEmpty()) {
		return 10000.0f * 10000.0f;
	} else if (material.StartsWith(TEXT("ShipwreckProjectImpedance_"))) {
		const FString DynamicPrefix(TEXT("ShipwreckProjectImpedance_"));
		const FString ImpedanceText = material.RightChop(DynamicPrefix.Len());
		const float	  ParsedImpedance = FCString::Atof(*ImpedanceText);
		if (ParsedImpedance > 0.0f) {
			return ParsedImpedance;
		}
		return 10000.0f * 10000.0f;
	} else if (Materials.Find(material, impedance)) {
		return impedance;
	} else {
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT(
				"UHolodeckRaycastSonar::GetImpedanceFromMap:: Material %s not found in materials map."),
			*material);
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT(
				"UHolodeckRaycastSonar::GetImpedanceFromMap:: Using default large impedance."));
		// Add default line to material file to fill in later
		FString filePath = FPaths::ProjectDir() + "../../materials.csv";
		FString line = material + ", 10000, 10000\n";
		FFileHelper::SaveStringToFile(
			line,
			*filePath,
			FFileHelper::EEncodingOptions::AutoDetect,
			&IFileManager::Get(),
			EFileWrite::FILEWRITE_Append);

		float z = 10000 * 10000;
		Materials.Add(material, z);
		return z;
	}
}

void UHolodeckRaycastSonar::showRegion(float DeltaTime) {
	// draw outlines of our region
	if (ViewRegion) {
		FTransform tran = this->GetComponentTransform();
		float	   debugThickness = 3.0f;

		// range lines
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, minAzimuth, minElev, tran),
			spherToEuc(RangeMax, minAzimuth, minElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, minAzimuth, maxElev, tran),
			spherToEuc(RangeMax, minAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, maxAzimuth, minElev, tran),
			spherToEuc(RangeMax, maxAzimuth, minElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, maxAzimuth, maxElev, tran),
			spherToEuc(RangeMax, maxAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);

		// azimuth lines (should be arcs, we're being lazy)
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, minAzimuth, minElev, tran),
			spherToEuc(RangeMin, maxAzimuth, minElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, minAzimuth, maxElev, tran),
			spherToEuc(RangeMin, maxAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMax, minAzimuth, minElev, tran),
			spherToEuc(RangeMax, maxAzimuth, minElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMax, minAzimuth, maxElev, tran),
			spherToEuc(RangeMax, maxAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);

		// elevation lines (should be arcs, we're being lazy)
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, minAzimuth, minElev, tran),
			spherToEuc(RangeMin, minAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMin, maxAzimuth, minElev, tran),
			spherToEuc(RangeMin, maxAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMax, minAzimuth, minElev, tran),
			spherToEuc(RangeMax, minAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
		DrawDebugLine(
			GetWorld(),
			spherToEuc(RangeMax, maxAzimuth, minElev, tran),
			spherToEuc(RangeMax, maxAzimuth, maxElev, tran),
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture,
			ECC_WorldStatic,
			debugThickness);
	}
}

SonarDetection UHolodeckRaycastSonar::ComputeDetection(
	FHitResult		  HitInfo,
	const FTransform& SensorTransf) {
	// TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("HolodeckRaycastSonar::ComputeDetection"));
	SonarDetection Detection;
	const FVector  HitPoint = HitInfo.ImpactPoint;
	const FVector  VecInc = -(HitPoint - SensorTransf.GetLocation()).GetSafeNormal();
	Detection.distance = HitInfo.Distance;
	Detection.cos_inc_angle = FVector::DotProduct(VecInc, HitInfo.ImpactNormal);

	AActor* HitActor = HitInfo.GetActor();
	if (HitActor) {
		const FString ActorName = HitActor->GetName();
		if (ActorName.Contains(TEXT("MadoAnchorStone"), ESearchCase::IgnoreCase)) {
			Detection.material_type = TEXT("ShipwreckProjectAnchorStone");
			return Detection;
		}
		if (ActorName.Contains(TEXT("MadoReefRock"), ESearchCase::IgnoreCase)) {
			Detection.material_type = TEXT("ShipwreckProjectReefRock");
			return Detection;
		}
		if (ActorName.Contains(TEXT("ShipwreckProject_Seafloor"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("ShipwreckProject_SeabedProxy"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("KhoaSmoothBathymetryTerrain"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("ShipwreckProject_SeabedFeature"), ESearchCase::IgnoreCase)) {
			if (RaycastMadoReportFaciesSceneActive()
				&& (ActorName.Contains(TEXT("ShipwreckProject_SeabedProxy"), ESearchCase::IgnoreCase)
					|| ActorName.Contains(TEXT("KhoaSmoothBathymetryTerrain"), ESearchCase::IgnoreCase))) {
				const FVector ClientHitPoint = ConvertLinearVector(HitPoint, UEToClient);
				const FVector ClientSensorPoint = ConvertLinearVector(SensorTransf.GetLocation(), UEToClient);
				const float GroundRangeM = FVector::Dist2D(ClientHitPoint, ClientSensorPoint);
				Detection.material_type = RaycastMadoReportHardFaciesOverlayEnabled()
					? RaycastMadoReportTerrainFaciesMaterialAtClientXY(ClientHitPoint.X, ClientHitPoint.Y)
					: RaycastMadoReportTerrainImpedanceMaterialAtClientXY(ClientHitPoint.X, ClientHitPoint.Y, GroundRangeM);
			} else {
				Detection.material_type = TEXT("ShipwreckProjectSeabed");
			}
			return Detection;
		}
		if (ActorName.Contains(TEXT("SurveyWreck"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("TorpedoMesh_Target"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("TorpedoMesh_Only"), ESearchCase::IgnoreCase)) {
			Detection.material_type = TEXT("ShipwreckProjectWreck");
			return Detection;
		}
		if (ActorName.Contains(TEXT("GearRope"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("RockMound"), ESearchCase::IgnoreCase)) {
			Detection.material_type = TEXT("ShipwreckProjectClutter");
			return Detection;
		}
	}

	UPrimitiveComponent* HitComponent = HitInfo.GetComponent();
	if (HitComponent) {
		int32				SectionIndex = 0;
		UMaterialInterface* Material = HitComponent->GetMaterialFromCollisionFaceIndex(
			HitInfo.FaceIndex, SectionIndex);
		if (Material) {
			Detection.material_type = Material->GetName();
		} else {
			Detection.material_type = TEXT("Unknown");
		}
	}

	return Detection;
}

void UHolodeckRaycastSonar::ResetBuffer() {
	float* result = static_cast<float*>(Buffer);
	std::fill(result, result + RangeBins, 0);

	for (int i = 0; i < Hists.Num(); i++) {
		for (auto& cell : Hists[i]) {
			cell = sum_count();
		}
	}
}

void UHolodeckRaycastSonar::TickSensorComponent(
	float						 DeltaTime,
	ELevelTick					 TickType,
	FActorComponentTickFunction* ThisTickFunction) {
	// As parent class, this function will be called by derived class and should handle
	// tick count and showing the region But full operation is done in derived class
	if (TickCounter == 0) {
		showRegion(DeltaTime);
	}

	TickCounter++;
}
