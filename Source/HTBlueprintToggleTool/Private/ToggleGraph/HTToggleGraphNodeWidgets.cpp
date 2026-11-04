#include "HTToggleGraphNodeWidgets.h"

#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditorSettings.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphNodes.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "PropertyCustomizationHelpers.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SSeparator.h"
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

TSharedRef<SWidget> SHTToggleGraphNode::MakeTextField(const FText& Label, TAttribute<FText> Value, const FText& Hint, TFunction<void(const FString&)> OnCommit, const FText& Tooltip) const
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
			.ToolTipText(Tooltip)
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

FText SHTToggleGraphNode::MakeInitialOptionText(const UHTToggleGraphNode_SwitchBase* Node)
{
	if (!Node)
	{
		return FText::GetEmpty();
	}
	const FString OptionLabel = Node->GetOptionLabel(Node->InitialOption).TrimStartAndEnd();
	return OptionLabel.IsEmpty()
		? FText::Format(LOCTEXT("InitialOptionPlain", "Option {0}"), FText::AsNumber(Node->InitialOption + 1))
		: FText::Format(LOCTEXT("InitialOptionLabeled", "Option {0} ({1})"), FText::AsNumber(Node->InitialOption + 1), FText::FromString(OptionLabel));
}

TSharedRef<SWidget> SHTToggleGraphNode::MakeOptionLabelBox(UHTToggleGraphNode_SwitchBase* Node, const int32 Option) const
{
	TWeakObjectPtr<UHTToggleGraphNode_SwitchBase> WeakNode = Node;
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

TSharedRef<SWidget> SHTToggleGraphNode::MakeAssetPicker(UClass* AllowedClass, TFunction<FString()> GetPath, TFunction<void(UObject*)> OnPick, const FText& Tooltip) const
{
	return SNew(SBox)
		.MinDesiredWidth(170.0f)
		.ToolTipText(Tooltip)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(AllowedClass)
			.ObjectPath_Lambda([GetPath]() { return GetPath(); })
			.OnObjectChanged_Lambda([OnPick](const FAssetData& Asset) { OnPick(Asset.IsValid() ? Asset.GetAsset() : nullptr); })
			.AllowClear(true)
			.DisplayUseSelected(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(false)
		];
}

int32 SHTToggleGraphNode::GetDynamicInputIndex(const UEdGraphPin* Pin) const
{
	const UHTToggleGraphNode* Node = GetToggleNode();
	for (int32 Index = 0; Node && Pin && Index < Node->GetDynamicInputCount(); ++Index)
	{
		if (Node->GetDynamicInputPin(Index) == Pin)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FText SHTToggleGraphNode::MakeConnectedPartText(const UHTToggleGraphNode_Part* Part)
{
	if (!Part)
	{
		return LOCTEXT("ConnectPart", "Connect a part");
	}
	const FString Slots = Part->GetMaterialIDsText();
	const FString Material = Part->SourceMaterial.IsNull() ? FString() : FString::Printf(TEXT(" (%s)"), *Part->SourceMaterial.GetAssetName());
	return Slots.IsEmpty()
		? FText::Format(LOCTEXT("PartNoSlots", "No slots{0}"), FText::FromString(Material))
		: FText::Format(LOCTEXT("PartSlots", "Slots {0}{1}"), FText::FromString(Slots), FText::FromString(Material));
}

TSharedRef<SWidget> SHTToggleGraphNode::MakeSwitchHeader(UHTToggleGraphNode_SwitchBase* Node) const
{
	TWeakObjectPtr<UHTToggleGraphNode_SwitchBase> WeakNode = Node;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeTextField(
				LOCTEXT("CommentField", "Comment:"),
				TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->Label) : FText::GetEmpty(); }),
				LOCTEXT("SwitchCommentHint", "Note (not shown in game)"),
				[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetLabelInline(Value); } },
				LOCTEXT("SwitchCommentTooltip", "Note for this graph only. It is not shown in game: the panel row is named after the variable."))
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
				TAttribute<FText>::CreateLambda([WeakNode]() { return MakeInitialOptionText(WeakNode.Get()); }),
				[WeakNode](const int32 Delta) { if (WeakNode.IsValid()) { WeakNode->SetInitialOptionInline(WeakNode->InitialOption + Delta); } })
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

