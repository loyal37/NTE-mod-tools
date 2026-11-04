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

	//~ UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual bool SupportsCommentBubble() const override { return false; }
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void ReconstructNode() override;

protected:
	virtual FString* GetMutableLabel() { return nullptr; }
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
	static constexpr int32 MinInputs = 1;
	static constexpr int32 MaxInputs = 32;
	static FName GroupInputPinName(int32 Index);

	UPROPERTY(EditAnywhere, Category = "Group", meta = (DisplayName = "Comment"))
	FString Label;

	UPROPERTY(EditAnywhere, Category = "Group", meta = (DisplayName = "Input Count", ClampMin = 1, ClampMax = 32))
	int32 InputCount = 2;

	UEdGraphPin* GetGroupInputPin(int32 Index) const;
	// Adds or removes input pins at the end; links on the remaining pins are kept.
	bool SetInputCountInline(int32 NewInputCount);

	virtual FString GetLabel() const override { return Label; }
	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PostLoad() override;

protected:
	virtual FString* GetMutableLabel() override { return &Label; }
};

// Generate: root of the tree. Everything connected to it is compiled; the rest is ignored.
UCLASS()
class UHTToggleGraphNode_Generate : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanDuplicateNode() const override { return false; }
};
