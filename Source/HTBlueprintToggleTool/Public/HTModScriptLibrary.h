#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HTModScriptLibrary.generated.h"

class UMaterialExpression;
class UMaterialInstance;
class UMaterialInstanceConstant;
class UMaterialInterface;

// Script entry points of the HT tools (Python: unreal.HTModScriptLibrary), used by editor
// automation such as the Unreal Engine MCP server. Functions that take or return JSON use
// compact UTF-8 JSON strings; every result object has "success" and "errors".
//
// Nothing here saves an asset unless the caller passes bSaveAssets / "save": true. Edits join
// the caller's undo transaction when one is open, otherwise they open their own.
UCLASS()
class UHTModScriptLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Materials --------------------------------------------------------------------------

	// Parameter names the material instance editor lists for Material, as the engine computes them
	// (FMaterialEditorUtilities::GetVisibleMaterialParameters: reachable from the material outputs
	// under the instance's static switch values). A plain material is seen through a transient
	// child instance with no overrides.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Materials")
	static TArray<FName> GetVisibleMaterialParameters(UMaterialInterface* Material);

	// Nodes of a Material or MaterialFunction graph (comments excluded), straight from its
	// expression collection.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Materials")
	static TArray<UMaterialExpression*> GetMaterialExpressions(UObject* MaterialOrFunction);

	// True when Instance itself (not a parent) overrides the static switch ParameterName.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Materials")
	static bool IsStaticSwitchOverridden(UMaterialInstance* Instance, FName ParameterName);

	// Removes Instance's own override of a static switch or static component mask parameter.
	// Returns false when there was none.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Materials")
	static bool ClearMaterialInstanceParameter(UMaterialInstanceConstant* Instance, FName ParameterName);

	// ---- Asset editors ----------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "HT|Script|Editor")
	static TArray<UObject*> GetOpenAssetEditors();

	UFUNCTION(BlueprintCallable, Category = "HT|Script|Editor")
	static bool IsAssetOpenInEditor(UObject* Asset);

	// ---- Toggle trees (HTToggleGraph assets) ------------------------------------------------

	// Target settings, every node with its properties, its input links and where its output goes.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Toggle Graph")
	static FString DescribeToggleGraph(const FString& ToggleGraphPath);

	// What Compile checks, without touching any asset: the condition model, its validation, the
	// state variables it resolves to, and the part slot report.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Toggle Graph")
	static FString CheckToggleGraph(const FString& ToggleGraphPath);

	// The Refresh button: moves Material IDs back onto their slot names and binds unbound parts.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Toggle Graph")
	static FString RefreshToggleGraphSlots(const FString& ToggleGraphPath);

	// The Compile button: generates the managed region of the Animation Blueprint.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Toggle Graph")
	static FString GenerateToggleGraph(const FString& ToggleGraphPath, bool bSaveAssets);

	// Applies a JSON array of operations to the graph in one undo step: add, set, connect,
	// disconnect, remove, set_target. See the implementation for the operation fields. When an
	// operation fails the whole batch is undone unless "keep_partial" is given.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Toggle Graph")
	static FString EditToggleGraph(const FString& ToggleGraphPath, const FString& OperationsJson);

	// ---- Generators -------------------------------------------------------------------------

	// The main panel's Generate button for the four classic modes (visibility, texture, material,
	// texture tuner), parameters as JSON.
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Generators")
	static FString GenerateToggle(const FString& ParamsJson);

	// The Material Instance tab: rebuilds the parent's texture parameter nodes and creates a
	// four-texture material instance (BaseColor, ID_Tex, LightMap, NormalMap).
	UFUNCTION(BlueprintCallable, Category = "HT|Script|Generators")
	static FString BuildMaterialInstance(const FString& ParamsJson);
};
