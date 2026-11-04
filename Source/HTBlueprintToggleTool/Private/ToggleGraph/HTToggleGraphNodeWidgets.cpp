#include "HTToggleGraphNodeWidgets.h"

#include "Engine/SkeletalMesh.h"
#include "GraphEditorSettings.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphNodes.h"
#include "Materials/MaterialInterface.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HTToggleGraphNodeWidgets"

namespace HTToggleGraphNodeWidgets
{
	constexpr float FieldLabelWidth = 92.0f;
	constexpr float NodeMinWidth = 250.0f;
}

// ---------------------------------------------------------------- base

void SHTToggleGraphNode::Construct(const FArguments& InArgs, UHTToggleGraphNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

UHTToggleGraphNode* SHTToggleGraphNode::GetToggleNode() const
{
	return Cast<UHTToggleGraphNode>(GraphNode);
}

FSlateFontInfo SHTToggleGraphNode::FieldFont()
{
	return FCoreStyle::GetDefaultFontStyle("Regular", 9);
}

TSharedRef<SWidget> SHTToggleGraphNode::CreateNodeContentArea()
{
	using namespace HTToggleGraphNodeWidgets;
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0.0f, 3.0f))
		[
			SNew(SBox)
			.MinDesiredWidth(NodeMinWidth)
			[
				SNew(SVerticalBox)
				// Output pin on the right, directly below the title.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					[
						SAssignNew(RightNodeBox, SVerticalBox)
					]
				]
				// Editable parameters.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(8.0f, 2.0f, 8.0f, 4.0f))
				[
					CreateFieldsArea()
				]
				// Input pins on the left.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(LeftNodeBox, SVerticalBox)
				]
			]
		];
}

void SHTToggleGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	if (PinToAdd->GetDirection() != EGPD_Input)
	{
		SGraphNode::AddPin(PinToAdd);
		return;
	}
	PinToAdd->SetOwner(SharedThis(this));
	const UGraphEditorSettings* GraphSettings = GetDefault<UGraphEditorSettings>();
	LeftNodeBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(GraphSettings->GetInputPinPadding())
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				PinToAdd
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.0f, 0.0f, 8.0f, 0.0f))
			[
				CreateInputPinExtra(PinToAdd->GetPinObj())
			]
		];
	InputPins.Add(PinToAdd);
}

TSharedRef<SWidget> SHTToggleGraphNode::MakeTextField(const FText& Label, TAttribute<FText> Value, const FText& Hint, TFunction<void(const FString&)> OnCommit) const
{
	using namespace HTToggleGraphNodeWidgets;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(FieldLabelWidth)
			[
				SNew(STextBlock)
				.Font(FieldFont())
				.Text(Label)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.Font(FieldFont())
			.Text(Value)
			.HintText(Hint)
			.SelectAllTextWhenFocused(true)
			.OnTextCommitted_Lambda([OnCommit](const FText& Text, ETextCommit::Type)
			{
				OnCommit(Text.ToString());
			})
		];
}

TSharedRef<SWidget> SHTToggleGraphNode::MakeCounterField(const FText& Label, TAttribute<FText> Value, TFunction<void(int32)> OnStep) const
{
	auto MakeStepButton = [OnStep](const FText& Glyph, const FText& Tooltip, const int32 Delta)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(6.0f, 1.0f))
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnStep, Delta]()
			{
				OnStep(Delta);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Text(Glyph)
			];
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 2.0f))
		[
			SNew(STextBlock)
			.Font(FieldFont())
			.Text(Label)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.HAlign(HAlign_Center)
				.Padding(FMargin(4.0f, 2.0f))
				[
					SNew(STextBlock)
					.Font(FieldFont())
					.Text(Value)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0f, 0.0f, 0.0f, 0.0f))
			[
				MakeStepButton(LOCTEXT("Plus", "+"), LOCTEXT("PlusTooltip", "Increase"), 1)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeStepButton(LOCTEXT("Minus", "-"), LOCTEXT("MinusTooltip", "Decrease"), -1)
			]
		];
}

// ---------------------------------------------------------------- switch

void SHTToggleGraphNode_Switch::Construct(const FArguments& InArgs, UHTToggleGraphNode_Switch* InNode)
{
	SHTToggleGraphNode::Construct(SHTToggleGraphNode::FArguments(), InNode);
}

UHTToggleGraphNode_Switch* SHTToggleGraphNode_Switch::GetSwitch() const
{
	return Cast<UHTToggleGraphNode_Switch>(GraphNode);
}

