#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/SoftObjectPtr.h"
#include "HTToggleGraphNodes.generated.h"

class UHTToggleGraph;
class UHTToggleGraphNode_Part;
class UMaterialInterface;
class USkeletalMesh;
class UTexture;

// Base class of every toggle graph node. All nodes carry one pin category so the schema
// can tell them apart from foreign pins; a single output means one path per part.
UCLASS(Abstract)
class UHTToggleGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	static const FName PinCategory;
	static const FName OutputPinName;
	static const FName InputPinName;
	static FName OptionPinName(int32 Option);

	// Readable, stable identifier used in generator errors: "<title>#<guid prefix>".
	FString GetNodeId() const;
	UEdGraphPin* GetOutputPin() const;
	UEdGraphPin* GetInputPin() const;
	UHTToggleGraph* GetToggleGraphAsset() const;
	// Short text used as the node title and in error messages.
	virtual FString GetDisplayLabel() const { return FString(); }

	// Inline (in-node) editing shared by every node: each call is one undoable transaction
	// that marks the asset dirty; calls that change pins rebuild them and keep remaining links.
	bool SetLabelInline(const FString& NewLabel);
	virtual FString GetLabel() const { return FString(); }

	// Growing input list (Group, Generate): the node always ends with exactly one empty input.
	// Connecting the last input adds a new empty one; disconnecting trims surplus empty inputs at
	// the end. Empty inputs in the middle stay so the order of the other links never changes.
	static constexpr int32 MaxDynamicInputs = 32;
	static FName DynamicInputPinName(int32 Index);
	virtual bool HasDynamicInputs() const { return false; }
	UEdGraphPin* GetDynamicInputPin(int32 Index) const;
	int32 GetDynamicInputCount() const { return InputCount; }
	// bRecord = true: Modify() (joins the caller's transaction) and refresh the node widget.
	// bRecord = false: silent fix-up while loading or opening the editor; does not dirty the asset.
	bool NormalizeDynamicInputs(bool bRecord = true);

	// Removes links that cannot be valid: null entries, trashed pins and pins of nodes that are not
	// in this node's graph. Returns how many were removed.
	int32 RemoveDeadLinks();
	// True for properties whose edit changes the pins or the title (rebuilds the node).
	static bool DoesPropertyRebuildNode(FName MemberName);

	// Collapsed nodes show only the title, a short summary and their pins (LoyalTools style).
	virtual bool CanCollapse() const { return false; }
	bool IsCollapsed() const { return bCollapsed && CanCollapse(); }
	// One undoable step; the node widget is rebuilt and links keep their pins.
	bool SetCollapsedInline(bool bNewCollapsed);
	// One undoable step for every collapsible node of Graph. Returns how many nodes changed.
	static int32 SetAllCollapsedInline(UEdGraph* Graph, bool bNewCollapsed);
	// Line under the title of a collapsed node, for example "Key alt 6 · 2 options".
	virtual FString GetCollapsedSummary() const { return FString(); }

	// The part connected to Pin (Texture Switch Target and Material Switch Slots inputs), if any.
	static UHTToggleGraphNode_Part* GetLinkedPart(const UEdGraphPin* Pin);

	// Current English display name of one of this node's pins, derived from the pin name.
	static FText MakePinDisplayName(const UEdGraphPin* Pin);
	// Rewrites every pin's display name (graphs saved by v1.5.30 carry Chinese names). Links,
	// pin names and ids are untouched and the asset is not dirtied. Returns how many changed.
	int32 RefreshPinDisplayNames();

	//~ UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	// Pin labels always come from the pin name, so stale stored names never show.
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override { return MakePinDisplayName(Pin); }
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual bool SupportsCommentBubble() const override { return false; }
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void ReconstructNode() override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void NodeConnectionListChanged() override;
	virtual void PostLoad() override;
	// Collapse / Expand, Collapse all / Expand all.
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

protected:
	virtual FString* GetMutableLabel() { return nullptr; }
	void AllocateDynamicInputPins();
	// The member property of a real property edit worth reacting to, or nullptr (no property,
	// text import, undo, loading, or the node is not in its graph).
	const FProperty* GetEditedMemberProperty(const FPropertyChangedEvent& PropertyChangedEvent) const;

	// Number of dynamic input pins (Group, Generate). Managed automatically, not user-editable.
	UPROPERTY()
	int32 InputCount = 1;

	// Saved per node; copied by copy and paste.
	UPROPERTY()
	bool bCollapsed = false;

private:
	bool bNormalizingInputs = false;
};

