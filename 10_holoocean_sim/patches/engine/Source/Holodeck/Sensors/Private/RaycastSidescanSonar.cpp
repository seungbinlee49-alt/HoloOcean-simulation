// MIT License (c) 2025 BYU FRoStLab see LICENSE file
#include "RaycastSidescanSonar.h"

#include "Holodeck.h"
#include "Benchmarker.h"
#include "HolodeckBuoyantAgent.h"
URaycastSidescanSonar::URaycastSidescanSonar() {
	SensorName = "RaycastSidescanSonar";
	PrimaryComponentTick.bCanEverTick = true;
}

void URaycastSidescanSonar::ParseSensorParms(FString ParmsJson) {
	/// For all default values please refer to the python class in the sensors.py file
	/// in the client folder

	// Parse any parent class parameters that were provided in the configuration
	// In this case this should be:
	//  - RangeMin
	//  - RangeMax
	//  - Azimuth
	//  - Elevation
	//  - RangeBins
	//  - ViewRegion
	//  - ViewRays
	//  - ViewPoints
	//  - WaterDensity
	//  - WaterSpeedSoun
	//  - TicksPerCapture (this is set by the Hz of the sensor, which is outside of the
	//  normal config block)
	Super::ParseSensorParms(ParmsJson);

	// Parse any parameters that were provided in the configuration
	TSharedPtr<FJsonObject>		   JsonParsed;
	TSharedRef<TJsonReader<TCHAR>> JsonReader =
		TJsonReaderFactory<TCHAR>::Create(ParmsJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonParsed)) {
		if (JsonParsed->HasTypedField<EJson::Number>("RangeRes")) {
			RangeRes = JsonParsed->GetNumberField("RangeRes") * 100; // m to cm
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for 'RangeRes'."));
		}
		// Noise Params
		if (JsonParsed->HasTypedField<EJson::Number>("AddSigma")) {
			addNoise.initSigma(JsonParsed->GetNumberField("AddSigma"));
		} else if (JsonParsed->HasTypedField<EJson::Number>("AddCov")) {
			addNoise.initCov(JsonParsed->GetNumberField("AddCov"));
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for either 'AddSigma' or 'AddCov'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("MultSigma")) {
			multNoise.initSigma(JsonParsed->GetNumberField("MultSigma"));
		} else if (JsonParsed->HasTypedField<EJson::Number>("MultCov")) {
			multNoise.initCov(JsonParsed->GetNumberField("MultCov"));
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for either 'MultSigma' or 'MultCov'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("AzimuthRayCount")) {
			AzimuthRayCount = JsonParsed->GetIntegerField("AzimuthRayCount");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for 'AzimuthRayCount'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("AzimuthRayRes")) {
			AzimuthRayRes = JsonParsed->GetNumberField("AzimuthRayRes");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for 'AzimuthRayRes'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("ElevationRayCount")) {
			ElevationRayCount = JsonParsed->GetIntegerField("ElevationRayCount");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for 'ElevationRayCount'."));
		}
		if (JsonParsed->HasTypedField<EJson::Number>("ElevationRayRes")) {
			ElevationRayRes = JsonParsed->GetNumberField("ElevationRayRes");
		} else {
			UE_LOG(
				LogHolodeck,
				Fatal,
				TEXT(
					"URaycastSidescanSonar::ParseSensorParms:: Missing configuration for 'ElevationRayRes'."));
		}
	} else {
		UE_LOG(
			LogHolodeck,
			Fatal,
			TEXT("URaycastSidescanSonar::ParseSensorParms:: Unable to parse json."));
	}
}

void URaycastSidescanSonar::InitializeSensor() {
	Super::InitializeSensor();

	// Setup angles
	minAzimuth = -Azimuth / 2;
	maxAzimuth = Azimuth / 2;
	minElev = -Elevation / 2;
	maxElev = Elevation / 2;

	Parent = TUniquePtr<AActor>(this->GetAttachmentRootActor());
	FillAngleArrays();
}

int min(int a, int b) {
	return (a < b) ? a : b;
}

