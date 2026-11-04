#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "HTToggleGraphNodes.generated.h"

class UHTToggleGraph;

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

	//~ UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual bool SupportsCommentBubble() const override { return false; }
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void ReconstructNode() override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void NodeConnectionListChanged() override;
	virtual void PostLoad() override;

protected:
	virtual FString* GetMutableLabel() { return nullptr; }
	void AllocateDynamicInputPins();

	// Number of dynamic input pins (Group, Generate). Managed automatically, not user-editable.
	UPROPERTY()
	int32 InputCount = 1;

private:
	bool bNormalizingInputs = false;
};

// Part: material IDs of the preview mesh. One output that hangs on a switch option,
// a group or the generate node.
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

	bool SetMaterialIDsInline(const TArray<int32>& NewMaterialIDs);
	bool SetMaterialIDSelectedInline(int32 MaterialID, bool bSelected);
	FString GetMaterialIDsText() const;
	// Accepts "1, 2 12" style lists; returns false (and leaves OutIDs empty) on any invalid token.
	static bool ParseMaterialIDs(const FString& Text, TArray<int32>& OutIDs);

	virtual FString GetLabel() const override { return Label; }
	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;

protected:
	virtual FString* GetMutableLabel() override { return &Label; }
};

// Switch: one input pin per option, one output. Switches that share a key share a state variable.
UCLASS()
class UHTToggleGraphNode_Switch : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	static constexpr int32 MinOptions = 2;
	static constexpr int32 MaxOptions = 64;

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

	UEdGraphPin* GetOptionPin(int32 Option) const;
	// Option index of one of this node's option pins, INDEX_NONE for any other pin.
	int32 GetOptionIndex(const UEdGraphPin* Pin) const;
	FString GetOptionLabel(int32 Option) const;

	bool SetVariableNameInline(const FString& NewVariableName);
	bool SetKeyNameInline(const FString& NewKeyName);
	// Adds or removes option pins at the end; links on the remaining pins are kept.
	bool SetOptionCountInline(int32 NewOptionCount);
	bool SetInitialOptionInline(int32 NewInitialOption);
	bool SetOptionLabelInline(int32 Option, const FString& NewLabel);

	virtual FString GetLabel() const override { return Label; }
	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;

protected:
	virtual FString* GetMutableLabel() override { return &Label; }
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