// Part: material IDs of the preview mesh. Its output can fan out: one visibility link (a switch
// option, a group or the generate node) plus any number of Texture Switch Target and Material
// Switch Slots inputs.
UCLASS()
class UHTToggleGraphNode_Part : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Part", meta = (DisplayName = "Comment"))
	FString Label;

	// Material IDs (skeletal mesh material slot indices). Pick them from the slot list on the node.
	UPROPERTY(EditAnywhere, Category = "Part", meta = (DisplayName = "Material IDs"))
	TArray<int32> MaterialIDs;

	// Every preview mesh slot that uses this material. The slots are looked up again on compile, so
	// the part follows the mesh; picking slots by hand clears it. A Texture Switch target uses it as
	// the material its textures belong to.
	UPROPERTY(EditAnywhere, Category = "Part", meta = (DisplayName = "By Material"))
	TSoftObjectPtr<UMaterialInterface> SourceMaterial;

	// The preview mesh's material slot name of each Material ID (same order; None where unknown),
	// recorded whenever the IDs are picked. Slot names survive slots being inserted or removed,
	// so Refresh can move every ID back onto its slot; Compile refuses IDs that moved.
	UPROPERTY(VisibleAnywhere, Category = "Part", meta = (DisplayName = "Slot Names"))
	TArray<FName> SlotNames;

	// Records the slot name of every Material ID from the preview mesh (None without a mesh).
	void BindSlotNames();
	// "sleeve, body" for the node; unknown names show as "?".
	FString GetSlotNamesText() const;
	bool HasSlotNames() const;

	bool SetMaterialIDsInline(const TArray<int32>& NewMaterialIDs);
	bool SetMaterialIDSelectedInline(int32 MaterialID, bool bSelected);
	// Picks every preview mesh slot that uses Material and remembers it; nullptr = manual mode.
	bool SetSourceMaterialInline(UMaterialInterface* Material);
	// Material IDs as compiled: looked up on the preview mesh in By material mode, else MaterialIDs.
	// bOutMeshMissing is set when By material is used but there is no preview mesh to look at.
	TArray<int32> GetEffectiveMaterialIDs(bool* bOutMeshMissing = nullptr) const;
	static TArray<int32> FindSlotsUsingMaterial(const USkeletalMesh* Mesh, const UMaterialInterface* Material);
	FString GetMaterialIDsText() const;
	// Accepts "1, 2 12" style lists; returns false (and leaves OutIDs empty) on any invalid token.
	static bool ParseMaterialIDs(const FString& Text, TArray<int32>& OutIDs);

	virtual FString GetLabel() const override { return Label; }
	virtual FString GetDisplayLabel() const override;
	virtual bool CanCollapse() const override { return true; }
	virtual FString GetCollapsedSummary() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

protected:
	virtual FString* GetMutableLabel() override { return &Label; }
};

// Header shared by every key-driven switch: comment, variable, key, option count, initial option
// and option labels. Switches that share a key or a variable name share one state variable, also
// across the visibility, texture and material kinds.
UCLASS(Abstract)
class UHTToggleGraphNode_SwitchBase : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	static constexpr int32 MinOptions = 2;
	static constexpr int32 MaxOptions = 64;

	// Note for this graph only; it is not shown in game. The panel row is named after the variable.
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "Comment"))
	FString Label;

	// Optional state variable name. Empty = derived from the key as <TreeName>_<Key>; switches with the
	// same key share one variable automatically, and typing the same name here also shares it.
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "Variable"))
	FString VariableName;

	// Examples: 9, =, ctrl 6, shift 6, alt 6
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "Key"))
	FString KeyName;

	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "Option Count", ClampMin = 2, ClampMax = 64))
	int32 OptionCount = 2;

	// Option used before any SaveGame exists (0-based).
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "Initial Option", ClampMin = 0))
	int32 InitialOption = 0;

	// Optional labels shown by the in-game panel, one per option.
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "Option Labels"))
	TArray<FString> OptionLabels;

	FString GetOptionLabel(int32 Option) const;

	bool SetVariableNameInline(const FString& NewVariableName);
	bool SetKeyNameInline(const FString& NewKeyName);
	// Adds or removes options at the end; links and per-option data of the remaining ones are kept.
	bool SetOptionCountInline(int32 NewOptionCount);
	bool SetInitialOptionInline(int32 NewInitialOption);
	bool SetOptionLabelInline(int32 Option, const FString& NewLabel);

	// Texture and Material Switches take parts on their dynamic inputs.
	virtual bool AcceptsTargetParts() const { return false; }
	// The connected part that was made from Material, if any.
	UHTToggleGraphNode_Part* FindTargetPartWithMaterial(const UMaterialInterface* Material) const;
	// "Add part from material": one undoable step that places a part holding every preview mesh
	// slot that uses Material (remembered, so compile looks the slots up again) and connects it
	// to the first free input. Returns the part already connected for Material instead of adding
	// a second one, and nullptr when the preview mesh does not use Material.
	UHTToggleGraphNode_Part* AddPartFromMaterial(UMaterialInterface* Material);

	virtual FString GetLabel() const override { return Label; }
	virtual FString GetDisplayLabel() const override;
	virtual bool CanCollapse() const override { return true; }
	virtual FString GetCollapsedSummary() const override;

