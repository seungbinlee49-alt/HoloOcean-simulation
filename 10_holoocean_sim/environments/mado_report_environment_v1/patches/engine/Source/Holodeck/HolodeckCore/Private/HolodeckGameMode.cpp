// Originally Written by joshgreaves.
// Modified by romrellb in 2026

#include "HolodeckGameMode.h"
#include "Holodeck.h"
#include "HolodeckGPUSonar.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Conversion.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"

#include "ShipwreckKhoaSmoothTerrainData.generated.h"
#include "MadoSceneConfig.h"

const char RESET_KEY[] = "RESET";
const int  RESET_BYTES = 1;

AHolodeckGameMode* AHolodeckGameMode::SIMMODE = nullptr;

namespace {

FVector ShipwreckProjectLocation(float X, float Y, float Z) {
	return ConvertLinearVector(FVector(X, Y, Z), ClientToUE);
}

FRotator ShipwreckProjectRotation(float PitchDeg, float YawDeg, float RollDeg) {
	return ConvertAngularVector(FRotator(PitchDeg, YawDeg, RollDeg), ClientToUE);
}

float ReadShipwreckProjectFloatEnv(const TCHAR* Name, float DefaultValue) {
	const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
	return Value.IsEmpty() ? DefaultValue : FCString::Atof(*Value);
}

bool IsShipwreckProjectFlagEnabled(const TCHAR* EnvName, const TCHAR* CommandLineParam) {
	const FString EnvValue = FPlatformMisc::GetEnvironmentVariable(EnvName);
	return EnvValue == TEXT("1") || EnvValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
		   FParse::Param(FCommandLine::Get(), CommandLineParam);
}

float ShipwreckProjectKhoaDepthAt(float X, float Y) {
	const FString TerrainProfile =
		FPlatformMisc::GetEnvironmentVariable(TEXT("HOLOOCEAN_SHIPWRECK_TERRAIN_PROFILE"));
	if (TerrainProfile.Equals(TEXT("flat"), ESearchCase::IgnoreCase)) {
		return ReadShipwreckProjectFloatEnv(TEXT("HOLOOCEAN_SHIPWRECK_FLAT_DEPTH_M"), 8.18f);
	}

	constexpr int32 GridX = ShipwreckKhoaSmoothTerrainData::GridX;
	constexpr int32 GridY = ShipwreckKhoaSmoothTerrainData::GridY;
	const float U = (X - ShipwreckKhoaSmoothTerrainData::XMinM) /
		(ShipwreckKhoaSmoothTerrainData::XMaxM - ShipwreckKhoaSmoothTerrainData::XMinM) * (GridX - 1);
	const float V = (Y - ShipwreckKhoaSmoothTerrainData::YMinM) /
		(ShipwreckKhoaSmoothTerrainData::YMaxM - ShipwreckKhoaSmoothTerrainData::YMinM) * (GridY - 1);
	const int32 Ix0 = FMath::Clamp(FMath::FloorToInt(U), 0, GridX - 2);
	const int32 Iy0 = FMath::Clamp(FMath::FloorToInt(V), 0, GridY - 2);
	const int32 Ix1 = Ix0 + 1;
	const int32 Iy1 = Iy0 + 1;
	const float Tx = FMath::Clamp(U - Ix0, 0.0f, 1.0f);
	const float Ty = FMath::Clamp(V - Iy0, 0.0f, 1.0f);

	const float D00 = ShipwreckKhoaSmoothTerrainData::DepthM[Iy0 * GridX + Ix0];
	const float D10 = ShipwreckKhoaSmoothTerrainData::DepthM[Iy0 * GridX + Ix1];
	const float D01 = ShipwreckKhoaSmoothTerrainData::DepthM[Iy1 * GridX + Ix0];
	const float D11 = ShipwreckKhoaSmoothTerrainData::DepthM[Iy1 * GridX + Ix1];
	const float D0 = FMath::Lerp(D00, D10, Tx);
	const float D1 = FMath::Lerp(D01, D11, Tx);
	return FMath::Lerp(D0, D1, Ty);
}

float ShipwreckProjectKhoaTerrainZAt(float X, float Y) {
	return -ShipwreckProjectKhoaDepthAt(X, Y);
}

FVector ShipwreckProjectLocalLocation(float LocalX, float LocalY, float Z, float BaseX, float BaseY, float YawDeg) {
	const float Theta = FMath::DegreesToRadians(YawDeg);
	return ShipwreckProjectLocation(
		BaseX + LocalX * FMath::Cos(Theta) - LocalY * FMath::Sin(Theta),
		BaseY + LocalX * FMath::Sin(Theta) + LocalY * FMath::Cos(Theta),
		Z);
}

AStaticMeshActor* SpawnShipwreckProjectBox(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Label,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale) {
	if (!World || !Cube) {
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*Label);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		Location,
		Rotation,
		SpawnParams);
	if (!Actor) {
		return nullptr;
	}

	Actor->Tags.Add(FName(TEXT("ShipwreckProject")));
	Actor->Tags.Add(FName(*Label));
	Actor->SetActorScale3D(Scale);
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	if (MeshComponent) {
		MeshComponent->SetStaticMesh(Cube);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComponent->SetMobility(EComponentMobility::Static);
	}
	return Actor;
}

FName ShipwreckProjectAcousticMaterialNameForLabel(const FString& Label) {
	if (Label.Contains(TEXT("MadoAnchorStone"), ESearchCase::IgnoreCase)) {
		return FName(TEXT("ShipwreckProjectAnchorStone"));
	}
	if (Label.Contains(TEXT("MadoReefRock"), ESearchCase::IgnoreCase)) {
		return FName(TEXT("ShipwreckProjectReefRock"));
	}
	if (Label.Contains(TEXT("SurveyWreck"), ESearchCase::IgnoreCase)
		|| Label.Contains(TEXT("TorpedoMesh_Target"), ESearchCase::IgnoreCase)
		|| Label.Contains(TEXT("TorpedoMesh_Only"), ESearchCase::IgnoreCase)) {
		return FName(TEXT("ShipwreckProjectWreck"));
	}
	if (Label.Contains(TEXT("GearRope"), ESearchCase::IgnoreCase)
		|| Label.Contains(TEXT("RockMound"), ESearchCase::IgnoreCase)) {
		return FName(TEXT("ShipwreckProjectClutter"));
	}
	return FName(TEXT("ShipwreckProjectSeabed"));
}

void ShipwreckProjectApplyBasicColor(
	UMeshComponent* MeshComponent,
	const FLinearColor& Color,
	FName AcousticMaterialName = FName(TEXT("ShipwreckProjectSeabed"))) {
	if (!MeshComponent) {
		return;
	}
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial) {
		UMaterialInstanceDynamic* DynMaterial =
			UMaterialInstanceDynamic::Create(BaseMaterial, MeshComponent, AcousticMaterialName);
		if (DynMaterial) {
			DynMaterial->SetVectorParameterValue(TEXT("Color"), Color);
			MeshComponent->SetMaterial(0, DynMaterial);
		}
	}
}

void ShipwreckProjectApplyActorColor(AStaticMeshActor* Actor, const FLinearColor& Color) {
	if (!Actor) {
		return;
	}
	ShipwreckProjectApplyBasicColor(
		Actor->GetStaticMeshComponent(),
		Color,
		ShipwreckProjectAcousticMaterialNameForLabel(Actor->GetName()));
}

FVector2D ShipwreckProjectRotate2D(float X, float Y, float YawDeg) {
	const float Theta = FMath::DegreesToRadians(YawDeg);
	return FVector2D(
		X * FMath::Cos(Theta) - Y * FMath::Sin(Theta),
		X * FMath::Sin(Theta) + Y * FMath::Cos(Theta));
}

void ShipwreckProjectComputeNormals(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, TArray<FVector>& Normals) {
	Normals.Init(FVector::ZeroVector, Vertices.Num());
	for (int32 TriIndex = 0; TriIndex + 2 < Triangles.Num(); TriIndex += 3) {
		const int32 A = Triangles[TriIndex];
		const int32 B = Triangles[TriIndex + 1];
		const int32 C = Triangles[TriIndex + 2];
		if (!Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B) || !Vertices.IsValidIndex(C)) {
			continue;
		}
		const FVector FaceNormal = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
		Normals[A] += FaceNormal;
		Normals[B] += FaceNormal;
		Normals[C] += FaceNormal;
	}
	for (FVector& Normal : Normals) {
		Normal = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	}
}

AActor* SpawnShipwreckProjectProceduralMesh(
	UWorld* World,
	const FString& Label,
	const TArray<FVector>& ClientVertices,
	const TArray<int32>& Triangles,
	const FLinearColor& Color) {
	if (!World || ClientVertices.Num() == 0 || Triangles.Num() == 0) {
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*Label);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!Actor) {
		return nullptr;
	}
	Actor->Tags.Add(FName(TEXT("ShipwreckProject")));
	Actor->Tags.Add(FName(TEXT("MadoReportEnvironment")));
	Actor->Tags.Add(FName(*Label));

	UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(Actor, *FString::Printf(TEXT("%sMesh"), *Label));
	if (!MeshComp) {
		return Actor;
	}
	Actor->SetRootComponent(MeshComp);
	MeshComp->RegisterComponent();
	MeshComp->SetMobility(EComponentMobility::Static);
	MeshComp->bUseComplexAsSimpleCollision = true;

	TArray<FVector> Vertices;
	Vertices.Reserve(ClientVertices.Num());
	for (const FVector& Vertex : ClientVertices) {
		Vertices.Add(ShipwreckProjectLocation(Vertex.X, Vertex.Y, Vertex.Z));
	}

	TArray<FVector> Normals;
	ShipwreckProjectComputeNormals(Vertices, Triangles, Normals);

	TArray<FVector2D> UV0;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	UV0.Reserve(Vertices.Num());
	VertexColors.Reserve(Vertices.Num());
	Tangents.Reserve(Vertices.Num());
	for (int32 Index = 0; Index < Vertices.Num(); ++Index) {
		UV0.Add(FVector2D(0.0f, 0.0f));
		VertexColors.Add(Color);
		Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
	}

	MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);
	ShipwreckProjectApplyBasicColor(MeshComp, Color, ShipwreckProjectAcousticMaterialNameForLabel(Label));
	MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
	MeshComp->SetCollisionObjectType(ECollisionChannel::ECC_WorldStatic);
	MeshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetGenerateOverlapEvents(false);
	MeshComp->RecreatePhysicsState();
	MeshComp->UpdateBounds();
	Actor->SetActorEnableCollision(true);
	return Actor;
}

