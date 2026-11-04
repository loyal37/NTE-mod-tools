#pragma once

#include "CoreMinimal.h"

enum class EHTBlueprintToggleMode : uint8
{
	MaterialSection,
	Texture,
	MaterialInterface,
	TextureChannelTuner
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
	bool bIncludeHiddenMaterialState = true;
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
	bool bTuneLightMap = false;
	bool bTuneIDTexture = false;
	FString LightMapTexturePath;
	FString IDTexturePath;
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

// ---------------------------------------------------------------------------
// Nested visibility ("toggle tree"). This is an independent entry point: it does
// not take part in EHTBlueprintToggleMode and leaves the modes above untouched.
// ---------------------------------------------------------------------------

struct FHTNestedToggleSwitch
{
	// Identifier of the authoring node; reported back in error locations.
	FString NodeId;
	// Free-form note from the authoring node. Not shown in game: the panel row and the metadata
	// label field use the state variable name.
	FString Label;
	// Optional explicit state variable name. Empty = derived from the graph and node id.
	// Switches that share a key or an explicit variable name share one state variable.
	FString VariableName;
	// Key specification understood by the existing key parser (for example "alt 6").
	FString KeyName;
	int32 OptionCount = 2;
	int32 InitialOption = 0;
	// Optional per-option labels. Missing or empty entries are filled by the generator.
	TArray<FString> OptionLabels;
	// Parent attachment. Empty ParentNodeId = root switch.
	FString ParentNodeId;
	int32 ParentOption = INDEX_NONE;
};

struct FHTNestedTogglePathStep
{
	FString SwitchNodeId;
	int32 Option = 0;
};

struct FHTNestedTogglePart
{
	FString NodeId;
	TArray<int32> MaterialIDs;
	// Conditions from the root switch down to the option this part hangs on. Empty = always visible.
	TArray<FHTNestedTogglePathStep> Path;
};

struct FHTNestedToggleGraph
{
	// Managed region name: "HT Toggle Tree - <Name>" comment and "HTN_Apply_<Name>" event.
	FString Name;
	TArray<FHTNestedToggleSwitch> Switches;
	TArray<FHTNestedTogglePart> Parts;
};

struct FHTNestedToggleIssue
{
	TArray<FString> NodeIds;
	FString Message;

	FString ToDisplayString() const;
};

struct FHTNestedToggleStateVariable
{
	FName VariableName;
	// Distinct raw key specifications that advance this variable (normally one).
	TArray<FString> KeyNames;
	int32 OptionCount = 2;
	int32 InitialOption = 0;
	// Indices into FHTNestedToggleGraph::Switches.
	TArray<int32> SwitchIndices;
};

struct FHTNestedToggleCondition
{
	int32 VariableIndex = INDEX_NONE;
	int32 Option = 0;
};

struct FHTNestedToggleCompiledPart
{
	int32 PartIndex = INDEX_NONE;
	TArray<int32> MaterialIDs;
	TArray<FHTNestedToggleCondition> Conditions;
};

struct FHTNestedToggleCompiledGraph
{
	FString Name;
	TArray<FHTNestedToggleStateVariable> Variables;
	// One entry per FHTNestedToggleGraph::Switches entry: its state variable index.
	TArray<int32> SwitchVariableIndices;
	TArray<FHTNestedToggleCompiledPart> Parts;
};

struct FHTNestedToggleGeneratorParams
{
	FString AnimBlueprintPath;
	FString SaveGameBlueprintPath;
	// Appended to every SaveGame slot name ("<Variable> <CharacterName>") so characters never share slots.
	FString CharacterName;
	int32 SectionIndex = 0;
	int32 LODIndex = 0;
	FHTNestedToggleGraph Graph;
	bool bSaveAssets = true;
};

struct FHTNestedToggleGeneratorResult
{
	bool bSuccess = false;
	TArray<FHTNestedToggleIssue> Errors;
	TArray<FString> Messages;
	// Filled on success: the state variables the generated nodes manage.
	TArray<FHTNestedToggleStateVariable> Variables;

	FString ToDisplayString() const;
};

class FHTBlueprintToggleGenerator
{
public:
	static FHTBlueprintToggleGeneratorResult Generate(const FHTBlueprintToggleGeneratorParams& Params);

	// Nested visibility: validates the condition model and resolves shared state variables
	// without touching any asset. Returns false and fills OutErrors (with node ids) on failure.
	static bool CompileNestedToggleGraph(const FHTNestedToggleGraph& Graph, FHTNestedToggleCompiledGraph& OutCompiled, TArray<FHTNestedToggleIssue>& OutErrors);
	// Nested visibility: replaces the managed "HT Toggle Tree - <Name>" region of the Animation
	// Blueprint with per-part AND conditions, per-variable key handlers and SaveGame restore.
	static FHTNestedToggleGeneratorResult GenerateNestedVisibility(const FHTNestedToggleGeneratorParams& Params);
	static FString MakeNestedToggleApplyEventName(const FString& GraphName);
	static FString MakeNestedToggleMetadataPrefix(const FString& GraphName);
	static FString SanitizeNestedToggleGraphName(const FString& GraphName);
};
