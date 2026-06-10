#pragma once

#include "CoreMinimal.h"

struct FHTBlueprintToggleGeneratorParams
{
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