void ShipwreckProjectSnapActorBottomToTerrainZ(
	AActor* Actor,
	float TerrainZClient,
	float ClearanceM,
	const FString& Label) {
	if (!Actor) {
		return;
	}

	FVector OriginUE;
	FVector ExtentUE;
	Actor->GetActorBounds(true, OriginUE, ExtentUE);
	const float BottomZUE = OriginUE.Z - ExtentUE.Z;
	const float TargetBottomZUE = ShipwreckProjectLocation(0.0f, 0.0f, TerrainZClient + ClearanceM).Z;
	FVector ActorLocationUE = Actor->GetActorLocation();
	ActorLocationUE.Z += TargetBottomZUE - BottomZUE;
	Actor->SetActorLocation(ActorLocationUE);

	FVector SnappedOriginUE;
	FVector SnappedExtentUE;
	Actor->GetActorBounds(true, SnappedOriginUE, SnappedExtentUE);
	const float SnappedBottomClient =
		ConvertLinearVector(FVector(0.0f, 0.0f, SnappedOriginUE.Z - SnappedExtentUE.Z), UEToClient).Z;
	UE_LOG(
		LogHolodeck,
		Verbose,
		TEXT("ShipwreckProject contact snap: %s terrain_z=%.3f bottom_z=%.3f clearance=%.3f"),
		*Label,
		TerrainZClient,
		SnappedBottomClient,
		SnappedBottomClient - TerrainZClient);
}

AStaticMeshActor* SpawnShipwreckProjectGroundContactBox(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Label,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale,
	float TerrainZClient,
	float ClearanceM = 0.015f) {
	AStaticMeshActor* Actor = SpawnShipwreckProjectBox(World, Cube, Label, Location, Rotation, Scale);
	ShipwreckProjectSnapActorBottomToTerrainZ(Actor, TerrainZClient, ClearanceM, Label);
	return Actor;
}

AStaticMeshActor* SpawnShipwreckProjectGroundContactBox(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Label,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale,
	float TerrainZClient,
	float ClearanceM,
	const FLinearColor& Color) {
	AStaticMeshActor* Actor =
		SpawnShipwreckProjectGroundContactBox(World, Cube, Label, Location, Rotation, Scale, TerrainZClient, ClearanceM);
	ShipwreckProjectApplyActorColor(Actor, Color);
	return Actor;
}

AStaticMeshActor* SpawnShipwreckProjectGuideBox(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Label,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale,
	const FLinearColor& Color) {
	if (!World || !Cube) {
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*Label);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		Location,
		Rotation,
		SpawnParams);
	if (!Actor) {
		return nullptr;
	}

	Actor->Tags.Add(FName(TEXT("ShipwreckProject")));
	Actor->Tags.Add(FName(TEXT("ShipwreckProjectVisualDebugGuide")));
	Actor->Tags.Add(FName(*Label));
	Actor->SetActorScale3D(Scale);

	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	if (MeshComponent) {
		MeshComponent->SetStaticMesh(Cube);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetMobility(EComponentMobility::Static);

		UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (BaseMaterial) {
			UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, MeshComponent);
			if (DynMaterial) {
				DynMaterial->SetVectorParameterValue(TEXT("Color"), Color);
				MeshComponent->SetMaterial(0, DynMaterial);
			}
		}
	}
	return Actor;
}

void SpawnShipwreckProjectMeshActor(
	UWorld* World,
	UStaticMesh* Mesh,
	const FString& Label,
	float X,
	float Y,
	float Z,
	float YawDeg,
	const FVector& Scale) {
	if (!World || !Mesh) {
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*Label);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		ShipwreckProjectLocation(X, Y, Z),
		ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
		SpawnParams);
	if (!Actor) {
		return;
	}

	Actor->Tags.Add(FName(TEXT("ShipwreckProject")));
	Actor->Tags.Add(FName(*Label));
	Actor->SetActorScale3D(Scale);
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	if (MeshComponent) {
		MeshComponent->SetStaticMesh(Mesh);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComponent->SetMobility(EComponentMobility::Static);
	}
}

void SpawnShipwreckProjectIntactWreck(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Prefix,
	float BaseX,
	float BaseY,
	float BaseZ,
	float YawDeg) {
	ShipwreckProjectSnapActorBottomToTerrainZ(
		SpawnShipwreckProjectBox(
			World,
			Cube,
			Prefix + TEXT("_MainHull"),
			ShipwreckProjectLocalLocation(0.0f, 0.0f, BaseZ + 0.38f, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
			FVector(13.5f, 3.2f, 0.74f)),
		BaseZ,
		0.015f,
		Prefix + TEXT("_MainHull"));
	SpawnShipwreckProjectBox(
		World,
		Cube,
		Prefix + TEXT("_Keel"),
		ShipwreckProjectLocalLocation(0.0f, 0.0f, BaseZ + 1.10f, BaseX, BaseY, YawDeg),
		ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
		FVector(13.8f, 0.18f, 0.25f));
	SpawnShipwreckProjectBox(
		World,
		Cube,
		Prefix + TEXT("_PortRail"),
		ShipwreckProjectLocalLocation(0.0f, -2.05f, BaseZ + 0.90f, BaseX, BaseY, YawDeg),
		ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
		FVector(13.4f, 0.20f, 0.42f));
	SpawnShipwreckProjectBox(
		World,
		Cube,
		Prefix + TEXT("_StarboardRail"),
		ShipwreckProjectLocalLocation(0.0f, 2.05f, BaseZ + 0.90f, BaseX, BaseY, YawDeg),
		ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
		FVector(13.4f, 0.20f, 0.42f));
	for (int32 Index = 0; Index < 7; ++Index) {
		const float X = -5.4f + Index * 1.8f;
		SpawnShipwreckProjectBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_Rib_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(X, 0.0f, BaseZ + 1.18f, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + 90.0f, 0.0f),
			FVector(4.0f, 0.18f, 0.35f));
	}
}

void SpawnShipwreckProjectBuriedWreck(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Prefix,
	float BaseX,
	float BaseY,
	float BaseZ,
	float YawDeg) {
	SpawnShipwreckProjectBox(
		World,
		Cube,
		Prefix + TEXT("_SedimentBlanket"),
		ShipwreckProjectLocalLocation(0.0f, 0.0f, BaseZ + 0.22f, BaseX, BaseY, YawDeg),
		ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
		FVector(12.5f, 4.4f, 0.78f));
	for (int32 Index = 0; Index < 6; ++Index) {
		const float X = -5.0f + Index * 2.0f;
		const float Y = (Index % 2 == 0) ? -1.25f : 1.25f;
		SpawnShipwreckProjectBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_ExposedTimber_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(X, Y, BaseZ + 0.78f + 0.04f * (Index % 2), BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + 90.0f + (Index - 2) * 3.0f, 0.0f),
			FVector(3.0f, 0.18f, 0.24f));
	}
}

void SpawnShipwreckProjectFragmentedWreck(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Prefix,
	float BaseX,
	float BaseY,
	float BaseZ,
	float YawDeg) {
	const float Xs[] = {-5.8f, -3.0f, -0.5f, 2.4f, 5.0f};
	const float Ys[] = {-1.6f, 1.3f, -0.8f, 1.7f, -1.1f};
	const float Yaws[] = {30.0f, 105.0f, -18.0f, 70.0f, -55.0f};
	const float Lens[] = {5.6f, 4.1f, 6.0f, 3.8f, 4.7f};
	for (int32 Index = 0; Index < 5; ++Index) {
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_Fragment_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(Xs[Index], Ys[Index], BaseZ + 0.55f + 0.08f * (Index % 3), BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + Yaws[Index], 0.0f),
			FVector(Lens[Index], 0.46f, 0.52f),
			BaseZ);
	}
	SpawnShipwreckProjectGroundContactBox(
		World,
		Cube,
		Prefix + TEXT("_LowHullPatch"),
		ShipwreckProjectLocalLocation(0.3f, 0.1f, BaseZ + 0.24f, BaseX, BaseY, YawDeg),
		ShipwreckProjectRotation(0.0f, YawDeg + 8.0f, 0.0f),
		FVector(8.0f, 2.0f, 0.24f),
		BaseZ);
}

void SpawnShipwreckProjectLowReliefWreck(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Prefix,
	float BaseX,
	float BaseY,
	float BaseZ,
	float YawDeg) {
	for (int32 Index = 0; Index < 8; ++Index) {
		const float X = -5.8f + Index * 1.65f;
		const float Y = (Index % 2 == 0) ? -0.9f : 0.9f;
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_LowPlank_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(X, Y, BaseZ + 0.11f, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + ((Index % 3) - 1) * 8.0f, 0.0f),
			FVector(4.6f, 0.24f, 0.10f),
			BaseZ);
	}
	SpawnShipwreckProjectGroundContactBox(
		World,
		Cube,
		Prefix + TEXT("_MudMat"),
		ShipwreckProjectLocalLocation(0.0f, 0.0f, BaseZ + 0.05f, BaseX, BaseY, YawDeg),
		ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
		FVector(11.5f, 3.7f, 0.09f),
		BaseZ,
		0.005f);
}

void SpawnShipwreckProjectRibFieldWreck(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Prefix,
	float BaseX,
	float BaseY,
	float BaseZ,
	float YawDeg) {
	for (int32 Index = 0; Index < 12; ++Index) {
		const float X = -6.0f + Index * 1.1f;
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_Rib_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(X, (Index % 2 == 0) ? -0.2f : 0.2f, BaseZ + 0.62f, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + 90.0f + ((Index % 3) - 1) * 6.0f, 0.0f),
			FVector(4.0f, 0.22f, 0.50f),
			BaseZ);
	}
	for (int32 Index = 0; Index < 3; ++Index) {
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_Longitudinal_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(0.0f, -0.75f + Index * 0.75f, BaseZ + 0.36f, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + (Index - 1) * 3.0f, 0.0f),
			FVector(12.8f, 0.22f, 0.26f),
			BaseZ);
	}
}

void SpawnShipwreckProjectDebrisFieldWreck(
	UWorld* World,
	UStaticMesh* Cube,
	const FString& Prefix,
	float BaseX,
	float BaseY,
	float BaseZ,
	float YawDeg) {
	SpawnShipwreckProjectFragmentedWreck(World, Cube, Prefix + TEXT("_Timber"), BaseX, BaseY, BaseZ, YawDeg);
	const float Xs[] = {-7.0f, -3.8f, 0.5f, 3.8f, 6.6f};
	const float Ys[] = {2.6f, -2.7f, 2.4f, -2.3f, 1.8f};
	const float Yaws[] = {-20.0f, 42.0f, 5.0f, 76.0f, -48.0f};
	for (int32 Index = 0; Index < 5; ++Index) {
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("%s_MixedClutter_%02d"), *Prefix, Index + 1),
			ShipwreckProjectLocalLocation(Xs[Index], Ys[Index], BaseZ + 0.24f + 0.05f * (Index % 2), BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg + Yaws[Index], 0.0f),
			FVector(1.8f + 0.4f * (Index % 2), 0.55f, 0.42f),
			BaseZ);
	}
}