TSharedRef<SWidget> SHTToggleGraphNode_Switch::CreateFieldsArea()
{
	return MakeSwitchHeader(GetSwitch());
}

TSharedRef<SWidget> SHTToggleGraphNode_Switch::CreateInputPinExtra(UEdGraphPin* Pin)
{
	UHTToggleGraphNode_Switch* Switch = GetSwitch();
	const int32 Option = Switch ? Switch->GetOptionIndex(Pin) : INDEX_NONE;
	if (Option == INDEX_NONE)
	{
		return SNullWidget::NullWidget;
	}
	return MakeOptionLabelBox(Switch, Option);
}

// ---------------------------------------------------------------- texture switch

void SHTToggleGraphNode_TextureSwitch::Construct(const FArguments& InArgs, UHTToggleGraphNode_TextureSwitch* InNode)
{
	SHTToggleGraphNode::Construct(SHTToggleGraphNode::FArguments(), InNode);
}

UHTToggleGraphNode_TextureSwitch* SHTToggleGraphNode_TextureSwitch::GetTextureSwitch() const
{
	return Cast<UHTToggleGraphNode_TextureSwitch>(GraphNode);
}

TSharedRef<SWidget> SHTToggleGraphNode_TextureSwitch::MakeParameterMenu() const
{
	UHTToggleGraphNode_TextureSwitch* Node = GetTextureSwitch();
	const TArray<FName> Candidates = Node ? Node->GetCandidateParameters() : TArray<FName>();
	if (Candidates.IsEmpty())
	{
		return SNew(SBox)
			.Padding(8.0f)
			.MaxDesiredWidth(320.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoParameters", "Connect parts to the Target inputs to list the texture parameters their materials share, or type a parameter name."))
			];
	}
	TWeakObjectPtr<UHTToggleGraphNode_TextureSwitch> WeakNode = Node;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FName Candidate : Candidates)
	{
		Rows->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked_Lambda([WeakNode, Candidate]()
				{
					if (WeakNode.IsValid())
					{
						WeakNode->SetParameterNameInline(Candidate.ToString());
					}
					FSlateApplication::Get().DismissAllMenus();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FieldFont())
					.Text(FText::FromName(Candidate))
				]
			];
	}
	return SNew(SBox)
		.MaxDesiredHeight(360.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Rows
			]
		];
}

TSharedRef<SWidget> SHTToggleGraphNode_TextureSwitch::CreateFieldsArea()
{
	UHTToggleGraphNode_TextureSwitch* Node = GetTextureSwitch();
	TWeakObjectPtr<UHTToggleGraphNode_TextureSwitch> WeakNode = Node;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSwitchHeader(Node)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 3.0f, 0.0f, 1.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeTextField(
					LOCTEXT("ParameterField", "Parameter:"),
					TAttribute<FText>::CreateLambda([WeakNode]() { return WeakNode.IsValid() ? FText::FromString(WeakNode->ParameterName) : FText::GetEmpty(); }),
					LOCTEXT("ParameterHint", "e.g. BaseColor"),
					[WeakNode](const FString& Value) { if (WeakNode.IsValid()) { WeakNode->SetParameterNameInline(Value); } },
					LOCTEXT("ParameterTooltip", "Texture parameter set on every target. The list shows the texture parameters all connected materials have."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SComboButton)
				.ContentPadding(FMargin(2.0f, 1.0f))
				.ToolTipText(LOCTEXT("ParameterListTooltip", "Pick a texture parameter of the target materials."))
				.OnGetMenuContent(this, &SHTToggleGraphNode_TextureSwitch::MakeParameterMenu)
			]
		];
	for (int32 Option = 0; Node && Option < Node->OptionCount; ++Option)
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 1.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(HTToggleGraphNodeWidgets::FieldLabelWidth)
					[
						SNew(STextBlock)
						.Font(FieldFont())
						.Text(FText::Format(LOCTEXT("OptionRow", "Option {0}:"), FText::AsNumber(Option + 1)))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MakeOptionLabelBox(Node, Option)
				]
			];
	}
	return Box;
}

