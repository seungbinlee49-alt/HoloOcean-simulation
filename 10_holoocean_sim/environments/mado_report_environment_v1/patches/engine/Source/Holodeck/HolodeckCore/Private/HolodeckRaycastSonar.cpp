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
#include "MadoSceneConfig.h"

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
	return IsMadoReportSceneActive();
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
	float RadiusY,
	float DomainWarpCellSize = 7.0f,
	float DomainWarpFraction = 0.22f) {
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

// Config-driven replacement for the previous hardcoded-per-zone version: zone centers/radii,
// target impedances, blend strength, baseline materials, and texture-noise parameters all come
// from GetActiveMadoSceneConfig() (see MadoSceneConfig.h) instead of being C++ constants, so a
// new scene variant (or a parameter sweep of this one) is a JSON edit, not a rebuild. The
// underlying math (ellipse scoring, domain warp, two-octave texture noise, near-nadir fade) is
// unchanged from the hardcoded version -- only where the numbers come from changed. Each zone's
// Hamilton-table/report justification lives in the JSON file itself (per-zone "evidence" field),
// not in C++ comments anymore.
FString RaycastMadoReportTerrainImpedanceMaterialAtClientXY(float X, float Y, float GroundRangeM) {
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
	// zone. Cell size matters relative to the sensor's own resolution -- see the
	// texture_noise config block for the reasoning (kept in the JSON now, not here).
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
				Detection.material_type =
					RaycastMadoReportTerrainImpedanceMaterialAtClientXY(ClientHitPoint.X, ClientHitPoint.Y, GroundRangeM);
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