void SpawnShipwreckProjectSeafloorPatch(
	UWorld* World,
	UStaticMesh* Cube,
	float BaseZ) {
	if (!IsShipwreckProjectFlagEnabled(
			TEXT("HOLOOCEAN_SHIPWRECK_ADD_SEAFLOOR"),
			TEXT("ShipwreckProjectAddSeafloor"))) {
		return;
	}

	SpawnShipwreckProjectBox(
		World,
		Cube,
		TEXT("ShipwreckProject_SeafloorPatch_Acoustic"),
		ShipwreckProjectLocation(-20.0f, 0.0f, BaseZ - 0.08f),
		ShipwreckProjectRotation(0.0f, 0.0f, 0.0f),
		FVector(150.0f, 115.0f, 0.04f));
}

void SpawnShipwreckProjectLiteratureSeabedProxy(
	UWorld* World,
	UStaticMesh* Cube,
	bool bKhoaSurveyScene,
	float BaseZ) {
	if (!World || !Cube) {
		return;
	}

	const auto TerrainZAt = [bKhoaSurveyScene, BaseZ](float X, float Y) {
		return bKhoaSurveyScene ? ShipwreckProjectKhoaTerrainZAt(X, Y) : BaseZ;
	};

	const FLinearColor RippleColor(0.54f, 0.49f, 0.36f, 1.0f);
	const FLinearColor RidgeColor(0.47f, 0.40f, 0.28f, 1.0f);
	const FLinearColor SedimentColor(0.58f, 0.54f, 0.44f, 1.0f);

	struct FSeabedFeature {
		const TCHAR* Label;
		float X;
		float Y;
		float YawDeg;
		float Length;
		float Width;
		float Height;
		FLinearColor Color;
	};

	const FSeabedFeature Features[] = {
		// Low, elongated sand-ripple/ridge proxies. They are grounded collision geometry
		// so SSS can see weak seabed texture without acting like shipwreck targets.
		{TEXT("SandRipple_01"), -59.0f, -34.0f, 18.0f, 9.0f, 0.10f, 0.055f, RippleColor},
		{TEXT("SandRipple_02"), -48.0f, -31.0f, 18.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_03"), -34.0f, -27.0f, 18.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_04"), -18.0f, -31.0f, 19.0f, 9.0f, 0.10f, 0.055f, RippleColor},
		{TEXT("SandRipple_05"), -2.0f, -34.0f, 17.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_06"), 15.0f, -30.0f, 17.0f, 8.0f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_07"), -61.0f, -12.0f, 21.0f, 9.0f, 0.10f, 0.055f, RippleColor},
		{TEXT("SandRipple_08"), -43.0f, -8.0f, 20.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_09"), -25.0f, -13.0f, 19.0f, 9.0f, 0.10f, 0.055f, RippleColor},
		{TEXT("SandRipple_10"), -4.0f, -9.0f, 20.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_11"), 17.0f, -12.0f, 18.0f, 9.0f, 0.10f, 0.055f, RippleColor},
		{TEXT("SandRipple_12"), -55.0f, 13.0f, 24.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_13"), -36.0f, 10.0f, 23.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_14"), -18.0f, 15.0f, 23.0f, 9.0f, 0.10f, 0.055f, RippleColor},
		{TEXT("SandRipple_15"), 2.0f, 11.0f, 22.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_16"), 20.0f, 16.0f, 23.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_17"), -52.0f, 34.0f, 26.0f, 8.0f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_18"), -30.0f, 31.0f, 26.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_19"), -8.0f, 35.0f, 25.0f, 8.5f, 0.10f, 0.050f, RippleColor},
		{TEXT("SandRipple_20"), 14.0f, 32.0f, 25.0f, 8.0f, 0.10f, 0.050f, RippleColor},
		// Broader low relief lenses/mounds that represent coarser patch boundaries.
		{TEXT("SedimentLens_01"), -52.0f, -2.0f, -8.0f, 11.0f, 3.4f, 0.070f, SedimentColor},
		{TEXT("SedimentLens_02"), -22.0f, 3.0f, 7.0f, 12.0f, 3.8f, 0.075f, SedimentColor},
		{TEXT("SedimentLens_03"), 9.0f, -4.0f, -5.0f, 11.0f, 3.4f, 0.070f, SedimentColor},
		{TEXT("LowRidge_01"), -41.0f, 24.0f, 32.0f, 16.0f, 0.36f, 0.120f, RidgeColor},
		{TEXT("LowRidge_02"), -4.0f, 26.0f, 31.0f, 15.0f, 0.36f, 0.110f, RidgeColor},
		{TEXT("LowRidge_03"), 15.0f, -23.0f, 12.0f, 14.0f, 0.34f, 0.105f, RidgeColor},
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Features); ++Index) {
		const FSeabedFeature& Feature = Features[Index];
		const float TerrainZ = TerrainZAt(Feature.X, Feature.Y);
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("ShipwreckProject_SeabedFeature_%s"), Feature.Label),
			ShipwreckProjectLocation(Feature.X, Feature.Y, TerrainZ + 0.10f),
			ShipwreckProjectRotation(0.0f, Feature.YawDeg, 0.0f),
			FVector(Feature.Length, Feature.Width, Feature.Height),
			TerrainZ,
			0.004f,
			Feature.Color);
	}

	UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned literature-backed low relief seabed proxy features"));
}

void ShipwreckProjectBuildSlabOutline(
	float LengthM,
	float WidthM,
	bool bGrooved,
	int32 ShapeCode,
	TArray<FVector2D>& Outline) {
	Outline.Reset();
	const float HL = LengthM * 0.5f;
	const float HW = WidthM * 0.5f;

	if (bGrooved) {
		const float Neck = HW * (0.58f + 0.05f * (ShapeCode % 3));
		Outline.Add(FVector2D(-HL, -HW * (0.30f + 0.08f * (ShapeCode % 2))));
		Outline.Add(FVector2D(-HL * 0.62f, -HW * 0.94f));
		Outline.Add(FVector2D(-HL * 0.18f, -HW));
		Outline.Add(FVector2D(-HL * 0.055f, -Neck));
		Outline.Add(FVector2D(HL * 0.055f, -Neck * 0.96f));
		Outline.Add(FVector2D(HL * 0.22f, -HW * 0.95f));
		Outline.Add(FVector2D(HL * 0.72f, -HW * (0.80f + 0.07f * ((ShapeCode + 1) % 2))));
		Outline.Add(FVector2D(HL, -HW * (0.18f + 0.05f * (ShapeCode % 3))));
		Outline.Add(FVector2D(HL * (0.82f - 0.05f * (ShapeCode % 2)), HW * 0.78f));
		Outline.Add(FVector2D(HL * 0.22f, HW));
		Outline.Add(FVector2D(HL * 0.055f, Neck));
		Outline.Add(FVector2D(-HL * 0.055f, Neck * 0.98f));
		Outline.Add(FVector2D(-HL * 0.18f, HW * 0.92f));
		Outline.Add(FVector2D(-HL * 0.66f, HW * 0.84f));
		Outline.Add(FVector2D(-HL, HW * (0.36f + 0.07f * ((ShapeCode + 1) % 2))));
		return;
	}

	if (ShapeCode % 3 == 0) {
		Outline.Add(FVector2D(-HL, -HW * 0.25f));
		Outline.Add(FVector2D(-HL * 0.50f, -HW));
		Outline.Add(FVector2D(HL * 0.35f, -HW * 0.86f));
		Outline.Add(FVector2D(HL, -HW * 0.18f));
		Outline.Add(FVector2D(HL * 0.70f, HW * 0.78f));
		Outline.Add(FVector2D(-HL * 0.18f, HW));
		Outline.Add(FVector2D(-HL * 0.82f, HW * 0.55f));
	} else if (ShapeCode % 3 == 1) {
		Outline.Add(FVector2D(-HL, -HW * 0.78f));
		Outline.Add(FVector2D(-HL * 0.12f, -HW));
		Outline.Add(FVector2D(HL, -HW * 0.45f));
		Outline.Add(FVector2D(HL * 0.84f, HW * 0.48f));
		Outline.Add(FVector2D(HL * 0.22f, HW));
		Outline.Add(FVector2D(-HL * 0.72f, HW * 0.70f));
	} else {
		Outline.Add(FVector2D(-HL, -HW * 0.12f));
		Outline.Add(FVector2D(-HL * 0.52f, -HW * 0.88f));
		Outline.Add(FVector2D(HL * 0.18f, -HW));
		Outline.Add(FVector2D(HL, -HW * 0.30f));
		Outline.Add(FVector2D(HL * 0.52f, HW * 0.92f));
		Outline.Add(FVector2D(-HL * 0.28f, HW));
		Outline.Add(FVector2D(-HL * 0.88f, HW * 0.46f));
	}
}