void URaycastSidescanSonar::FillAngleArrays() {
	HorizontalAngles.Empty(FGenericPlatformMath::Min(AzimuthRayCount, 2147483647));
	VerticalAngles.Empty(FGenericPlatformMath::Min(ElevationRayCount, 2147483647));
	// Fill arrays with the horizontal and vertical angles at which rays will be cast
	if (AzimuthRayCount == 1) {
		HorizontalAngles.Add(0);
	} else {
		for (int i = 0; i < AzimuthRayCount - 1; i++) {
			HorizontalAngles.Add(minAzimuth + i * AzimuthRayRes);
		}
		HorizontalAngles.Add(maxAzimuth);
	}
	if (ElevationRayCount == 1) {
		VerticalAngles.Add(0);
	} else {
		for (int i = 0; i < ElevationRayCount - 1; i++) {
			VerticalAngles.Add(minElev + i * ElevationRayRes);
		}
		VerticalAngles.Add(maxElev);
	}
	Hists.Empty(ElevationRayCount);
	// leftHists.Empty(ElevationRayCount);

	for (int i = 0; i < ElevationRayCount; i++) {
		Hists.Emplace(bh::axis::regular<>(RangeBins, RangeMin, RangeMax));
		// leftHists.Emplace(bh::axis::regular<>(RangeBins, RangeMin, RangeMax));
	}
}

void URaycastSidescanSonar::ResetBuffer() {
	// Reset the histogram buffer
	float* result = static_cast<float*>(Buffer);
	// std::fill(result, result + RangeBins * 2, 0);
	std::fill(result, result + RangeBins, 0);

	ParallelFor(Hists.Num(), [&](int32 i) {
		for (auto& cell : Hists[i]) {
			cell.clear();
		}
		// for (auto& cell : leftHists[i]) {
		// 	cell.clear();
		// }
	});
}

SonarDetection URaycastSidescanSonar::ShootRay(
	const float			   VerticalAngle,
	const float			   HorizontalAngle,
	const FTransform&	   ActorTransf,
	const FVector&		   SonarBodyLoc,
	const FRotator&		   SonarBodyRot,
	FCollisionQueryParams& TraceParams) {
	// TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("RaycastSidescanSonar::ShootRay"));
	static thread_local FHitResult HitInfo(ForceInit);
	HitInfo.Reset();

	FRotator LaserRot(VerticalAngle, HorizontalAngle, 0);
	FRotator ResultRot = UKismetMathLibrary::ComposeRotators(LaserRot, SonarBodyRot);

	// const auto Range = RangeMax;
	FVector EndTrace =
		RangeMax * UKismetMathLibrary::GetForwardVector(ResultRot) + SonarBodyLoc;

	if (ViewRays) {
		LinesToDraw.Enqueue(EndTrace);
	}

	// TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("RaycastSidescanSonar::LineTraceSingleByChannel"));
	if (bIgnorePlants) {
		GetWorld()->LineTraceSingleByChannel(
			HitInfo,
			SonarBodyLoc,
			EndTrace,
			ECC_GameTraceChannel4,
			TraceParams,
			FCollisionResponseParams::DefaultResponseParam);
	} else {
		GetWorld()->LineTraceSingleByChannel(
			HitInfo,
			SonarBodyLoc,
			EndTrace,
			ECC_GameTraceChannel3,
			TraceParams,
			FCollisionResponseParams::DefaultResponseParam);
	}

	if (HitInfo.bBlockingHit) {
		if (ViewPoints) {
			PointsToDraw.Enqueue(HitInfo.ImpactPoint);
		}
		return ComputeDetection(HitInfo, ActorTransf);
	} else {
		return SonarDetection(0, 0, TEXT(""));
	}
}

void URaycastSidescanSonar::showRegion(float DeltaTime) {
	if (ViewRegion) {
		float	 debugThickness = 3.0f;
		float	 DebugNumSides = 12; // change later?
		float	 length = RangeMax;	 // length of cone in cm
		FRotator ResultRotation = UKismetMathLibrary::ComposeRotators(
			FRotator(90, 0, 0), GetComponentRotation());
		DrawDebugCone(
			GetWorld(),
			GetComponentLocation(),
			GetForwardVector(), // ResultRotation.RotateVector(GetForwardVector()),
			length,
			(Azimuth / 2) * PI / 180,
			(Elevation / 2) * PI / 180,
			DebugNumSides,
			FColor::Green,
			false,
			DeltaTime * TicksPerCapture * 1.1,
			ECC_WorldStatic,
			debugThickness);
	}
}

