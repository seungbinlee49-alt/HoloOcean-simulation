// Config-driven scene definition for the Mado report environment family. Facies zones, anchor
// stones, reef cues, and wreck spawns used to be hardcoded C++ constants in HolodeckGameMode.cpp
// / HolodeckRaycastSonar.cpp, which meant every new scene variant required a C++ patch + full UE
// rebuild. This loads the same data from a JSON file at runtime instead, so new scene variants
// (or parameter sweeps of an existing one) are a JSON edit + engine restart, not a rebuild.
#pragma once
#include "CoreMinimal.h"

struct FMadoMaterialConfig {
	float DensityKgm3 = 1298.0f;
	float SoundSpeedMps = 1564.0f;
	float Impedance() const { return DensityKgm3 * SoundSpeedMps; }
};

struct FMadoFaciesZoneConfig {
	FString			   Id;
	FString			   Evidence;
	float			   CenterX = 0.0f;
	float			   CenterY = 0.0f;
	float			   YawDeg = 0.0f;
	float			   RadiusX = 1.0f;
	float			   RadiusY = 1.0f;
	FMadoMaterialConfig TargetMaterial;
};

struct FMadoAnchorStoneConfig {
	FString Id;
	float	CenterX = 0.0f;
	float	CenterY = 0.0f;
	float	YawDeg = 0.0f;
	float	LengthCm = 0.0f;
	float	WidthCm = 0.0f;
	float	ThicknessCm = 0.0f;
	FString RockType;
};

struct FMadoReefCueConfig {
	FString Id;
	float	CenterX = 0.0f;
	float	CenterY = 0.0f;
	float	YawDeg = 0.0f;
	float	LengthM = 0.0f;
	float	WidthM = 0.0f;
	float	ThicknessM = 0.0f;
};

// Placement/burial parameters for a wreck actor. The 3D mesh asset itself is out of scope here
// (SSS-team / modeling responsibility) -- this only controls where and how it sits relative to
// the seafloor, which is squarely environment-authoring scope.
struct FMadoWreckSpawnConfig {
	FString Id;
	FString Evidence;
	float	CenterX = 0.0f;
	float	CenterY = 0.0f;
	float	YawDeg = 0.0f;
	float	LengthM = 1.0f;
	float	WidthM = 1.0f;
	float	HeightM = 1.0f;
	// 0 = fully proud of the seafloor, 1 = fully buried (top flush with surrounding grade).
	float BurialFraction = 0.0f;
	bool  bScourPitEnabled = false;
	// Scour pit radius as a multiple of the hull length, and max depth in meters, only used if
	// bScourPitEnabled.
	float ScourRadiusLengthMultiple = 1.4f;
	float ScourDepthM = 0.15f;
};

struct FMadoTextureNoiseConfig {
	float FineCellSizeM = 0.3f;
	float CoarseCellSizeM = 0.3f;
	float CoarseFreqScale = 0.3f;
	float FineWeight = 0.6f;
	float CoarseWeight = 0.4f;
	float AmpBase = 0.03f;
	float AmpZoneWeightScale = 0.05f;
	float NearNadirFadeStartM = 0.0f;
	float NearNadirFadeEndM = 5.0f;
};

struct FMadoSceneConfig {
	bool	bValid = false;
	FString SceneName;
	FString SourceJsonPath;

	FMadoMaterialConfig BaselineMaterial;
	FMadoMaterialConfig SoftMudBaselineMaterial;

	float ActiveWindowXStart = 60.0f;
	float ActiveWindowXEnd = 72.0f;
	float ActiveWindowYStart = 50.0f;
	float ActiveWindowYEnd = 62.0f;

	float BlendStrength = 0.35f;

	FMadoTextureNoiseConfig TextureNoise;
	float					DomainWarpCellSizeM = 7.0f;
	float					DomainWarpFractionOfRadius = 0.22f;

	// Filename of a heightfield CSV under Content/Config/mado_terrain/ (see FMadoTerrainData).
	// Empty/unrecognized falls back to mado_report_environment_v1_terrain.csv for backward
	// compatibility with callers that request khoa terrain-following without an active scene.
	FString TerrainDataSource;

	TArray<FMadoFaciesZoneConfig>  FaciesZones;
	TArray<FMadoAnchorStoneConfig> AnchorStones;
	TArray<FMadoReefCueConfig>	    ReefEdgeCues;
	TArray<FMadoWreckSpawnConfig>  WreckSpawns;
};

// Loaded from a Content/Config/mado_terrain/*.csv heightfield (columns: ix,iy,x_m,y_m,depth_m,
// seabed_z_m; rows in raster order, iy outer / ix inner -- see
// 04_code/environment_data/generate_khoa_smooth_terrain_header_v2.py's --out-smoothed-csv output,
// which is exactly this format). Replaces the old compiled ShipwreckKhoaSmoothTerrainData.generated.h
// so a new site's terrain is a data file, not a C++ rebuild.
struct FMadoTerrainData {
	bool  bValid = false;
	int32 GridX = 0;
	int32 GridY = 0;
	float XMinM = 0.0f;
	float XMaxM = 0.0f;
	float YMinM = 0.0f;
	float YMaxM = 0.0f;
	float DepthMinM = 0.0f;
	float DepthMaxM = 0.0f;
	TArray<float> XValues;
	TArray<float> YValues;
	TArray<float> DepthM; // row-major, index = iy * GridX + ix

	// Bilinear-sampled water depth (positive, meters) at local (X, Y). Mirrors the sampling math
	// previously inlined in HolodeckGameMode.cpp::ShipwreckProjectKhoaDepthAt exactly.
	float DepthAt(float X, float Y) const;
};

// Resolves HOLOOCEAN_SHIPWRECK_SCENE_PRESET to a concrete config and caches it (loaded once per
// process). If the env var value ends in ".json" it is treated as a config file path (relative
// to Content/Config/mado_scenes/ unless absolute); otherwise "mado_report_environment_v1" /
// "mado_report_environment_visual_debug_v1" resolve to the bundled default JSON of the same
// name, so existing capture scripts that pass the preset name keep working unchanged.
const FMadoSceneConfig& GetActiveMadoSceneConfig();

// True if the resolved config loaded successfully (i.e. some Mado report scene is active).
bool IsMadoReportSceneActive();

// Resolves and caches (once per process) the terrain heightfield to use: the active scene's
// "terrain_data_source" JSON field if a scene is active and sets one, otherwise
// mado_report_environment_v1_terrain.csv (matches the old hardcoded-header behavior, so any
// caller using khoa terrain-following without an active Mado scene is unaffected).
const FMadoTerrainData& GetActiveMadoTerrainData();