void SpawnShipwreckProjectMadoIrregularSlab(
	UWorld* World,
	const FString& Label,
	float X,
	float Y,
	float YawDeg,
	float LengthM,
	float WidthM,
	float ThicknessM,
	bool bGrooved,
	int32 ShapeCode,
	const FLinearColor& Color) {
	TArray<FVector2D> Outline;
	ShipwreckProjectBuildSlabOutline(LengthM, WidthM, bGrooved, ShapeCode, Outline);
	if (Outline.Num() < 3) {
		return;
	}

	const float TerrainZ = ShipwreckProjectKhoaTerrainZAt(X, Y);
	const float Clearance = 0.008f;
	const float RoughAmp = FMath::Min(ThicknessM * 0.16f, 0.018f);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	Vertices.Reserve(Outline.Num() * 2 + 2);

	for (int32 Index = 0; Index < Outline.Num(); ++Index) {
		const FVector2D Rotated = ShipwreckProjectRotate2D(Outline[Index].X, Outline[Index].Y, YawDeg);
		const float Rough = RoughAmp * FMath::Sin((Index + 1) * (ShapeCode + 2) * 1.37f);
		Vertices.Add(FVector(X + Rotated.X, Y + Rotated.Y, TerrainZ + Clearance + ThicknessM + Rough));
	}
	const int32 BottomStart = Vertices.Num();
	for (int32 Index = 0; Index < Outline.Num(); ++Index) {
		const FVector2D Rotated = ShipwreckProjectRotate2D(Outline[Index].X, Outline[Index].Y, YawDeg);
		Vertices.Add(FVector(X + Rotated.X, Y + Rotated.Y, TerrainZ + Clearance));
	}
	const int32 TopCenter = Vertices.Num();
	Vertices.Add(FVector(X, Y, TerrainZ + Clearance + ThicknessM + RoughAmp * 0.35f));
	const int32 BottomCenter = Vertices.Num();
	Vertices.Add(FVector(X, Y, TerrainZ + Clearance));

	const int32 N = Outline.Num();
	for (int32 Index = 0; Index < N; ++Index) {
		const int32 Next = (Index + 1) % N;
		Triangles.Add(TopCenter);
		Triangles.Add(Index);
		Triangles.Add(Next);

		Triangles.Add(BottomCenter);
		Triangles.Add(BottomStart + Next);
		Triangles.Add(BottomStart + Index);

		Triangles.Add(Index);
		Triangles.Add(BottomStart + Index);
		Triangles.Add(BottomStart + Next);
		Triangles.Add(Index);
		Triangles.Add(BottomStart + Next);
		Triangles.Add(Next);
	}

	SpawnShipwreckProjectProceduralMesh(World, Label, Vertices, Triangles, Color);
}

void SpawnShipwreckProjectMadoAnchorStoneField(UWorld* World) {
	const FMadoSceneConfig& Config = GetActiveMadoSceneConfig();
	const FLinearColor		 DefaultColor(0.35f, 0.35f, 0.33f, 1.0f);
	for (int32 Index = 0; Index < Config.AnchorStones.Num(); ++Index) {
		const FMadoAnchorStoneConfig& Stone = Config.AnchorStones[Index];
		SpawnShipwreckProjectMadoIrregularSlab(
			World,
			FString::Printf(TEXT("ShipwreckProject_MadoAnchorStone_%s"), *Stone.Id),
			Stone.CenterX,
			Stone.CenterY,
			Stone.YawDeg,
			Stone.LengthCm * 0.01f,
			Stone.WidthCm * 0.01f,
			FMath::Clamp(Stone.ThicknessCm * 0.01f, 0.045f, 0.24f),
			true,
			Index,
			DefaultColor);
	}
}

// Shallow radial depression around a wreck's base, tapering back to the surrounding terrain
// height. Reuses the same triangulated-fan mesh pattern as other low-relief seabed features in
// this file, just with a negative (dug-in) height offset instead of a positive one. Depth is
// full ScourDepthM at the hull footprint and fades to 0 at RadiusM.
void ShipwreckProjectSpawnScourPit(UWorld* World, const FString& Label, float CenterX, float CenterY, float RadiusM, float DepthM) {
	constexpr int32 Segments = 24;
	constexpr int32 Rings = 3;
	const float		Clearance = 0.004f;

	TArray<FVector> Vertices;
	TArray<int32>	Triangles;
	Vertices.Reserve(1 + Segments * Rings);

	const float CenterZ = ShipwreckProjectKhoaTerrainZAt(CenterX, CenterY) - Clearance - DepthM;
	Vertices.Add(FVector(CenterX, CenterY, CenterZ));

	for (int32 Ring = 1; Ring <= Rings; ++Ring) {
		const float R = static_cast<float>(Ring) / static_cast<float>(Rings);
		for (int32 Segment = 0; Segment < Segments; ++Segment) {
			const float Angle = 2.0f * PI * static_cast<float>(Segment) / static_cast<float>(Segments);
			const float Px = CenterX + FMath::Cos(Angle) * RadiusM * R;
			const float Py = CenterY + FMath::Sin(Angle) * RadiusM * R;
			const float EdgeFalloff = FMath::Clamp(1.0f - R * R, 0.0f, 1.0f);
			const float Z = ShipwreckProjectKhoaTerrainZAt(Px, Py) - Clearance - DepthM * EdgeFalloff;
			Vertices.Add(FVector(Px, Py, Z));
		}
	}

	for (int32 Segment = 0; Segment < Segments; ++Segment) {
		const int32 Next = (Segment + 1) % Segments;
		Triangles.Add(0);
		Triangles.Add(1 + Next);
		Triangles.Add(1 + Segment);
	}
	for (int32 Ring = 2; Ring <= Rings; ++Ring) {
		const int32 PrevStart = 1 + (Ring - 2) * Segments;
		const int32 CurrStart = 1 + (Ring - 1) * Segments;
		for (int32 Segment = 0; Segment < Segments; ++Segment) {
			const int32 Next = (Segment + 1) % Segments;
			Triangles.Add(PrevStart + Segment);
			Triangles.Add(CurrStart + Next);
			Triangles.Add(CurrStart + Segment);
			Triangles.Add(PrevStart + Segment);
			Triangles.Add(PrevStart + Next);
			Triangles.Add(CurrStart + Next);
		}
	}

	SpawnShipwreckProjectProceduralMesh(World, Label, Vertices, Triangles, FLinearColor(0.20f, 0.19f, 0.17f, 1.0f));
}

