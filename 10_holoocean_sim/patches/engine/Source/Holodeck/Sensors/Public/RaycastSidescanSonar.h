#pragma once
#include "Holodeck.h"
#include "HolodeckSensor.h"
#include "HolodeckRaycastSonar.h"
#include "MultivariateNormal.h"
#include "MultivariateUniform.h"
#include "RaycastSidescanSonar.generated.h"

class TestRaycastSidescanSonar;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class URaycastSidescanSonar : public UHolodeckRaycastSonar {
	GENERATED_BODY()

#if WITH_AUTOMATION_TESTS
	friend class TestRaycastSidescanSonar;
#endif

public:
	URaycastSidescanSonar();

	/**
	 * InitializeSensor
	 * Sets up the class
	 * is called after ParseSensorParams
	 */
	virtual void InitializeSensor() override;
	/**
	 * Parse parameters from JsonString
	 * Allowing parameters to be set dynamically
	 */
	virtual void ParseSensorParms(FString ParmsJson) override;

protected:
	virtual void TickSensorComponent(
		float						 DeltaTime,
		ELevelTick					 TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void showRegion(float DeltaTime) override;
	SonarDetection ShootRay(
		float				   VerticalAngle,
		float				   HorizontalAngle,
		const FTransform&	   ActorTransf,
		const FVector&		   SonarBodyLoc,
		const FRotator&		   ResultRot,
		FCollisionQueryParams& TraceParams);
	int GetNumItems() override { return RangeBins /* * 2 */; };
	int GetItemSize() override { return sizeof(float); };
	void FillAngleArrays();
	void ResetBuffer() override;
	void CopyHistogramsToBuffer();
	void SumHistograms();

private:
	/*
	 * Parent
	 * After initialization, Parent contains a pointer to whatever the sensor is
	 * attached to.
	 */
	TUniquePtr<AActor> Parent;

	UPROPERTY(EditAnywhere)
	float RangeRes = NAN;

	UPROPERTY(EditAnywhere)
	int AzimuthRayCount = NAN;
	UPROPERTY(EditAnywhere)
	float AzimuthRayRes = NAN;
	UPROPERTY(EditAnywhere)
	int ElevationRayCount = NAN;
	UPROPERTY(EditAnywhere)
	float ElevationRayRes = NAN;

	// 2nd lobe of hists since its sidescan
	// TArray<Histogram> leftHists;
	Histogram HistZero;
	// Histogram		  LeftHist;

	// Noise
	MultivariateNormal<1> addNoise;
	MultivariateNormal<1> multNoise;
};
