#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceConstant;
class UTexture2D;

struct FHTMaterialInstanceBuilderParams
{
	FString MaterialPath;
	FString InstanceName;
	FString BaseColorTexturePath;
	FString IDTexturePath;
	FString LightMapTexturePath;
	FString NormalTexturePath;
	bool bSaveAssets = true;
};

struct FHTMaterialInstanceBuilderResult
{
	bool bSuccess = false;
	FString InstancePath;
	TArray<FString> Errors;
	TArray<FString> Notes;

	FString ToDisplayString() const;
};

class FHTMaterialInstanceBuilder
{
public:
	static FHTMaterialInstanceBuilderResult Build(const FHTMaterialInstanceBuilderParams& Params);
};