// Wreck placement: position/yaw/dimensions/burial/scour come from GetActiveMadoSceneConfig()'s
// wreck_spawns array (see MadoSceneConfig.h). The 3D mesh asset itself is out of scope for this
// environment repo -- these are procedural placeholder hulls (box keel/ribs/rails) until a real
// asset is swapped in; each spawn's "evidence" field in the JSON records whether it is
// report-sourced or a test-only stand-in (the bundled default has one test-only hull, no report
// source for its position/shape -- see its evidence field).
void SpawnShipwreckProjectMadoWrecks(UWorld* World) {
	const FMadoSceneConfig& Config = GetActiveMadoSceneConfig();
	UStaticMesh*			 Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) {
		return;
	}

	for (const FMadoWreckSpawnConfig& Wreck : Config.WreckSpawns) {
		const FString Prefix = FString::Printf(TEXT("ShipwreckProject_SurveyWreck_%s"), *Wreck.Id);
		const float	  BaseX = Wreck.CenterX;
		const float	  BaseY = Wreck.CenterY;
		const float	  YawDeg = Wreck.YawDeg;
		const float	  Length = Wreck.LengthM;
		const float	  Width = Wreck.WidthM;
		const float	  Height = Wreck.HeightM;
		// Burial lowers the whole hull toward/into the surrounding grade, reducing exposed
		// freeboard (and therefore the SSS highlight/shadow footprint) proportionally --
		// burial_fraction=0 sits proud on the seafloor, =1 has its high point flush with grade.
		const float BaseZ = ShipwreckProjectKhoaTerrainZAt(BaseX, BaseY) + 0.05f - Wreck.BurialFraction * Height;

		TArray<AStaticMeshActor*> Parts;
		Parts.Add(SpawnShipwreckProjectBox(
			World,
			Cube,
			Prefix + TEXT("_Keel"),
			ShipwreckProjectLocalLocation(0.0f, 0.0f, BaseZ + 0.18f * Height, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
			FVector(Length, 0.18f, 0.16f * Height)));

		for (int32 Index = 0; Index < 8; ++Index) {
			const float X = -0.42f * Length + Index * (0.84f * Length / 7.0f);
			const float RibWidth = Width * (0.62f + 0.34f * FMath::Sin((Index + 1) * PI / 9.0f));
			Parts.Add(SpawnShipwreckProjectBox(
				World,
				Cube,
				FString::Printf(TEXT("%s_Rib_%02d"), *Prefix, Index + 1),
				ShipwreckProjectLocalLocation(X, 0.0f, BaseZ + 0.46f * Height, BaseX, BaseY, YawDeg),
				ShipwreckProjectRotation(0.0f, YawDeg + 90.0f, 0.0f),
				FVector(RibWidth, 0.16f, 0.22f * Height)));
		}

		Parts.Add(SpawnShipwreckProjectBox(
			World,
			Cube,
			Prefix + TEXT("_PortRail"),
			ShipwreckProjectLocalLocation(0.0f, -0.5f * Width, BaseZ + 0.55f * Height, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
			FVector(Length, 0.16f, 0.20f * Height)));
		Parts.Add(SpawnShipwreckProjectBox(
			World,
			Cube,
			Prefix + TEXT("_StarboardRail"),
			ShipwreckProjectLocalLocation(0.0f, 0.5f * Width, BaseZ + 0.55f * Height, BaseX, BaseY, YawDeg),
			ShipwreckProjectRotation(0.0f, YawDeg, 0.0f),
			FVector(Length, 0.16f, 0.20f * Height)));

		for (AStaticMeshActor* Part : Parts) {
			if (Part) {
				Part->Tags.Add(FName(TEXT("wreck")));
			}
		}

		if (Wreck.bScourPitEnabled) {
			ShipwreckProjectSpawnScourPit(
				World,
				Prefix + TEXT("_ScourPit"),
				BaseX,
				BaseY,
				Length * Wreck.ScourRadiusLengthMultiple,
				Wreck.ScourDepthM);
		}

		UE_LOG(
			LogHolodeck,
			Log,
			TEXT("ShipwreckProject 2.4: spawned wreck '%s' at %.1f,%.1f burial=%.2f scour=%d evidence=\"%s\""),
			*Wreck.Id,
			BaseX,
			BaseY,
			Wreck.BurialFraction,
			Wreck.bScourPitEnabled ? 1 : 0,
			*Wreck.Evidence);
	}
}

void SpawnShipwreckProjectMadoReefEdgeCues(UWorld* World) {
	const FMadoSceneConfig& Config = GetActiveMadoSceneConfig();
	const FLinearColor		 ReefColor(0.25f, 0.24f, 0.22f, 1.0f);
	for (int32 Index = 0; Index < Config.ReefEdgeCues.Num(); ++Index) {
		const FMadoReefCueConfig& Reef = Config.ReefEdgeCues[Index];
		SpawnShipwreckProjectMadoIrregularSlab(
			World,
			FString::Printf(TEXT("ShipwreckProject_MadoReefRockEdge_%s"), *Reef.Id),
			Reef.CenterX,
			Reef.CenterY,
			Reef.YawDeg,
			Reef.LengthM,
			Reef.WidthM,
			Reef.ThicknessM,
			false,
			10 + Index,
			ReefColor);
	}
}

void SpawnShipwreckProjectMadoReportEnvironmentCore(UWorld* World) {
	const FMadoSceneConfig& Config = GetActiveMadoSceneConfig();
	SpawnShipwreckProjectMadoAnchorStoneField(World);
	SpawnShipwreckProjectMadoReefEdgeCues(World);
	SpawnShipwreckProjectMadoWrecks(World);
	UE_LOG(
		LogHolodeck,
		Log,
		TEXT("ShipwreckProject 2.4: spawned Mado report environment core '%s' from %s facies_zones=%d anchor_stones=%d reef_edge=%d wrecks=%d"),
		*Config.SceneName,
		*Config.SourceJsonPath,
		Config.FaciesZones.Num(),
		Config.AnchorStones.Num(),
		Config.ReefEdgeCues.Num(),
		Config.WreckSpawns.Num());
}

void SpawnShipwreckProjectKhoaSmoothTerrainMesh(UWorld* World) {
	if (!World) {
		return;
	}

	const FString TerrainEnv =
		FPlatformMisc::GetEnvironmentVariable(TEXT("HOLOOCEAN_SHIPWRECK_KHOA_SMOOTH_TERRAIN_MESH"));
	if (!TerrainEnv.IsEmpty() && TerrainEnv.Equals(TEXT("0"))) {
		UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: KHOA smooth terrain mesh disabled by env"));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("ShipwreckProject_SeabedProxy_KhoaSmoothBathymetryTerrain"));
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* TerrainActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!TerrainActor) {
		UE_LOG(LogHolodeck, Warning, TEXT("ShipwreckProject 2.4: failed to spawn KHOA smooth terrain actor"));
		return;
	}
	TerrainActor->Tags.Add(FName(TEXT("ShipwreckProject")));
	TerrainActor->Tags.Add(FName(TEXT("SeabedProxy")));
	TerrainActor->Tags.Add(FName(TEXT("KhoaSmoothBathymetryTerrain")));

	UProceduralMeshComponent* MeshComp =
		NewObject<UProceduralMeshComponent>(TerrainActor, TEXT("KhoaSmoothBathymetryTerrain"));
	if (!MeshComp) {
		UE_LOG(LogHolodeck, Warning, TEXT("ShipwreckProject 2.4: failed to create KHOA smooth terrain mesh component"));
		return;
	}

	TerrainActor->SetRootComponent(MeshComp);
	MeshComp->RegisterComponent();
	MeshComp->SetMobility(EComponentMobility::Static);
	MeshComp->bUseComplexAsSimpleCollision = true;

	const int32 GridX = ShipwreckKhoaSmoothTerrainData::GridX;
	const int32 GridY = ShipwreckKhoaSmoothTerrainData::GridY;
	TArray<FVector> Vertices;
	Vertices.Reserve(GridX * GridY);
	for (int32 Iy = 0; Iy < GridY; ++Iy) {
		for (int32 Ix = 0; Ix < GridX; ++Ix) {
			const int32 Index = Iy * GridX + Ix;
			const float X = ShipwreckKhoaSmoothTerrainData::XValues[Ix];
			const float Y = ShipwreckKhoaSmoothTerrainData::YValues[Iy];
			const float Z = -ShipwreckProjectKhoaDepthAt(X, Y);
			Vertices.Add(ShipwreckProjectLocation(X, Y, Z));
		}
	}

	TArray<int32> Triangles;
	Triangles.Reserve((GridX - 1) * (GridY - 1) * 6);
	for (int32 Iy = 0; Iy < GridY - 1; ++Iy) {
		for (int32 Ix = 0; Ix < GridX - 1; ++Ix) {
			const int32 V00 = Iy * GridX + Ix;
			const int32 V10 = Iy * GridX + Ix + 1;
			const int32 V01 = (Iy + 1) * GridX + Ix;
			const int32 V11 = (Iy + 1) * GridX + Ix + 1;

			Triangles.Add(V00);
			Triangles.Add(V01);
			Triangles.Add(V10);
			Triangles.Add(V10);
			Triangles.Add(V01);
			Triangles.Add(V11);
		}
	}

	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	Normals.Init(FVector::ZeroVector, Vertices.Num());
	Tangents.Reserve(Vertices.Num());
	UV0.Reserve(Vertices.Num());
	VertexColors.Reserve(Vertices.Num());
	const float DepthRange =
		FMath::Max(ShipwreckKhoaSmoothTerrainData::DepthMaxM - ShipwreckKhoaSmoothTerrainData::DepthMinM, 0.001f);
	for (int32 Iy = 0; Iy < GridY; ++Iy) {
		for (int32 Ix = 0; Ix < GridX; ++Ix) {
			const int32 Index = Iy * GridX + Ix;
			UV0.Add(FVector2D(
				static_cast<float>(Ix) / static_cast<float>(GridX - 1),
				static_cast<float>(Iy) / static_cast<float>(GridY - 1)));
			const float X = ShipwreckKhoaSmoothTerrainData::XValues[Ix];
			const float Y = ShipwreckKhoaSmoothTerrainData::YValues[Iy];
			const float Depth01 =
				(ShipwreckProjectKhoaDepthAt(X, Y) - ShipwreckKhoaSmoothTerrainData::DepthMinM) / DepthRange;
			VertexColors.Add(FLinearColor(0.16f + 0.14f * Depth01, 0.15f + 0.10f * Depth01, 0.10f, 1.0f));
			Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}
	}

	for (int32 TriIndex = 0; TriIndex + 2 < Triangles.Num(); TriIndex += 3) {
		const int32 A = Triangles[TriIndex];
		const int32 B = Triangles[TriIndex + 1];
		const int32 C = Triangles[TriIndex + 2];
		const FVector FaceNormal = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
		Normals[A] += FaceNormal;
		Normals[B] += FaceNormal;
		Normals[C] += FaceNormal;
	}
	for (FVector& Normal : Normals) {
		Normal = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
		if (Normal.Z < 0.0f) {
			Normal *= -1.0f;
		}
	}

	MeshComp->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, true);
	ShipwreckProjectApplyBasicColor(
		MeshComp,
		FLinearColor(0.66f, 0.60f, 0.46f, 1.0f),
		FName(TEXT("ShipwreckProjectSeabed")));
	MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
	MeshComp->SetCollisionObjectType(ECollisionChannel::ECC_WorldStatic);
	MeshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetGenerateOverlapEvents(false);
	MeshComp->RecreatePhysicsState();
	MeshComp->UpdateBounds();
	TerrainActor->SetActorEnableCollision(true);

	UE_LOG(
		LogHolodeck,
		Log,
		TEXT("ShipwreckProject 2.4: spawned KHOA smooth bathymetry terrain grid=%dx%d vertices=%d triangles=%d x=[%.1f,%.1f] y=[%.1f,%.1f] depth=[%.2f,%.2f]"),
		GridX,
		GridY,
		Vertices.Num(),
		Triangles.Num() / 3,
		ShipwreckKhoaSmoothTerrainData::XMinM,
		ShipwreckKhoaSmoothTerrainData::XMaxM,
		ShipwreckKhoaSmoothTerrainData::YMinM,
		ShipwreckKhoaSmoothTerrainData::YMaxM,
		ShipwreckKhoaSmoothTerrainData::DepthMinM,
		ShipwreckKhoaSmoothTerrainData::DepthMaxM);
}

