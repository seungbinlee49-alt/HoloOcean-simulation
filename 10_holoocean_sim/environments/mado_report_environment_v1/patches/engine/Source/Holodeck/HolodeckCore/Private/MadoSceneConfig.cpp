// MIT License (c) 2026 see LICENSE file
#include "MadoSceneConfig.h"
#include "Holodeck.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {

float GetNum(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, float Default) {
	double Value = Default;
	if (Obj.IsValid() && Obj->TryGetNumberField(Field, Value)) {
		return static_cast<float>(Value);
	}
	return Default;
}

FString GetStr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FString& Default) {
	FString Value;
	if (Obj.IsValid() && Obj->TryGetStringField(Field, Value)) {
		return Value;
	}
	return Default;
}

bool GetBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, bool Default) {
	bool Value = Default;
	if (Obj.IsValid() && Obj->TryGetBoolField(Field, Value)) {
		return Value;
	}
	return Default;
}

void GetPair(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, float& OutA, float& OutB) {
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj.IsValid() && Obj->TryGetArrayField(Field, Arr) && Arr && Arr->Num() >= 2) {
		OutA = static_cast<float>((*Arr)[0]->AsNumber());
		OutB = static_cast<float>((*Arr)[1]->AsNumber());
	}
}

FMadoMaterialConfig ParseMaterial(const TSharedPtr<FJsonObject>& Obj, FMadoMaterialConfig Default) {
	FMadoMaterialConfig Result = Default;
	if (Obj.IsValid()) {
		Result.DensityKgm3 = GetNum(Obj, TEXT("density_kgm3"), Default.DensityKgm3);
		Result.SoundSpeedMps = GetNum(Obj, TEXT("sound_speed_mps"), Default.SoundSpeedMps);
	}
	return Result;
}

