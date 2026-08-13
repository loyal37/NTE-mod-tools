#pragma once

#include "CoreMinimal.h"

enum class EHTBlueprintToggleMode : uint8
{
	MaterialSection,
	Texture,
	MaterialInterface
};

struct FHTTextureMaterialSlotGroup
{
	FString SourceMaterialPath;
	TArray<int32> MaterialElementIndices;
	TArray<FString> TexturePaths;
};

struct FHTMaterialVisibilityGroup
{
	TArray<int32> MaterialIDs;
};

struct FHTBlueprintToggleGeneratorParams
{
	EHTBlueprintToggleMode Mode = EHTBlueprintToggleMode::MaterialSection;
	FString AnimBlueprintPath;
	FString SaveGameBlueprintPath;
	FString ToggleVariableName;
	FString SaveVariableName;
	FString SlotName;
	FString KeyName;
	int32 MaterialID = 16;
	TArray<int32> MaterialIDs;
	bool bToggleMaterialIDsTogether = false;
	TArray<FHTMaterialVisibilityGroup> MaterialVisibilityGroups;
	int32 InitialState = 0;
	int32 SectionIndex = 0;
	int32 LODIndex = 0;
	int32 MaterialElementIndex = 0;
	TArray<int32> MaterialElementIndices;
	FString SourceMaterialPath;
	TArray<FHTTextureMaterialSlotGroup> TextureMaterialSlotGroups;
	FString TextureParameterName;
	TArray<FString> TexturePaths;
	TArray<FString> MaterialInterfacePaths;
	bool bGenerateInitializeGraph = true;
	bool bGenerateUpdateGraph = true;
	bool bSaveAssets = true;
};

struct FHTBlueprintToggleGeneratorResult
{
	bool bSuccess = false;
	TArray<FString> Messages;
	TArray<FString> Errors;

	FString ToDisplayString() const;
};

class FHTBlueprintToggleGenerator
{
public:
	static FHTBlueprintToggleGeneratorResult Generate(const FHTBlueprintToggleGeneratorParams& Params);
};