void SpawnShipwreckProjectKhoaSurveyVisualDebugGuides(UWorld* World, UStaticMesh* Cube) {
	if (!World || !Cube) {
		return;
	}

	const FLinearColor TrackColor(0.95f, 0.95f, 0.18f, 1.0f);
	const FLinearColor BoundaryColor(0.10f, 0.75f, 1.00f, 1.0f);
	const FLinearColor ContactColor(1.00f, 0.20f, 0.12f, 1.0f);
	const FLinearColor PostColor(0.05f, 1.00f, 0.25f, 1.0f);

	const float XMin = -66.0f;
	const float XMax = 26.0f;
	const float YMin = -42.0f;
	const float YMax = 42.0f;
	const float SegmentLength = 7.5f;
	const float GuideLift = 0.08f;

	const float TrackYs[] = {-36.0f, -18.0f, 0.0f, 18.0f, 24.0f};
	for (int32 TrackIndex = 0; TrackIndex < UE_ARRAY_COUNT(TrackYs); ++TrackIndex) {
		const float Y = TrackYs[TrackIndex];
		int32 SegmentIndex = 0;
		for (float X = XMin + SegmentLength * 0.5f; X < XMax; X += SegmentLength) {
			const float Z = ShipwreckProjectKhoaTerrainZAt(X, Y) + GuideLift;
			SpawnShipwreckProjectGuideBox(
				World,
				Cube,
				FString::Printf(TEXT("ShipwreckProject_VisualGuide_Track_%02d_%02d"), TrackIndex + 1, SegmentIndex + 1),
				ShipwreckProjectLocation(X, Y, Z),
				ShipwreckProjectRotation(0.0f, 0.0f, 0.0f),
				FVector(SegmentLength * 0.95f, 0.08f, 0.035f),
				TrackColor);
			++SegmentIndex;
		}
	}

	const float BorderYs[] = {YMin, YMax};
	for (int32 BorderIndex = 0; BorderIndex < UE_ARRAY_COUNT(BorderYs); ++BorderIndex) {
		const float Y = BorderYs[BorderIndex];
		int32 SegmentIndex = 0;
		for (float X = XMin + SegmentLength * 0.5f; X < XMax; X += SegmentLength) {
			const float Z = ShipwreckProjectKhoaTerrainZAt(X, Y) + GuideLift;
			SpawnShipwreckProjectGuideBox(
				World,
				Cube,
				FString::Printf(TEXT("ShipwreckProject_VisualGuide_BorderY_%02d_%02d"), BorderIndex + 1, SegmentIndex + 1),
				ShipwreckProjectLocation(X, Y, Z),
				ShipwreckProjectRotation(0.0f, 0.0f, 0.0f),
				FVector(SegmentLength * 0.95f, 0.13f, 0.04f),
				BoundaryColor);
			++SegmentIndex;
		}
	}

	const float BorderXs[] = {XMin, XMax};
	for (int32 BorderIndex = 0; BorderIndex < UE_ARRAY_COUNT(BorderXs); ++BorderIndex) {
		const float X = BorderXs[BorderIndex];
		int32 SegmentIndex = 0;
		for (float Y = YMin + SegmentLength * 0.5f; Y < YMax; Y += SegmentLength) {
			const float Z = ShipwreckProjectKhoaTerrainZAt(X, Y) + GuideLift;
			SpawnShipwreckProjectGuideBox(
				World,
				Cube,
				FString::Printf(TEXT("ShipwreckProject_VisualGuide_BorderX_%02d_%02d"), BorderIndex + 1, SegmentIndex + 1),
				ShipwreckProjectLocation(X, Y, Z),
				ShipwreckProjectRotation(0.0f, 0.0f, 0.0f),
				FVector(0.13f, SegmentLength * 0.95f, 0.04f),
				BoundaryColor);
			++SegmentIndex;
		}
	}

	struct FContactMarker {
		const TCHAR* Label;
		float X;
		float Y;
		float FootprintX;
		float FootprintY;
		float PostHeight;
	};

	const FContactMarker Markers[] = {
		{TEXT("Intact_A"), -58.0f, -22.0f, 4.2f, 2.0f, 2.8f},
		{TEXT("Buried_A"), -47.0f, 9.0f, 4.0f, 2.0f, 2.4f},
		{TEXT("Fragmented_A"), -32.0f, 31.0f, 4.0f, 2.0f, 2.4f},
		{TEXT("LowRelief_A"), -10.0f, -20.0f, 3.8f, 1.8f, 1.5f},
		{TEXT("RibField_A"), 7.0f, 17.0f, 4.0f, 2.0f, 2.6f},
		{TEXT("DebrisField_A"), 19.0f, -31.0f, 3.8f, 2.0f, 2.3f},
		{TEXT("Fragmented_B"), -6.0f, 36.0f, 4.0f, 2.0f, 2.4f},
		{TEXT("Buried_B"), 24.0f, 4.0f, 4.0f, 2.0f, 2.4f},
		{TEXT("Gear_01"), -63.0f, 4.0f, 1.8f, 1.0f, 1.3f},
		{TEXT("Gear_02"), -52.0f, -36.0f, 1.8f, 1.0f, 1.3f},
		{TEXT("Gear_03"), 0.0f, -24.0f, 1.8f, 1.0f, 1.3f},
		{TEXT("Gear_04"), 25.0f, -18.0f, 1.8f, 1.0f, 1.3f},
		{TEXT("Rock_01"), -66.0f, 18.0f, 1.2f, 1.2f, 1.1f},
		{TEXT("Rock_02"), -60.0f, -6.0f, 1.2f, 1.2f, 1.1f},
		{TEXT("Rock_03"), -44.0f, -30.0f, 1.2f, 1.2f, 1.1f},
		{TEXT("Rock_04"), -9.0f, -18.0f, 1.2f, 1.2f, 1.1f},
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Markers); ++Index) {
		const FContactMarker& Marker = Markers[Index];
		const float TerrainZ = ShipwreckProjectKhoaTerrainZAt(Marker.X, Marker.Y);
		SpawnShipwreckProjectGuideBox(
			World,
			Cube,
			FString::Printf(TEXT("ShipwreckProject_VisualGuide_Footprint_%s"), Marker.Label),
			ShipwreckProjectLocation(Marker.X, Marker.Y, TerrainZ + 0.035f),
			ShipwreckProjectRotation(0.0f, 0.0f, 0.0f),
			FVector(Marker.FootprintX, Marker.FootprintY, 0.035f),
			ContactColor);
		SpawnShipwreckProjectGuideBox(
			World,
			Cube,
			FString::Printf(TEXT("ShipwreckProject_VisualGuide_Post_%s"), Marker.Label),
			ShipwreckProjectLocation(Marker.X, Marker.Y, TerrainZ + Marker.PostHeight * 0.5f),
			ShipwreckProjectRotation(0.0f, 0.0f, 0.0f),
			FVector(0.11f, 0.11f, Marker.PostHeight),
			PostColor);
	}

	UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned KHOA survey visual debug guide layer"));
}

void SpawnShipwreckProjectFlatUnderwaterScene(UWorld* World) {
	const bool bRequested = IsShipwreckProjectFlagEnabled(
		TEXT("HOLOOCEAN_SHIPWRECK_SPAWN"),
		TEXT("ShipwreckProjectSpawn"));
	if (!bRequested) {
		return;
	}

	const FString ScenePreset = FPlatformMisc::GetEnvironmentVariable(TEXT("HOLOOCEAN_SHIPWRECK_SCENE_PRESET"));
	if (ScenePreset.Equals(TEXT("khoa_bathymetry_only_v1"), ESearchCase::IgnoreCase)) {
		SpawnShipwreckProjectKhoaSmoothTerrainMesh(World);
		UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned khoa_bathymetry_only_v1 scene"));
		return;
	}
	const bool bKhoaVisualDebugScene =
		ScenePreset.Equals(TEXT("khoa_survey_scene_visual_debug_v1"), ESearchCase::IgnoreCase) ||
		ScenePreset.Equals(TEXT("khoa_survey_scene_literature_visual_debug_v2"), ESearchCase::IgnoreCase);
	const bool bKhoaLiteratureScene =
		ScenePreset.Equals(TEXT("khoa_survey_scene_literature_v2"), ESearchCase::IgnoreCase) ||
		ScenePreset.Equals(TEXT("khoa_survey_scene_literature_visual_debug_v2"), ESearchCase::IgnoreCase);
	// Resolved via GetActiveMadoSceneConfig() (MadoSceneConfig.h), not a hardcoded string-equals
	// check, so that HOLOOCEAN_SHIPWRECK_SCENE_PRESET can point at *any* JSON config file (not
	// just the two preset names originally supported) without a C++ change.
	const bool bMadoReportScene = IsMadoReportSceneActive();
	const bool bMadoReportVisualDebugScene =
		ScenePreset.Equals(TEXT("mado_report_environment_visual_debug_v1"), ESearchCase::IgnoreCase);
	const bool bKhoaSurveyScene =
		ScenePreset.Equals(TEXT("khoa_survey_scene_v1"), ESearchCase::IgnoreCase) ||
		bKhoaVisualDebugScene ||
		bKhoaLiteratureScene ||
		bMadoReportScene;
	if (bKhoaSurveyScene) {
		SpawnShipwreckProjectKhoaSmoothTerrainMesh(World);
		UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned KHOA smooth terrain under survey scene"));
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) {
		UE_LOG(LogHolodeck, Warning, TEXT("ShipwreckProject 2.4: failed to load cube mesh"));
		return;
	}

	const float BaseZ = ReadShipwreckProjectFloatEnv(TEXT("HOLOOCEAN_SHIPWRECK_BASE_Z"), -8.18f);
	if (!bKhoaSurveyScene) {
		SpawnShipwreckProjectSeafloorPatch(World, Cube, BaseZ);
	}
	const auto SurveyBaseZAt = [bKhoaSurveyScene, BaseZ](float X, float Y) {
		return bKhoaSurveyScene ? ShipwreckProjectKhoaTerrainZAt(X, Y) : BaseZ;
	};

	if (IsShipwreckProjectFlagEnabled(
			TEXT("HOLOOCEAN_SHIPWRECK_ONLY_AGENT_MESH"),
			TEXT("ShipwreckProjectOnlyAgentMesh"))) {
		UStaticMesh* TorpedoMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Game/HolodeckContent/Agents/TorpedoAUV/TorpedoAUVMesh.TorpedoAUVMesh"));
		if (TorpedoMesh) {
			SpawnShipwreckProjectMeshActor(
				World,
				TorpedoMesh,
				TEXT("ShipwreckProject_TorpedoMesh_Only_A"),
				-36.0f,
				-24.0f,
				BaseZ + 0.35f,
				90.0f,
				FVector(2.2f, 2.2f, 2.2f));
			SpawnShipwreckProjectMeshActor(
				World,
				TorpedoMesh,
				TEXT("ShipwreckProject_TorpedoMesh_Only_B"),
				-12.0f,
				12.0f,
				BaseZ + 0.35f,
				45.0f,
				FVector(2.2f, 2.2f, 2.2f));
			UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned only TorpedoAUV mesh targets at base_z=%.2f"), BaseZ);
		} else {
			UE_LOG(LogHolodeck, Warning, TEXT("ShipwreckProject 2.4: failed to load TorpedoAUVMesh for only-agent-mesh test"));
		}
		return;
	}

	if (bMadoReportScene) {
		SpawnShipwreckProjectMadoReportEnvironmentCore(World);
		UE_LOG(
			LogHolodeck,
			Log,
			TEXT("ShipwreckProject 2.4: spawned mado_report_environment_v1 without cube wreck/gear proxy actors; visual_debug=%d"),
			bMadoReportVisualDebugScene ? 1 : 0);
		return;
	}

	SpawnShipwreckProjectIntactWreck(World, Cube, TEXT("SurveyWreck_Intact_A"), -58.0f, -22.0f, SurveyBaseZAt(-58.0f, -22.0f), 22.0f);
	SpawnShipwreckProjectBuriedWreck(World, Cube, TEXT("SurveyWreck_Buried_A"), -47.0f, 9.0f, SurveyBaseZAt(-47.0f, 9.0f), -18.0f);
	SpawnShipwreckProjectFragmentedWreck(World, Cube, TEXT("SurveyWreck_Fragmented_A"), -32.0f, 31.0f, SurveyBaseZAt(-32.0f, 31.0f), 41.0f);
	SpawnShipwreckProjectLowReliefWreck(World, Cube, TEXT("SurveyWreck_LowRelief_A"), -10.0f, -20.0f, SurveyBaseZAt(-10.0f, -20.0f), 12.0f);
	SpawnShipwreckProjectRibFieldWreck(World, Cube, TEXT("SurveyWreck_RibField_A"), 7.0f, 17.0f, SurveyBaseZAt(7.0f, 17.0f), -28.0f);
	SpawnShipwreckProjectDebrisFieldWreck(World, Cube, TEXT("SurveyWreck_DebrisField_A"), 19.0f, -31.0f, SurveyBaseZAt(19.0f, -31.0f), 36.0f);
	SpawnShipwreckProjectFragmentedWreck(World, Cube, TEXT("SurveyWreck_Fragmented_B"), -6.0f, 36.0f, SurveyBaseZAt(-6.0f, 36.0f), -52.0f);
	SpawnShipwreckProjectBuriedWreck(World, Cube, TEXT("SurveyWreck_Buried_B"), 24.0f, 4.0f, SurveyBaseZAt(24.0f, 4.0f), 63.0f);

	const float GearXs[] = {-63.0f, -52.0f, 0.0f, 25.0f};
	const float GearYs[] = {4.0f, -36.0f, -24.0f, -18.0f};
	const float GearYaw[] = {28.0f, -8.0f, 16.0f, -35.0f};
	for (int32 Index = 0; Index < 4; ++Index) {
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("ShipwreckProject_GearRope_%02d"), Index + 1),
			ShipwreckProjectLocation(GearXs[Index], GearYs[Index], SurveyBaseZAt(GearXs[Index], GearYs[Index]) + 0.11f),
			ShipwreckProjectRotation(0.0f, GearYaw[Index], 0.0f),
			FVector(4.4f, 0.10f, 0.08f),
			SurveyBaseZAt(GearXs[Index], GearYs[Index]),
			0.006f,
			FLinearColor(0.09f, 0.10f, 0.08f, 1.0f));
	}

	const float RockXs[] = {-66.0f, -60.0f, -44.0f, -9.0f};
	const float RockYs[] = {18.0f, -6.0f, -30.0f, -18.0f};
	for (int32 Index = 0; Index < 4; ++Index) {
		SpawnShipwreckProjectGroundContactBox(
			World,
			Cube,
			FString::Printf(TEXT("ShipwreckProject_RockMound_%02d"), Index + 1),
			ShipwreckProjectLocation(RockXs[Index], RockYs[Index], SurveyBaseZAt(RockXs[Index], RockYs[Index]) + 0.15f),
			ShipwreckProjectRotation(0.0f, -30.0f + Index * 17.0f, 0.0f),
			FVector(1.0f + 0.2f * Index, 0.65f, 0.16f),
			SurveyBaseZAt(RockXs[Index], RockYs[Index]),
			0.006f,
			FLinearColor(0.16f, 0.15f, 0.13f, 1.0f));
	}

	if (bKhoaLiteratureScene) {
		SpawnShipwreckProjectLiteratureSeabedProxy(World, Cube, bKhoaSurveyScene, BaseZ);
	}

	if (IsShipwreckProjectFlagEnabled(
			TEXT("HOLOOCEAN_SHIPWRECK_USE_AGENT_MESH"),
			TEXT("ShipwreckProjectUseAgentMesh"))) {
		UStaticMesh* TorpedoMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Game/HolodeckContent/Agents/TorpedoAUV/TorpedoAUVMesh.TorpedoAUVMesh"));
		if (TorpedoMesh) {
			SpawnShipwreckProjectMeshActor(
				World,
				TorpedoMesh,
				TEXT("ShipwreckProject_TorpedoMesh_Target_A"),
				-32.0f,
				0.0f,
				SurveyBaseZAt(-32.0f, 0.0f) + 0.35f,
				90.0f,
				FVector(1.7f, 1.7f, 1.7f));
			UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned TorpedoAUV mesh target for non-cube sonar test"));
		} else {
			UE_LOG(LogHolodeck, Warning, TEXT("ShipwreckProject 2.4: failed to load TorpedoAUVMesh for non-cube sonar test"));
		}
	}

	if (bKhoaSurveyScene &&
		(bKhoaVisualDebugScene || IsShipwreckProjectFlagEnabled(
									 TEXT("HOLOOCEAN_SHIPWRECK_VISUAL_DEBUG_GUIDES"),
									 TEXT("ShipwreckProjectVisualDebugGuides")))) {
		SpawnShipwreckProjectKhoaSurveyVisualDebugGuides(World, Cube);
	}

	UE_LOG(LogHolodeck, Log, TEXT("ShipwreckProject 2.4: spawned FlatUnderwater acoustic demo scene at base_z=%.2f"), BaseZ);
}

}