FMadoSceneConfig ParseSceneConfigJson(const FString& JsonText, const FString& SourcePath) {
	FMadoSceneConfig Config;
	TSharedPtr<FJsonObject>	   Root;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) {
		UE_LOG(LogHolodeck, Warning, TEXT("MadoSceneConfig: failed to parse JSON at %s"), *SourcePath);
		return Config;
	}

	Config.SceneName = GetStr(Root, TEXT("scene_name"), TEXT(""));
	Config.SourceJsonPath = SourcePath;

	const TSharedPtr<FJsonObject>* BaselineObj = nullptr;
	if (Root->TryGetObjectField(TEXT("baseline_material"), BaselineObj)) {
		Config.BaselineMaterial = ParseMaterial(*BaselineObj, Config.BaselineMaterial);
	}
	const TSharedPtr<FJsonObject>* SoftMudObj = nullptr;
	if (Root->TryGetObjectField(TEXT("soft_mud_baseline_material"), SoftMudObj)) {
		Config.SoftMudBaselineMaterial = ParseMaterial(*SoftMudObj, Config.SoftMudBaselineMaterial);
	}

	const TSharedPtr<FJsonObject>* ActiveWindowObj = nullptr;
	if (Root->TryGetObjectField(TEXT("active_window"), ActiveWindowObj)) {
		Config.ActiveWindowXStart = GetNum(*ActiveWindowObj, TEXT("x_falloff_start_m"), Config.ActiveWindowXStart);
		Config.ActiveWindowXEnd = GetNum(*ActiveWindowObj, TEXT("x_falloff_end_m"), Config.ActiveWindowXEnd);
		Config.ActiveWindowYStart = GetNum(*ActiveWindowObj, TEXT("y_falloff_start_m"), Config.ActiveWindowYStart);
		Config.ActiveWindowYEnd = GetNum(*ActiveWindowObj, TEXT("y_falloff_end_m"), Config.ActiveWindowYEnd);
	}

	Config.BlendStrength = GetNum(Root, TEXT("blend_strength"), Config.BlendStrength);

	const TSharedPtr<FJsonObject>* TextureNoiseObj = nullptr;
	if (Root->TryGetObjectField(TEXT("texture_noise"), TextureNoiseObj)) {
		FMadoTextureNoiseConfig& T = Config.TextureNoise;
		T.FineCellSizeM = GetNum(*TextureNoiseObj, TEXT("fine_cell_size_m"), T.FineCellSizeM);
		T.CoarseCellSizeM = GetNum(*TextureNoiseObj, TEXT("coarse_cell_size_m"), T.CoarseCellSizeM);
		T.CoarseFreqScale = GetNum(*TextureNoiseObj, TEXT("coarse_freq_scale"), T.CoarseFreqScale);
		T.FineWeight = GetNum(*TextureNoiseObj, TEXT("fine_weight"), T.FineWeight);
		T.CoarseWeight = GetNum(*TextureNoiseObj, TEXT("coarse_weight"), T.CoarseWeight);
		T.AmpBase = GetNum(*TextureNoiseObj, TEXT("amp_base"), T.AmpBase);
		T.AmpZoneWeightScale = GetNum(*TextureNoiseObj, TEXT("amp_zone_weight_scale"), T.AmpZoneWeightScale);
		T.NearNadirFadeStartM = GetNum(*TextureNoiseObj, TEXT("near_nadir_fade_start_m"), T.NearNadirFadeStartM);
		T.NearNadirFadeEndM = GetNum(*TextureNoiseObj, TEXT("near_nadir_fade_end_m"), T.NearNadirFadeEndM);
	}

	Config.DomainWarpCellSizeM = GetNum(Root, TEXT("domain_warp_cell_size_m"), Config.DomainWarpCellSizeM);
	Config.DomainWarpFractionOfRadius = GetNum(Root, TEXT("domain_warp_fraction_of_radius"), Config.DomainWarpFractionOfRadius);

	const TArray<TSharedPtr<FJsonValue>>* ZonesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("facies_zones"), ZonesArr) && ZonesArr) {
		for (const TSharedPtr<FJsonValue>& Item : *ZonesArr) {
			const TSharedPtr<FJsonObject> ItemObj = Item->AsObject();
			if (!ItemObj.IsValid()) {
				continue;
			}
			FMadoFaciesZoneConfig Zone;
			Zone.Id = GetStr(ItemObj, TEXT("id"), TEXT(""));
			Zone.Evidence = GetStr(ItemObj, TEXT("evidence"), TEXT(""));
			GetPair(ItemObj, TEXT("center_m"), Zone.CenterX, Zone.CenterY);
			Zone.YawDeg = GetNum(ItemObj, TEXT("yaw_deg"), 0.0f);
			GetPair(ItemObj, TEXT("radius_m"), Zone.RadiusX, Zone.RadiusY);
			const TSharedPtr<FJsonObject>* TargetObj = nullptr;
			if (ItemObj->TryGetObjectField(TEXT("target_material"), TargetObj)) {
				Zone.TargetMaterial = ParseMaterial(*TargetObj, Zone.TargetMaterial);
			}
			Config.FaciesZones.Add(Zone);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* StonesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("anchor_stones"), StonesArr) && StonesArr) {
		for (const TSharedPtr<FJsonValue>& Item : *StonesArr) {
			const TSharedPtr<FJsonObject> ItemObj = Item->AsObject();
			if (!ItemObj.IsValid()) {
				continue;
			}
			FMadoAnchorStoneConfig Stone;
			Stone.Id = GetStr(ItemObj, TEXT("id"), TEXT(""));
			GetPair(ItemObj, TEXT("center_m"), Stone.CenterX, Stone.CenterY);
			Stone.YawDeg = GetNum(ItemObj, TEXT("yaw_deg"), 0.0f);
			Stone.LengthCm = GetNum(ItemObj, TEXT("length_cm"), 0.0f);
			Stone.WidthCm = GetNum(ItemObj, TEXT("width_cm"), 0.0f);
			Stone.ThicknessCm = GetNum(ItemObj, TEXT("thickness_cm"), 0.0f);
			Stone.RockType = GetStr(ItemObj, TEXT("rock_type"), TEXT(""));
			Config.AnchorStones.Add(Stone);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ReefArr = nullptr;
	if (Root->TryGetArrayField(TEXT("reef_edge_cues"), ReefArr) && ReefArr) {
		for (const TSharedPtr<FJsonValue>& Item : *ReefArr) {
			const TSharedPtr<FJsonObject> ItemObj = Item->AsObject();
			if (!ItemObj.IsValid()) {
				continue;
			}
			FMadoReefCueConfig Reef;
			Reef.Id = GetStr(ItemObj, TEXT("id"), TEXT(""));
			GetPair(ItemObj, TEXT("center_m"), Reef.CenterX, Reef.CenterY);
			Reef.YawDeg = GetNum(ItemObj, TEXT("yaw_deg"), 0.0f);
			Reef.LengthM = GetNum(ItemObj, TEXT("length_m"), 0.0f);
			Reef.WidthM = GetNum(ItemObj, TEXT("width_m"), 0.0f);
			Reef.ThicknessM = GetNum(ItemObj, TEXT("thickness_m"), 0.0f);
			Config.ReefEdgeCues.Add(Reef);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* WrecksArr = nullptr;
	if (Root->TryGetArrayField(TEXT("wreck_spawns"), WrecksArr) && WrecksArr) {
		for (const TSharedPtr<FJsonValue>& Item : *WrecksArr) {
			const TSharedPtr<FJsonObject> ItemObj = Item->AsObject();
			if (!ItemObj.IsValid()) {
				continue;
			}
			FMadoWreckSpawnConfig Wreck;
			Wreck.Id = GetStr(ItemObj, TEXT("id"), TEXT(""));
			Wreck.Evidence = GetStr(ItemObj, TEXT("evidence"), TEXT(""));
			GetPair(ItemObj, TEXT("center_m"), Wreck.CenterX, Wreck.CenterY);
			Wreck.YawDeg = GetNum(ItemObj, TEXT("yaw_deg"), 0.0f);
			Wreck.LengthM = GetNum(ItemObj, TEXT("length_m"), 1.0f);
			Wreck.WidthM = GetNum(ItemObj, TEXT("width_m"), 1.0f);
			Wreck.HeightM = GetNum(ItemObj, TEXT("height_m"), 1.0f);
			Wreck.BurialFraction = FMath::Clamp(GetNum(ItemObj, TEXT("burial_fraction"), 0.0f), 0.0f, 1.0f);
			Wreck.bScourPitEnabled = GetBool(ItemObj, TEXT("scour_pit_enabled"), false);
			Wreck.ScourRadiusLengthMultiple = GetNum(ItemObj, TEXT("scour_radius_length_multiple"), Wreck.ScourRadiusLengthMultiple);
			Wreck.ScourDepthM = GetNum(ItemObj, TEXT("scour_depth_m"), Wreck.ScourDepthM);
			Config.WreckSpawns.Add(Wreck);
		}
	}

	Config.bValid = true;
	return Config;
}

// Resolves the env var value to an absolute JSON path, or empty if it doesn't name a Mado scene.
FString ResolveConfigPath(const FString& PresetEnvValue) {
	if (PresetEnvValue.IsEmpty()) {
		return FString();
	}
	FString Candidate;
	if (PresetEnvValue.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase)) {
		Candidate = PresetEnvValue;
	} else if (
		PresetEnvValue.Equals(TEXT("mado_report_environment_v1"), ESearchCase::IgnoreCase) ||
		PresetEnvValue.Equals(TEXT("mado_report_environment_visual_debug_v1"), ESearchCase::IgnoreCase)) {
		Candidate = TEXT("mado_report_environment_v1.json");
	} else {
		return FString();
	}
	if (FPaths::IsRelative(Candidate)) {
		Candidate = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Config/mado_scenes"), Candidate);
	}
	return FPaths::ConvertRelativePathToFull(Candidate);
}

} // namespace

const FMadoSceneConfig& GetActiveMadoSceneConfig() {
	static const FMadoSceneConfig Config = []() {
		const FString PresetEnvValue = FPlatformMisc::GetEnvironmentVariable(TEXT("HOLOOCEAN_SHIPWRECK_SCENE_PRESET"));
		const FString ConfigPath = ResolveConfigPath(PresetEnvValue);
		if (ConfigPath.IsEmpty()) {
			return FMadoSceneConfig();
		}
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath)) {
			UE_LOG(LogHolodeck, Warning, TEXT("MadoSceneConfig: could not read config file %s (HOLOOCEAN_SHIPWRECK_SCENE_PRESET=%s)"), *ConfigPath, *PresetEnvValue);
			return FMadoSceneConfig();
		}
		FMadoSceneConfig Parsed = ParseSceneConfigJson(JsonText, ConfigPath);
		UE_LOG(
			LogHolodeck,
			Log,
			TEXT("MadoSceneConfig: loaded '%s' from %s (zones=%d anchor_stones=%d reef_cues=%d wrecks=%d)"),
			*Parsed.SceneName,
			*ConfigPath,
			Parsed.FaciesZones.Num(),
			Parsed.AnchorStones.Num(),
			Parsed.ReefEdgeCues.Num(),
			Parsed.WreckSpawns.Num());
		return Parsed;
	}();
	return Config;
}

bool IsMadoReportSceneActive() {
	return GetActiveMadoSceneConfig().bValid;
}