TSharedRef<SWidget> SHTToggleGraphNode_TextureSwitch::CreateInputPinExtra(UEdGraphPin* Pin)
{
	UHTToggleGraphNode_TextureSwitch* Node = GetTextureSwitch();
	const int32 Target = GetDynamicInputIndex(Pin);
	if (!Node || Target == INDEX_NONE)
	{
		return SNullWidget::NullWidget;
	}
	TWeakObjectPtr<UHTToggleGraphNode_TextureSwitch> WeakNode = Node;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(FieldFont())
			.Text_Lambda([WeakNode, Target]() { return WeakNode.IsValid() ? MakeConnectedPartText(WeakNode->GetTargetPart(Target)) : FText::GetEmpty(); })
		];
	if (!Node->GetTargetPart(Target))
	{
		return Box;
	}
	for (int32 Option = 0; Option < Node->OptionCount; ++Option)
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 1.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
				[
					SNew(STextBlock)
					.Font(FieldFont())
					.Text(FText::AsNumber(Option + 1))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MakeAssetPicker(
						UTexture::StaticClass(),
						[WeakNode, Target, Option]() { return WeakNode.IsValid() ? WeakNode->GetTargetTexture(Target, Option).ToSoftObjectPath().ToString() : FString(); },
						[WeakNode, Target, Option](UObject* Picked) { if (WeakNode.IsValid()) { WeakNode->SetTargetTextureInline(Target, Option, Cast<UTexture>(Picked)); } },
						FText::Format(LOCTEXT("TargetTextureTooltip", "Texture for option {0}. Empty keeps the material's own texture."), FText::AsNumber(Option + 1)))
				]
			];
	}
	return Box;
}

// ---------------------------------------------------------------- material switch

void SHTToggleGraphNode_MaterialSwitch::Construct(const FArguments& InArgs, UHTToggleGraphNode_MaterialSwitch* InNode)
{
	SHTToggleGraphNode::Construct(SHTToggleGraphNode::FArguments(), InNode);
}

UHTToggleGraphNode_MaterialSwitch* SHTToggleGraphNode_MaterialSwitch::GetMaterialSwitch() const
{
	return Cast<UHTToggleGraphNode_MaterialSwitch>(GraphNode);
}

TSharedRef<SWidget> SHTToggleGraphNode_MaterialSwitch::CreateFieldsArea()
{
	UHTToggleGraphNode_MaterialSwitch* Node = GetMaterialSwitch();
	TWeakObjectPtr<UHTToggleGraphNode_MaterialSwitch> WeakNode = Node;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSwitchHeader(Node)
		];
	for (int32 Option = 0; Node && Option < Node->OptionCount; ++Option)
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(HTToggleGraphNodeWidgets::FieldLabelWidth)
						[
							SNew(STextBlock)
							.Font(FieldFont())
							.Text(FText::Format(LOCTEXT("MaterialOptionRow", "Option {0}:"), FText::AsNumber(Option + 1)))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						MakeOptionLabelBox(Node, Option)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 1.0f, 0.0f, 0.0f))
				[
					MakeAssetPicker(
						UMaterialInterface::StaticClass(),
						[WeakNode, Option]() { return WeakNode.IsValid() ? WeakNode->GetOptionMaterial(Option).ToSoftObjectPath().ToString() : FString(); },
						[WeakNode, Option](UObject* Picked) { if (WeakNode.IsValid()) { WeakNode->SetOptionMaterialInline(Option, Cast<UMaterialInterface>(Picked)); } },
						FText::Format(LOCTEXT("OptionMaterialTooltip", "Material for option {0}. Empty restores each slot's original material."), FText::AsNumber(Option + 1)))
				]
			];
	}
	return Box;
}