AHolodeckGameMode::AHolodeckGameMode(const FObjectInitializer& ObjectInitializer)
	: AGameMode(ObjectInitializer), bHolodeckIsOn(true) {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	UE_LOG(LogHolodeck, Log, TEXT("HolodeckGameMode initialized"));
	SIMMODE = this;
}

void AHolodeckGameMode::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);

	// If !bHolodeckIsOn, then we never got instance or reset signal,
	// so we don't need to check bOn here.
	if (this->Instance)
		this->Instance->Tick(DeltaSeconds);
	if (this->CommandCenter)
		this->CommandCenter->Tick(DeltaSeconds);
	// Check if we should reset, and then reset the level.
	if (ResetSignal && *ResetSignal) {
		UGameplayStatics::OpenLevel(
			this->Instance, FName(*GetWorld()->GetName()), false);
		*ResetSignal = false;
	}
}

void AHolodeckGameMode::StartPlay() {
	UE_LOG(LogHolodeck, Log, TEXT("HolodeckGameMode starting play"));

	// To prevent crashing in standalone games, check the HolodeckOn command is
	// supplied. This overrides the bHolodeckIsOn value supplied in the editor.
	// if (GetWorld()->WorldType == EWorldType::Game)
	//	bHolodeckIsOn = FParse::Param(FCommandLine::Get(), TEXT("HolodeckOn"));

	SpawnShipwreckProjectFlatUnderwaterScene(GetWorld());

	// Make sure Octree is properly initialized
	Octree::initOctree(GetWorld());

	// Cap our tickrate
	int FramesPerSec;
	if (FParse::Value(FCommandLine::Get(), TEXT("FramesPerSec="), FramesPerSec)) {
		FString command = "t.MaxFPS " + FString::FromInt(FramesPerSec);
		bool	succeeded = GEngine->Exec(GetWorld(), *command);

		if (!succeeded) {
			UE_LOG(LogHolodeck, Warning, TEXT("Unable to cap frametrate"));
		}
	}

	if (bHolodeckIsOn) {
		this->Instance = (UHolodeckGameInstance*)(GetGameInstance());
		if (this->Instance) {
			this->Instance->StartServer();
			Server = this->Instance->GetServer();

			RegisterSettings();
		} else {
			UE_LOG(
				LogHolodeck,
				Warning,
				TEXT("Game Instance couldn't be found and initialized"));
		}
		if (this->Server) {
			this->CommandCenter = NewObject<UCommandCenter>();
			CommandCenter->Init(Server, this);
		}
	}

	Super::StartPlay();
}

void AHolodeckGameMode::RegisterSettings() {
	UE_LOG(LogHolodeck, Log, TEXT("Registering Settings"));
	if (Server != nullptr) {
		ResetSignal = static_cast<bool*>(Server->Malloc(RESET_KEY, RESET_BYTES));
		UE_LOG(LogHolodeck, Log, TEXT("Reset signal registered"));
	}
}

void AHolodeckGameMode::LogFatalMessage(const FString& Message) {
	UE_LOG(LogHolodeck, Fatal, TEXT("%s"), *Message);
}

// These functions are taken from and/or inspired by the cosys air sim
// (https://cosys-lab.github.io/Cosys-AirSim/), which is MIT licensed.

template <>
FString
AHolodeckGameMode::GetMeshName<USkinnedMeshComponent>(USkinnedMeshComponent* mesh) {
	if (!mesh) {
		UE_LOG(
			LogHolodeck, Warning, TEXT("GetMeshName: USkinnedMeshComponent is null"));
		return "Unknown";
	}
	if (mesh->GetOwner()) {
		FString ownerName = mesh->GetOwner()->GetName();
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT("GetMeshName: USkinnedMeshComponent has owner '%s'"),
			*ownerName);
		return ownerName;
	} else {
		FString meshName = mesh->GetName();
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT(
				"GetMeshName: USkinnedMeshComponent has no owner, using mesh name '%s'"),
			*meshName);
		return meshName;
	}
}

FString AHolodeckGameMode::GetMeshName(ALandscapeProxy* mesh) {
	if (!mesh) {
		UE_LOG(LogHolodeck, Warning, TEXT("GetMeshName: ALandscapeProxy is null"));
		return "Unknown";
	}
	FString meshName = mesh->GetName();
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("GetMeshName: ALandscapeProxy name '%s'"),
		*meshName);
	return meshName;
}

void AHolodeckGameMode::InitialzeMaterialStencils() {
	FString filePath = FPaths::ProjectDir() + "../../materials.csv";

	// Create file if it doesn't exist to prevent crashes
	if (!FPaths::FileExists(filePath)) {
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT("Materials file not found, creating new one at %s"),
			*filePath);
		FFileHelper::SaveStringToFile(
			TEXT("MaterialName,Density,SoundSpeed\n"), *filePath);
	}

	TArray<FString> lines;
	// Reset counter
	NextStencilID = 1;

	FFileHelper::LoadANSITextFileToStrings(*filePath, NULL, lines);

	for (int i = 1; i < lines.Num(); i++) {
		TArray<FString> stringArray = {};
		lines[i].ParseIntoArray(stringArray, TEXT(","), false);

		if (stringArray.Num() >= 3) { // Changed to >= to be safe
			FString key = stringArray[0];
			// Ensure inputs are valid
			float	density = FCString::Atof(*stringArray[1]);
			float	speed = FCString::Atof(*stringArray[2]);
			float	z = density * speed;

			MaterialIDs.Add(key, NextStencilID);
			IDImpedances.Add(NextStencilID, z);

			NextStencilID++;
		}
	}

	InitializeMeshStencilIDs(MaterialIDs);
}

// void AHolodeckGameMode::InitializeMeshStencilIDs(TMap<FString, int> MeshStencilIDs,
// bool ignore_existing) { 	for (TObjectIterator<UStaticMeshComponent> comp; comp;
// ++comp){ 		InitializeObjectStencilID(*comp, ignore_existing);
// 	}
// 	for (TObjectIterator<USkinnedMeshComponent> comp; comp; ++comp) {
// 		InitializeObjectStencilID(*comp, ignore_existing);
// 	}
// 	for (TObjectIterator<ALandscapeProxy> comp; comp; ++comp) {
// 		InitializeObjectStencilID(*comp, ignore_existing);
// 	}
// }
void AHolodeckGameMode::InitializeMeshStencilIDs(
	TMap<FString, int> MeshStencilIDs,
	bool			   ignore_existing) {
	UWorld* World = GetWorld();
	if (!World)
		return;
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr) {
		FString ActorName = ActorItr->GetName();
		// --- FILTER: Skip Sky, Atmosphere, and other non-physical actors ---
		if (ActorName.Contains(TEXT("Sky"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("Atmosphere"), ESearchCase::IgnoreCase)
			|| ActorName.Contains(TEXT("Fog"), ESearchCase::IgnoreCase)) {
			continue;
		}
		AActor* Actor = *ActorItr;
		InitializeObjectStencilID(Actor, ignore_existing);
	}
}

