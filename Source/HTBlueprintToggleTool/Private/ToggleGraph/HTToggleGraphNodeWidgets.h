#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphNode.h"

class UHTToggleGraphNode;
class UHTToggleGraphNode_Group;
class UHTToggleGraphNode_MaterialSwitch;
class UHTToggleGraphNode_Part;
class UHTToggleGraphNode_Switch;
class UHTToggleGraphNode_SwitchBase;
class UHTToggleGraphNode_TextureSwitch;

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
	// Title plus, on a collapsed node, its one-line summary.
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	// Collapse / expand arrow on nodes with fields.
	virtual TSharedRef<SWidget> CreateTitleRightWidget() override;

	// Inspection hooks used by automation.
	int32 GetInputPinWidgetCount() const { return InputPins.Num(); }
	int32 GetOutputPinWidgetCount() const { return OutputPins.Num(); }
	bool IsShowingFields() const { return bShowingFields; }
	// "Add part from material": the preview mesh materials with thumbnails and their slots
	// (Texture and Material Switches; public for automation).
	TSharedRef<SWidget> MakeMaterialPartMenu() const;

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() { return SNullWidget::NullWidget; }
	virtual TSharedRef<SWidget> CreateInputPinExtra(UEdGraphPin* Pin) { return SNullWidget::NullWidget; }
	// What stays beside an input pin while the node is collapsed (a switch shows its option label).
	virtual TSharedRef<SWidget> CreateCollapsedPinExtra(UEdGraphPin* Pin) { return SNullWidget::NullWidget; }
	bool IsNodeCollapsed() const;

	UHTToggleGraphNode* GetToggleNode() const;
	// "Label: [text box]" row. Commits only call OnCommit when the text changed.
	TSharedRef<SWidget> MakeTextField(const FText& Label, TAttribute<FText> Value, const FText& Hint, TFunction<void(const FString&)> OnCommit, const FText& Tooltip = FText::GetEmpty()) const;
	// "Label:" then "[ value ][+][-]".
	TSharedRef<SWidget> MakeCounterField(const FText& Label, TAttribute<FText> Value, TFunction<void(int32)> OnStep) const;
	// Comment, Variable, Key, Option count and Initial option of every switch kind.
	TSharedRef<SWidget> MakeSwitchHeader(UHTToggleGraphNode_SwitchBase* Node) const;
	// Small text box for one option's panel label.
	TSharedRef<SWidget> MakeOptionLabelBox(UHTToggleGraphNode_SwitchBase* Node, int32 Option) const;
	// Asset picker (texture or material) with a thumbnail, committing through OnPick; nullptr clears.
	TSharedRef<SWidget> MakeAssetPicker(UClass* AllowedClass, TFunction<FString()> GetPath, TFunction<void(UObject*)> OnPick, const FText& Tooltip) const;
	// Button that opens MakeMaterialPartMenu.
	TSharedRef<SWidget> MakeMaterialPartButton() const;
	// Index of Pin among the node's dynamic inputs, INDEX_NONE otherwise.
	int32 GetDynamicInputIndex(const UEdGraphPin* Pin) const;
	// "Part ...: slots 3, 4" for a Target / Slots input.
	static FText MakeConnectedPartText(const UHTToggleGraphNode_Part* Part);
	static FText MakeInitialOptionText(const UHTToggleGraphNode_SwitchBase* Node);
	static FSlateFontInfo FieldFont();

private:
	bool bShowingFields = false;
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
	virtual TSharedRef<SWidget> CreateCollapsedPinExtra(UEdGraphPin* Pin) override;

private:
	UHTToggleGraphNode_Switch* GetSwitch() const;
};

class SHTToggleGraphNode_TextureSwitch : public SHTToggleGraphNode
{
public:
	SLATE_BEGIN_ARGS(SHTToggleGraphNode_TextureSwitch) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHTToggleGraphNode_TextureSwitch* InNode);

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() override;
	virtual TSharedRef<SWidget> CreateInputPinExtra(UEdGraphPin* Pin) override;

private:
	UHTToggleGraphNode_TextureSwitch* GetTextureSwitch() const;
	TSharedRef<SWidget> MakeParameterMenu() const;
};

class SHTToggleGraphNode_MaterialSwitch : public SHTToggleGraphNode
{
public:
	SLATE_BEGIN_ARGS(SHTToggleGraphNode_MaterialSwitch) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHTToggleGraphNode_MaterialSwitch* InNode);

protected:
	virtual TSharedRef<SWidget> CreateFieldsArea() override;
	virtual TSharedRef<SWidget> CreateInputPinExtra(UEdGraphPin* Pin) override;

private:
	UHTToggleGraphNode_MaterialSwitch* GetMaterialSwitch() const;
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