void URaycastSidescanSonar::TickSensorComponent(
	float						 DeltaTime,
	ELevelTick					 TickType,
	FActorComponentTickFunction* ThisTickFunction) {
	// TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("RaycastSidescanSonar::TickSensorComponent"));
	// UE_LOG(LogHolodeck, Warning,
	// TEXT("RaycastSidescanSonar::TickSensorComponent()"));
	Super::TickSensorComponent(DeltaTime, TickType, ThisTickFunction);

	if (TickCounter >= TicksPerCapture) {
		TickCounter -= TicksPerCapture;

		URaycastSidescanSonar::ResetBuffer();

		// Pre get some parameters
		const FTransform	  ActorTransf = this->GetComponentTransform();
		const FVector		  SonarBodyLoc = ActorTransf.GetLocation();
		const FRotator		  SonarBodyRot = ActorTransf.Rotator();
		// Setup the trace parameters
		// args are TraceTag, bInTraceComplex, and ActorToIgnore
		FCollisionQueryParams TraceParams(
			FName(TEXT("SonarTrace")), true, Parent.Get());
		TraceParams.bReturnFaceIndex = true;
		TraceParams.bTraceComplex = false;
		TraceParams.bReturnPhysicalMaterial = false;
		TraceParams.bIgnoreTouches = true;
		TraceParams.bFindInitialOverlaps = false;

		// For each vertical/elevation angle in the array
		// Start a parallel for, and for loop through each horizontal/azimuth angle
		ParallelFor(
			VerticalAngles.Num(),
			[&](int32 i) {
				for (auto h : HorizontalAngles) {
					SonarDetection Detection = ShootRay(
						VerticalAngles[i],
						h,
						ActorTransf,
						SonarBodyLoc,
						SonarBodyRot,
						TraceParams);
					float z = GetImpedanceFromMap(Detection.material_type);
					float r = (z - WaterImpedance) / (z + WaterImpedance);
					float val = r * r * Detection.cos_inc_angle;
					// if (h < 0) {
					// 	leftHists[i](bh::weight(val), Detection.distance);
					// } else {
					Hists[i](bh::weight(val), Detection.distance);
					// }
				}
			},
			EParallelForFlags::Unbalanced);
		if (ViewRays) {
			while (!LinesToDraw.IsEmpty()) {
				FVector Line;
				LinesToDraw.Dequeue(Line);
				DrawDebugLine(
					GetWorld(),
					GetComponentLocation(),
					Line,
					FColor::Green,
					false,
					DeltaTime * TicksPerCapture);
			}
		}
		if (ViewPoints) {
			while (!PointsToDraw.IsEmpty()) {
				FVector Point;
				PointsToDraw.Dequeue(Point);
				DrawDebugPoint(
					GetWorld(),
					Point,
					5,
					FColor::Red,
					false,
					DeltaTime * TicksPerCapture);
			}
		}

		SumHistograms();

		/// Get the left and right histograms as easier to use variables
		auto hist = HistZero;
		// auto histl = LeftHist;

		/// Move histogram data into the buffer
		float* result = static_cast<float*>(Buffer);
		for (int i = 0; i < RangeBins; i++) {
			if (hist.at(i).count() > 0) {
				result[i] = hist.at(i).sum() / hist.at(i).count();
				result[i] *= (1 + multNoise.sampleFloat());
				result[i] += addNoise.sampleRayleigh();
			} else {
				result[i] = addNoise.sampleRayleigh();
			}
			// lidx = RangeBins - i - 1;
			// if (histl.at(i).count() > 0) {
			// 	result[lidx] = histl.at(i).sum() / histl.at(i).count();
			// 	result[lidx] *= (1 + multNoise.sampleFloat());
			// 	result[lidx] += addNoise.sampleRayleigh();
			// } else {
			// 	result[lidx] = addNoise.sampleRayleigh();
			// }
		}
	}
}

void URaycastSidescanSonar::SumHistograms() {
	// TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("RaycastSidescanSonar::SumHistograms"));

	// Sum the parallel histograms
	HistZero = Hists[0];
	for (int i = 1; i < Hists.Num(); i++) {
		HistZero += Hists[i];
	}
	// LeftHist = leftHists[0];
	// for (int i = 1; i < leftHists.Num(); i++) {
	// 	LeftHist += leftHists[i];
	// }
}