FText SHTToggleGraphNode_Switch::GetInitialOptionText() const
{
	const UHTToggleGraphNode_Switch* Switch = GetSwitch();
	if (!Switch)
	{
		return FText::GetEmpty();
	}
	const FString OptionLabel = Switch->GetOptionLabel(Switch->InitialOption).TrimStartAndEnd();
	return OptionLabel.IsEmpty()
		? FText::Format(LOCTEXT("InitialOptionPlain", "Option {0}"), FText::AsNumber(Switch->InitialOption + 1))
		: FText::Format(LOCTEXT("InitialOptionLabeled", "Option {0} ({1})"), FText::AsNumber(Switch->InitialOption + 1), FText::FromString(OptionLabel));
}

TSharedRef<SWidget> SHTToggleGraphNode_Switch::CreateFieldsArea()
{
	TWeakObjectPtr<UHTToggleGraphNode_Switch> WeakNode = GetSwitch();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("CommentField", "Comment:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->Label) : FText::GetEmpty(); }),
				LOCTEXT("CommentHint", "Shown as the panel row name"),
				[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetLabelInline(Value); } })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("VariableField", "Variable:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->VariableName) : FText::GetEmpty(); }),
				LOCTEXT("VariableHint", "Auto from key"),
				[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetVariableNameInline(Value); } })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("KeyField", "Key:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->KeyName) : FText::GetEmpty(); }),
				LOCTEXT("KeyHint", "e.g. alt 6"),
				[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetKeyNameInline(Value); } })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeCounterField(
				LOCTEXT("OptionCountField", "Option count:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::AsNumber(WeakNode->OptionCount) : FText::GetEmpty(); }),
				[WeakNode](const int32 Delta) { if (WeakNode.IsValid()) { WeakNode->SetOptionCountInline(WeakNode->OptionCount + Delta); } })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeCounterField(
				LOCTEXT("InitialOptionField", "Initial option:"),
				TAttribute<FText>(this, &SHTToggleGraphNode_Switch::GetInitialOptionText),
				[WeakNode](const int32 Delta) { if (WeakNode.IsValid()) { WeakNode->SetInitialOptionInline(WeakNode->InitialOption + Delta); } })
		];
}

TSharedRef<SWidget> SHTToggleGraphNode_Switch::CreateInputPinExtra(UEdGraphPin* Pin)
{
	UHTToggleGraphNode_Switch* Switch = GetSwitch();
	const int32 Option = Switch ? Switch->GetOptionIndex(Pin) : INDEX_NONE;
	if (Option == INDEX_NONE)
	{
		return SNullWidget::NullWidget;
	}
	TWeakObjectPtr<UHTToggleGraphNode_Switch> WeakNode = Switch;
	return SNew(SBox)
		.MinDesiredWidth(110.0f)
		[
			SNew(SEditableTextBox)
			.Font(FieldFont())
			.Text_Lambda([WeakNode, Option]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->GetOptionLabel(Option)) : FText::GetEmpty(); })
			.HintText(LOCTEXT("OptionLabelHint", "label"))
			.ToolTipText(LOCTEXT("OptionLabelTooltip", "Option label shown by the in-game panel. Empty options show as Hidden."))
			.SelectAllTextWhenFocused(true)
			.OnTextCommitted_Lambda([WeakNode, Option](const FText& Text, ETextCommit::Type)
			{
				if (WeakNode.IsValid())
				{
					WeakNode->SetOptionLabelInline(Option, Text.ToString());
				}
			})
		];
}

// ---------------------------------------------------------------- part

void SHTToggleGraphNode_Part::Construct(const FArguments& InArgs, UHTToggleGraphNode_Part* InNode)
{
	SHTToggleGraphNode::Construct(SHTToggleGraphNode::FArguments(), InNode);
}

UHTToggleGraphNode_Part* SHTToggleGraphNode_Part::GetPart() const
{
	return Cast<UHTToggleGraphNode_Part>(GraphNode);
}

FText SHTToggleGraphNode_Part::GetSlotButtonText() const
{
	const UHTToggleGraphNode_Part* Part = GetPart();
	const int32 Count = Part ? Part->MaterialIDs.Num() : 0;
	return Count == 0
		? LOCTEXT("PickSlots", "Pick material slots...")
		: FText::Format(LOCTEXT("PickedSlots", "{0} slot(s) selected"), FText::AsNumber(Count));
}