TSharedRef<SWidget> SHTToggleGraphNode_MaterialSwitch::CreateInputPinExtra(UEdGraphPin* Pin)
{
	UHTToggleGraphNode_MaterialSwitch* Node = GetMaterialSwitch();
	const int32 Input = GetDynamicInputIndex(Pin);
	if (!Node || Input == INDEX_NONE)
	{
		return SNullWidget::NullWidget;
	}
	TWeakObjectPtr<UHTToggleGraphNode_MaterialSwitch> WeakNode = Node;
	return SNew(STextBlock)
		.Font(FieldFont())
		.Text_Lambda([WeakNode, Input]() { return WeakNode.IsValid() ? MakeConnectedPartText(WeakNode->GetSlotsPart(Input)) : FText::GetEmpty(); });
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
	const int32 Count = Part ? Part->GetEffectiveMaterialIDs().Num() : 0;
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
					return WeakNode.IsValid() && WeakNode->GetEffectiveMaterialIDs().Contains(SlotIndex) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
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

FText SHTToggleGraphNode_Part::GetMaterialButtonText() const
{
	const UHTToggleGraphNode_Part* Part = GetPart();
	return Part && !Part->SourceMaterial.IsNull()
		? FText::Format(LOCTEXT("ByMaterialPicked", "By material: {0}"), FText::FromString(Part->SourceMaterial.GetAssetName()))
		: LOCTEXT("ByMaterialNone", "By material...");
}

TSharedRef<SWidget> SHTToggleGraphNode_Part::MakeMaterialMenu() const
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
				.Text(LOCTEXT("NoPreviewMeshMaterials", "No preview mesh. Set the asset's Anim Blueprint (click empty graph space to show the asset details)."))
			];
	}
	TWeakObjectPtr<UHTToggleGraphNode_Part> WeakNode = Part;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	auto AddRow = [&Rows](const FText& Text, TFunction<void()> OnPick)
	{
		Rows->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked_Lambda([OnPick]()
				{
					OnPick();
					FSlateApplication::Get().DismissAllMenus();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FieldFont())
					.Text(Text)
				]
			];
	};
	AddRow(LOCTEXT("ManualSlots", "Manual (pick slots below)"), [WeakNode]() { if (WeakNode.IsValid()) { WeakNode->SetSourceMaterialInline(nullptr); } });
	TArray<UMaterialInterface*> Seen;
	for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
	{
		UMaterialInterface* Interface = Material.MaterialInterface;
		if (!Interface || Seen.Contains(Interface))
		{
			continue;
		}
		Seen.Add(Interface);
		const TArray<int32> Slots = UHTToggleGraphNode_Part::FindSlotsUsingMaterial(Mesh, Interface);
		const FString SlotText = FString::JoinBy(Slots, TEXT(", "), [](const int32 Slot) { return FString::FromInt(Slot); });
		TWeakObjectPtr<UMaterialInterface> WeakMaterial = Interface;
		AddRow(FText::FromString(FString::Printf(TEXT("%s  (slots %s)"), *Interface->GetName(), *SlotText)), [WeakNode, WeakMaterial]()
		{
			if (WeakNode.IsValid() && WeakMaterial.IsValid())
			{
				WeakNode->SetSourceMaterialInline(WeakMaterial.Get());
			}
		});
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
			.ToolTipText(LOCTEXT("ByMaterialTooltip", "Pick a material: every preview mesh slot that uses it joins the part, and the slots are looked up again on compile. A Texture Switch target uses this material."))
			.OnGetMenuContent(this, &SHTToggleGraphNode_Part::MakeMaterialMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FieldFont())
				.Text(this, &SHTToggleGraphNode_Part::GetMaterialButtonText)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 2.0f, 0.0f, 1.0f))
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
		];
}

// ---------------------------------------------------------------- factory

TSharedPtr<SGraphNode> FHTToggleGraphNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UHTToggleGraphNode_Switch* Switch = Cast<UHTToggleGraphNode_Switch>(Node))
	{
		return SNew(SHTToggleGraphNode_Switch, Switch);
	}
	if (UHTToggleGraphNode_TextureSwitch* TextureSwitch = Cast<UHTToggleGraphNode_TextureSwitch>(Node))
	{
		return SNew(SHTToggleGraphNode_TextureSwitch, TextureSwitch);
	}
	if (UHTToggleGraphNode_MaterialSwitch* MaterialSwitch = Cast<UHTToggleGraphNode_MaterialSwitch>(Node))
	{
		return SNew(SHTToggleGraphNode_MaterialSwitch, MaterialSwitch);
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
