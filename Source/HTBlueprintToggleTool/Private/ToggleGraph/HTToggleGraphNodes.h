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

	//~ UEdGraphNode
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void ReconstructNode() override;
};

// 部件: material IDs of the preview mesh. One output that hangs on a switch option,
// a group or the generate node.
UCLASS()
class UHTToggleGraphNode_Part : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Part", meta = (DisplayName = "备注"))
	FString Label;

	// Material IDs (skeletal mesh material slot indices). Pick them from the slot list below.
	UPROPERTY(EditAnywhere, Category = "Part", meta = (DisplayName = "材质 ID"))
	TArray<int32> MaterialIDs;

	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
};

// 切换: one input pin per option, one output. Switches that share a key share a state variable.
UCLASS()
class UHTToggleGraphNode_Switch : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "备注"))
	FString Label;

	// Optional state variable name. Empty = derived from the key as <TreeName>_<Key>; switches with the
	// same key share one variable automatically, and typing the same name here also shares it.
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "变量名"))
	FString VariableName;

	// Examples: 9, =, ctrl 6, shift 6, alt 6
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "按键"))
	FString KeyName;

	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "选项数量", ClampMin = 2, ClampMax = 64))
	int32 OptionCount = 2;

	// Option used before any SaveGame exists (0-based).
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "初始选项", ClampMin = 0))
	int32 InitialOption = 0;

	// Optional labels shown by the in-game panel, one per option.
	UPROPERTY(EditAnywhere, Category = "Switch", meta = (DisplayName = "选项标签"))
	TArray<FString> OptionLabels;

	UEdGraphPin* GetOptionPin(int32 Option) const;
	// Option index of one of this node's option pins, INDEX_NONE for any other pin.
	int32 GetOptionIndex(const UEdGraphPin* Pin) const;

	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
};

// 群组: merges several parts or switches into one connection.
UCLASS()
class UHTToggleGraphNode_Group : public UHTToggleGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Group", meta = (DisplayName = "备注"))
	FString Label;

	virtual FString GetDisplayLabel() const override;
	virtual void AllocateDefaultPins() override;
	virtual FLinearColor GetNodeTitleColor() const override;
};

// 生成: root of the tree. Everything connected to it is compiled; the rest is ignored.
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
