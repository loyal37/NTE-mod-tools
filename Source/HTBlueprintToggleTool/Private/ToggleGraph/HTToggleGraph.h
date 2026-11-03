#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "HTToggleGraph.generated.h"

class UAnimBlueprint;
class UBlueprint;
class UHTToggleGraph;
class USkeletalMesh;

// The toggle tree graph itself. Lives inside a UHTToggleGraph asset.
UCLASS()
class UHTToggleEdGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UHTToggleGraph* GetToggleGraphAsset() const;
};

// Editor-only asset: a LoyalTools-style nested toggle blueprint for one Animation Blueprint.
// "编译" converts the graph to FHTNestedToggleGraph and runs GenerateNestedVisibility.
UCLASS(BlueprintType)
class UHTToggleGraph : public UObject
{
	GENERATED_BODY()

public:
	UHTToggleGraph();

	// Animation Blueprint that receives the generated nodes.
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "动画蓝图"))
	TSoftObjectPtr<UAnimBlueprint> AnimBlueprint;

	// SaveGame Blueprint that stores every state variable.
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "SaveGame 蓝图"))
	TSoftObjectPtr<UBlueprint> SaveGameBlueprint;

	// Appended to every SaveGame slot name so characters never share slots.
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "角色名"))
	FString CharacterName;

	// Name of the managed region: "HT Toggle Tree - <TreeName>" and "HTN_Apply_<TreeName>". Empty = asset name.
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "蓝图名"))
	FString TreeName;

	UPROPERTY(EditAnywhere, Category = "Target", AdvancedDisplay, meta = (DisplayName = "Section Index", ClampMin = 0))
	int32 SectionIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Target", AdvancedDisplay, meta = (DisplayName = "LOD Index", ClampMin = 0))
	int32 LODIndex = 0;

	// Save the Animation Blueprint and SaveGame Blueprint after a successful compile.
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "编译后保存蓝图"))
	bool bSaveBlueprintsAfterCompile = true;

	UPROPERTY()
	TObjectPtr<UEdGraph> Graph;

	// Creates the graph (with its default generate node) when the asset has none yet.
	UEdGraph* EnsureGraph();
	FString GetEffectiveTreeName() const;
	UAnimBlueprint* LoadAnimBlueprint() const;
	// Preview mesh of the bound Animation Blueprint, used to list material slots on part nodes.
	USkeletalMesh* ResolvePreviewMesh() const;
};