protected:
	virtual FString* GetMutableLabel() override { return &Label; }
	// First word of the title: "Switch", "Texture Switch" or "Material Switch".
	virtual FString GetKindTitle() const { return TEXT("Switch"); }
	// Runs inside SetOptionCountInline's transaction after OptionCount changed.
	virtual void OnOptionCountChanged() {}
};

// Switch: one input pin per option, one output. Parts and switches hang on its options.
UCLASS()
class UHTToggleGraphNode_Switch : public UHTToggleGraphNode_SwitchBase
{
	GENERATED_BODY()

public:
	UEdGraphPin* GetOptionPin(int32 Option) const;
	// Option index of one of this node's option pins, INDEX_NONE for any other pin.
	int32 GetOptionIndex(const UEdGraphPin* Pin) const;

	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
};

USTRUCT()
struct FHTToggleTextureTarget
{
	GENERATED_BODY()

	// One texture per option. Empty = the option keeps the material's own texture.
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "Option Textures"))
	TArray<TSoftObjectPtr<UTexture>> OptionTextures;
};

// Texture Switch: sets one texture parameter on the slots of its target parts, one texture per
// option. A leaf: its output hangs on a switch option, a group or the generate node, and only
// parts connect to its Target inputs (one part per input; the inputs grow like a group's).
UCLASS()
class UHTToggleGraphNode_TextureSwitch : public UHTToggleGraphNode_SwitchBase
{
	GENERATED_BODY()

public:
	// Texture parameter set on every target, for example BaseColor.
	UPROPERTY(EditAnywhere, Category = "Texture Switch", meta = (DisplayName = "Parameter"))
	FString ParameterName;

	// One entry per Target input, in input order.
	UPROPERTY(EditAnywhere, Category = "Texture Switch", meta = (DisplayName = "Targets"))
	TArray<FHTToggleTextureTarget> Targets;

	TSoftObjectPtr<UTexture> GetTargetTexture(int32 Target, int32 Option) const;
	UHTToggleGraphNode_Part* GetTargetPart(int32 Target) const { return GetLinkedPart(GetDynamicInputPin(Target)); }
	bool SetParameterNameInline(const FString& NewParameterName);
	bool SetTargetTextureInline(int32 Target, int32 Option, UTexture* Texture);
	// Texture parameters that the material of every connected target has (for the Parameter list).
	TArray<FName> GetCandidateParameters() const;
	// Materials a target's textures apply to: its part's By material choice, else the preview
	// mesh materials of its slots.
	TArray<UMaterialInterface*> GetTargetMaterials(int32 Target) const;

	virtual bool HasDynamicInputs() const override { return true; }
	virtual bool AcceptsTargetParts() const override { return true; }
	virtual FString GetCollapsedSummary() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;

protected:
	virtual FString GetKindTitle() const override { return TEXT("Texture Switch"); }
	virtual void OnOptionCountChanged() override;
};

// Material Switch: puts one material per option on the slots of its parts (their union). Empty
// options restore each slot's original material. A leaf like the Texture Switch.
UCLASS()
class UHTToggleGraphNode_MaterialSwitch : public UHTToggleGraphNode_SwitchBase
{
	GENERATED_BODY()

public:
	// One material per option. Empty = the option restores each slot's original material.
	UPROPERTY(EditAnywhere, Category = "Material Switch", meta = (DisplayName = "Option Materials"))
	TArray<TSoftObjectPtr<UMaterialInterface>> OptionMaterials;

	TSoftObjectPtr<UMaterialInterface> GetOptionMaterial(int32 Option) const;
	UHTToggleGraphNode_Part* GetSlotsPart(int32 Input) const { return GetLinkedPart(GetDynamicInputPin(Input)); }
	bool SetOptionMaterialInline(int32 Option, UMaterialInterface* Material);

	virtual bool HasDynamicInputs() const override { return true; }
	virtual bool AcceptsTargetParts() const override { return true; }
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;

protected:
	virtual FString GetKindTitle() const override { return TEXT("Material Switch"); }
	virtual void OnOptionCountChanged() override;
};

// Group: merges several parts or switches into one connection.
UCLASS()
class UHTToggleGraphNode_Group : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	static FName GroupInputPinName(int32 Index) { return DynamicInputPinName(Index); }

	UPROPERTY(EditAnywhere, Category = "Group", meta = (DisplayName = "Comment"))
	FString Label;

	UEdGraphPin* GetGroupInputPin(int32 Index) const { return GetDynamicInputPin(Index); }

	virtual bool HasDynamicInputs() const override { return true; }
	virtual FString GetLabel() const override { return Label; }
	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;

protected:
	virtual FString* GetMutableLabel() override { return &Label; }
};

// Generate: root of the tree. Everything connected to it is compiled; the rest is ignored.
UCLASS()
class UHTToggleGraphNode_Generate : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	virtual bool HasDynamicInputs() const override { return true; }
	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanDuplicateNode() const override { return false; }
};
