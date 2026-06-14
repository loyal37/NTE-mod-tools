#pragma once

#include "CoreMinimal.h"

enum class EHTBlueprintToggleMode : uint8
{
	MaterialSection,
	Texture
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
	int32 SectionIndex = 0;
	int32 LODIndex = 0;
	int32 MaterialElementIndex = 0;
	TArray<int32> MaterialElementIndices;
	FString SourceMaterialPath;
	FString TextureParameterName;
	TArray<FString> TexturePaths;
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
