#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphNode.h"

class UHTToggleGraphNode;
class UHTToggleGraphNode_Group;
class UHTToggleGraphNode_Part;
class UHTToggleGraphNode_Switch;

// Shared layout of every toggle graph node (LoyalTools style):
//   title bar
//   output pin on the right
//   editable fields
//   input pins on the left, each with an optional inline editor beside it
class SHTToggleGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SHTToggleGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHTToggleGraphNode* InNode);

	//~ SGraphNode
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

	// Inspection hooks used by automation.
	int32 GetInputPinWidgetCount() const { return InputPins.Num(); }
	int32 GetOutputPinWidgetCount() const { return OutputPins.Num(); }

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() { return SNullWidget::NullWidget; }
	virtual TSharedRef<SWidget> CreateInputPinExtra(UEdGraphPin* Pin) { return SNullWidget::NullWidget; }

	UHTToggleGraphNode* GetToggleNode() const;
	// "Label: [text box]" row. Commits only call OnCommit when the text changed.
	TSharedRef<SWidget> MakeTextField(const FText& Label, TAttribute<FText> Value, const FText& Hint, TFunction<void(const FString&)> OnCommit) const;
	// "Label:" then "[ value ][+][-]".
	TSharedRef<SWidget> MakeCounterField(const FText& Label, TAttribute<FText> Value, TFunction<void(int32)> OnStep) const;
	static FSlateFontInfo FieldFont();
};

class SHTToggleGraphNode_Switch : public SHTToggleGraphNode
{
public:
	SLATE_BEGIN_ARGS(SHTToggleGraphNode_Switch) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHTToggleGraphNode_Switch* InNode);

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() override;
	virtual TSharedRef<SWidget> CreateInputPinExtra(UEdGraphPin* Pin) override;

private:
	UHTToggleGraphNode_Switch* GetSwitch() const;
	FText GetInitialOptionText() const;
};

class SHTToggleGraphNode_Part : public SHTToggleGraphNode
{
public:
	SLATE_BEGIN_ARGS(SHTToggleGraphNode_Part) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHTToggleGraphNode_Part* InNode);

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() override;

private:
	UHTToggleGraphNode_Part* GetPart() const;
	TSharedRef<SWidget> MakeSlotMenu() const;
	FText GetSlotButtonText() const;
};

class SHTToggleGraphNode_Group : public SHTToggleGraphNode
{
public:
	SLATE_BEGIN_ARGS(SHTToggleGraphNode_Group) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHTToggleGraphNode_Group* InNode);

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() override;

private:
	UHTToggleGraphNode_Group* GetGroup() const;
};

// Creates the widgets above for toggle graph nodes; registered by the module.
struct FHTToggleGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};