TSharedRef<SWidget> SHTToggleGraphNode_Part::MakeSlotMenu() const
{
	UHTToggleGraphNode_Part* Part = GetPart();
	const UHTToggleGraph* Asset = Part ? Part->GetToggleGraphAsset() : nullptr;
	USkeletalMesh* Mesh = Asset ? Asset->ResolvePreviewMesh() : nullptr;
	if (!Mesh)
	{
		return SNew(SBox)
			.Padding(8.0f)
			.MaxDesiredWidth(320.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoPreviewMesh", "No preview mesh. Set the asset's Anim Blueprint (click empty graph space to show the asset details), or type the Material IDs."))
			];
	}
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	TWeakObjectPtr<UHTToggleGraphNode_Part> WeakNode = Part;
	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
	{
		const FSkeletalMaterial& Material = Materials[SlotIndex];
		const FName SlotName = !Material.MaterialSlotName.IsNone() ? Material.MaterialSlotName : Material.ImportedMaterialSlotName;
		const FString MaterialName = Material.MaterialInterface ? Material.MaterialInterface->GetName() : TEXT("None");
		Rows->AddSlot()
			.AutoHeight()
			.Padding(FMargin(6.0f, 1.0f))
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([WeakNode, SlotIndex]()
				{
					return WeakNode.IsValid() && WeakNode->MaterialIDs.Contains(SlotIndex) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakNode, SlotIndex](const ECheckBoxState State)
				{
					if (WeakNode.IsValid())
					{
						WeakNode->SetMaterialIDSelectedInline(SlotIndex, State == ECheckBoxState::Checked);
					}
				})
				[
					SNew(STextBlock)
					.Font(FieldFont())
					.Text(FText::FromString(FString::Printf(TEXT("%d  %s  (%s)"), SlotIndex, *SlotName.ToString(), *MaterialName)))
				]
			];
	}
	return SNew(SBox)
		.MaxDesiredHeight(420.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Rows
			]
		];
}

TSharedRef<SWidget> SHTToggleGraphNode_Part::CreateFieldsArea()
{
	TWeakObjectPtr<UHTToggleGraphNode_Part> WeakNode = GetPart();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("PartCommentField", "Comment:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->Label) : FText::GetEmpty(); }),
				FText::GetEmpty(),
				[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetLabelInline(Value); } })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("MaterialIDsField", "Material IDs:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->GetMaterialIDsText()) : FText::GetEmpty(); }),
				LOCTEXT("MaterialIDsHint", "e.g. 1, 2, 12"),
				[WeakNode](const FString& Value)
				{
					TArray<int32> IDs;
					if (WeakNode.IsValid() && UHTToggleGraphNode_Part::ParseMaterialIDs(Value, IDs))
					{
						WeakNode->SetMaterialIDsInline(IDs);
					}
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 3.0f, 0.0f, 1.0f))
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(4.0f, 1.0f))
			.ToolTipText(LOCTEXT("SlotsTooltip", "Tick the preview mesh material slots this part shows and hides."))
			.OnGetMenuContent(this, &SHTToggleGraphNode_Part::MakeSlotMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FieldFont())
				.Text(this, &SHTToggleGraphNode_Part::GetSlotButtonText)
			]
		];
}

// ---------------------------------------------------------------- group

void SHTToggleGraphNode_Group::Construct(const FArguments& InArgs, UHTToggleGraphNode_Group* InNode)
{
	SHTToggleGraphNode::Construct(SHTToggleGraphNode::FArguments(), InNode);
}

UHTToggleGraphNode_Group* SHTToggleGraphNode_Group::GetGroup() const
{
	return Cast<UHTToggleGraphNode_Group>(GraphNode);
}

TSharedRef<SWidget> SHTToggleGraphNode_Group::CreateFieldsArea()
{
	TWeakObjectPtr<UHTToggleGraphNode_Group> WeakNode = GetGroup();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("GroupCommentField", "Comment:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->Label) : FText::GetEmpty(); }),
				FText::GetEmpty(),
				[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetLabelInline(Value); } })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeCounterField(
				LOCTEXT("InputCountField", "Inputs:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::AsNumber(WeakNode->InputCount) : FText::GetEmpty(); }),
				[WeakNode](const int32 Delta) { if (WeakNode.IsValid()) { WeakNode->SetInputCountInline(WeakNode->InputCount + Delta); } })
		];
}

// ---------------------------------------------------------------- factory

TSharedPtr<SGraphNode> FHTToggleGraphNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UHTToggleGraphNode_Switch* Switch = Cast<UHTToggleGraphNode_Switch>(Node))
	{
		return SNew(SHTToggleGraphNode_Switch, Switch);
	}
	if (UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(Node))
	{
		return SNew(SHTToggleGraphNode_Part, Part);
	}
	if (UHTToggleGraphNode_Group* Group = Cast<UHTToggleGraphNode_Group>(Node))
	{
		return SNew(SHTToggleGraphNode_Group, Group);
	}
	if (UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node))
	{
		return SNew(SHTToggleGraphNode, ToggleNode);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