void AHolodeckGameMode::RegisterUnknownMaterial(FString MaterialName) {
	// 1. Assign new ID
	int NewID = NextStencilID++;

	// 2. Assign Default Values (You can change these defaults)
	const float DefaultDensity = 10000.0f; // Approx Water/Generic
	const float DefaultSpeed = 10000.0f;
	float		Impedance = DefaultDensity * DefaultSpeed;

	// 3. Update Runtime Maps
	MaterialIDs.Add(MaterialName, NewID);
	IDImpedances.Add(NewID, Impedance);

	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("Discovered Unknown Material '%s'. Assigned ID: %d. Saving to CSV."),
		*MaterialName,
		NewID);

	// 4. Append to CSV
	FString filePath = FPaths::ProjectDir() + "../../materials.csv";

	// Format: Name,Density,Speed
	FString NewLine = FString::Printf(
		TEXT("\n%s,%.2f,%.2f\n"), *MaterialName, DefaultDensity, DefaultSpeed);

	// Append to file
	FFileHelper::SaveStringToFile(
		NewLine,
		*filePath,
		FFileHelper::EEncodingOptions::ForceAnsi,
		&IFileManager::Get(),
		FILEWRITE_Append);
}

void AHolodeckGameMode::InitializeAnnotation(
	TArray<AnnotatorSettings> annotator_settings) {
	// for (auto& annotator_setting : getSettings().annotator_settings) {  //
	// getSettings().annotator_settings is the cosys sim way of retrieving the settings
	// will need to replace this with a holoocean way of retrieving the settings
	for (const auto& annotator_setting : annotator_settings) {
		FString							name = annotator_setting.name;
		FObjectAnnotator::AnnotatorType type =
			FObjectAnnotator::AnnotatorType(annotator_setting.type);
		bool  set_direct = annotator_setting.set_direct;
		float max_view_distance = annotator_setting.max_view_distance;
		annotators_.Emplace(
			name,
			FObjectAnnotator(
				name,
				type,
				annotator_setting.show_by_default,
				set_direct,
				max_view_distance));
		annotators_[name].Initialize(this->GetLevel());
		// AddAnnotatorCamera(name, type, max_view_distance); was for cameras, not lidar
		// (or now sonar);
		ForceUpdateAnnotation(name);
		// updateAnnotation(name);  // I think unnecessary call as ForceUpdateAnnotation
		// already calls updateAnnotation
	}
}

void AHolodeckGameMode::InitializeInstanceSegmentation() {
	TArray<AActor*> cameras_found;
}

void AddAnnotatorCamera(
	FString							name,
	FObjectAnnotator::AnnotatorType type,
	float							max_view_distance) {
	// TODO: Implement camera addition logic
	//  if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("AddAnnotatorCamera is TODO for for now"));
}

void AHolodeckGameMode::updateAnnotation(FString annotation_name) {
	if (annotators_.Find(annotation_name)) {
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Sim could not find annotation layer %s to update."),
			*annotation_name);
	} else {
		TArray<TWeakObjectPtr<UPrimitiveComponent>> current_annotation_components =
			annotators_[annotation_name].GetAnnotationComponents();
		// cutting out camera functions
		TArray<UHolodeckGPUSonar*> gpuray_cameras_found;
		RunCommandOnGameThread(
			[this, &gpuray_cameras_found, annotation_name]() {
				auto* ContextGameWorld = GetWorld();
				if (ContextGameWorld) {
					for (TObjectIterator<UHolodeckGPUSonar> Itr; Itr; ++Itr) {
						if (Itr->GetWorld() == ContextGameWorld
							&& !Itr->bInstance_segmentation_
							&& Itr->bGenerate_ground_truth_
							&& Itr->annotation_name_ == annotation_name) {
							gpuray_cameras_found.Add(*Itr);
						}
					}
				}
			},
			true);
		FindAllActor<UHolodeckGPUSonar>(this, gpuray_cameras_found);
		for (auto gpuray_camera : gpuray_cameras_found) {
			UHolodeckGPUSonar* camera = static_cast<UHolodeckGPUSonar*>(gpuray_camera);
			if (!camera->bInstance_segmentation_ && camera->bGenerate_ground_truth_
				&& camera->annotation_name_ == annotation_name) {
				camera->updateAnnotation(current_annotation_components);
			}
		}
	}
}

void AHolodeckGameMode::RunCommandOnGameThread(
	TFunction<void()> InFunc,
	bool			  wait,
	const TStatId	  inStatId) {
	if (IsInGameThread())
		InFunc();
	else {
		FGraphEventRef task = FFunctionGraphTask::CreateAndDispatchWhenReady(
			MoveTemp(InFunc), inStatId, nullptr, ENamedThreads::GameThread);
		if (wait) {
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(task);
		}
	}
}

void AHolodeckGameMode::updateInstanceSegmentationAnnotation() {
	// TODO: implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("updateInstanceSegmentationAnnotation is TODO for now"));
}

AHolodeckGameMode* AHolodeckGameMode::getSimMode() {
	return SIMMODE;
}

std::vector<std::string> AHolodeckGameMode::GetAllInstanceSegmentationMeshIDs() {
	// TODO: Implement if needed, remove if not needed.
	return std::vector<std::string>();
}

TMap<UMeshComponent*, FString>
AHolodeckGameMode::GetInstanceSegmentationComponentToNameMap() {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("GetInstanceSegmentationComponentToNameMap is TODO for now"));
	return TMap<UMeshComponent*, FString>();
}

bool AHolodeckGameMode::SetMeshInstanceSegmentationID(
	const std::string& mesh_name,
	int				   object_id,
	bool			   is_name_regex,
	bool			   update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("SetMeshInstanceSegmentationID is TODO for now"));
	return false;
}

int AHolodeckGameMode::GetMeshInstanceSegmentationID(const std::string& mesh_name) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("GetMeshInstanceSegmentationID is TODO for now"));
	return -1;
}

std::vector<std::string>
AHolodeckGameMode::GetAllAnnotationMeshIDs(const std::string& annotation_name) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("GetAllAnnotationMeshIDs is TODO for now"));
	return std::vector<std::string>();
}

bool AHolodeckGameMode::SetMeshRGBAnnotationID(
	const std::string& annotation_name,
	const std::string& mesh_name,
	int				   object_id,
	bool			   is_name_regex,
	bool			   update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("SetMeshRGBAnnotationID is TODO for now"));
	return false;
}

bool AHolodeckGameMode::SetMeshRGBAnnotationColor(
	const std::string& annotation_name,
	const std::string& mesh_name,
	int				   r,
	int				   g,
	int				   b,
	bool			   is_name_regex,
	bool			   update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("SetMeshRGBAnnotationColor is TODO for now"));
	return false;
}

int AHolodeckGameMode::GetMeshRGBAnnotationID(
	const std::string& annotation_name,
	const std::string& mesh_name) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("GetMeshRGBAnnotationID is TODO for now"));
	return -1;
}

std::string AHolodeckGameMode::GetMeshRGBAnnotationColor(
	const std::string& annotation_name,
	const std::string& mesh_name) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("GetMeshRGBAnnotationColor is TODO for now"));
	return "";
}

bool AHolodeckGameMode::AddNewActorToInstanceSegmentation(
	AActor* Actor,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("AddNewActorToInstanceSegmentation is TODO for now"));
	return false;
}

bool AHolodeckGameMode::DeleteActorFromInstanceSegmentation(
	AActor* Actor,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("DeleteActorFromInstanceSegmentation is TODO for now"));
	return false;
}

void AHolodeckGameMode::ForceUpdateInstanceSegmentation() {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck, Warning, TEXT("ForceUpdateInstanceSegmentation is TODO for now"));
}

bool AHolodeckGameMode::DoesAnnotationLayerExist(FString annotation_name) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("DoesAnnotationLayerExist is TODO for now"));
	return false;
}

bool AHolodeckGameMode::AddNewActorToAnnotation(
	FString annotation_name,
	AActor* Actor,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("AddNewActorToAnnotation is TODO for now"));
	return false;
}

bool AHolodeckGameMode::AddRGBDirectAnnotationTagToActor(
	FString annotation_name,
	AActor* actor,
	FColor	color,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck, Warning, TEXT("AddRGBDirectAnnotationTagToActor is TODO for now"));
	return false;
}

bool AHolodeckGameMode::UpdateRGBDirectAnnotationTagToActor(
	FString annotation_name,
	AActor* actor,
	FColor	color,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("UpdateRGBDirectAnnotationTagToActor is TODO for now"));
	return false;
}

bool AHolodeckGameMode::AddRGBIndexAnnotationTagToActor(
	FString annotation_name,
	AActor* actor,
	int32	index,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck, Warning, TEXT("AddRGBIndexAnnotationTagToActor is TODO for now"));
	return false;
}

bool AHolodeckGameMode::UpdateRGBIndexAnnotationTagToActor(
	FString annotation_name,
	AActor* actor,
	int32	index,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("UpdateRGBIndexAnnotationTagToActor is TODO for now"));
	return false;
}

bool AHolodeckGameMode::AddRGBDirectAnnotationTagToComponent(
	FString			annotation_name,
	UMeshComponent* component,
	FColor			color,
	bool			update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("AddRGBDirectAnnotationTagToComponent is TODO for now"));
	return false;
}

bool AHolodeckGameMode::UpdateRGBDirectAnnotationTagToComponent(
	FString			annotation_name,
	UMeshComponent* component,
	FColor			color,
	bool			update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("UpdateRGBDirectAnnotationTagToComponent is TODO for now"));
	return false;
}

bool AHolodeckGameMode::AddRGBIndexAnnotationTagToComponent(
	FString			annotation_name,
	UMeshComponent* component,
	int32			index,
	bool			update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("AddRGBIndexAnnotationTagToComponent is TODO for now"));
	return false;
}

bool AHolodeckGameMode::UpdateRGBIndexAnnotationTagToComponent(
	FString			annotation_name,
	UMeshComponent* component,
	int32			index,
	bool			update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(
		LogHolodeck,
		Warning,
		TEXT("UpdateRGBIndexAnnotationTagToComponent is TODO for now"));
	return false;
}

bool AHolodeckGameMode::DeleteActorFromAnnotation(
	FString annotation_name,
	AActor* Actor,
	bool	update_annotation) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("DeleteActorFromAnnotation is TODO for now"));
	return false;
}

void AHolodeckGameMode::ForceUpdateAnnotation(FString annotation_name) {
	if (annotators_.Contains(annotation_name) == false) {
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Sim could not find annotation layer %s to update."),
			*annotation_name);
	} else {
		annotators_[annotation_name].UpdateAnnotationComponents(GetWorld());
		updateAnnotation(annotation_name);
	}
}

bool AHolodeckGameMode::IsAnnotationRGBValid(FString annotation_name, FColor color) {
	// TODO: Implement if needed, remove if not needed.
	UE_LOG(LogHolodeck, Warning, TEXT("IsAnnotationRGBValid is TODO for now"));
	return false;
}
