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
		if (ActorName.Contains(TEXT("ShipwreckProject_Seafloor"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("ShipwreckProject_SeabedProxy"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("KhoaSmoothBathymetryTerrain"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("ShipwreckProject_SeabedFeature"), ESearchCase::IgnoreCase)) {
			Detection.material_type = TEXT("ShipwreckProjectSeabed");
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
