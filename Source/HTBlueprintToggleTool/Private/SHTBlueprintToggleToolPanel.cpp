#include "SHTBlueprintToggleToolPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HTBlueprintToggleGenerator.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "PropertyCustomizationHelpers.h"
#include "SHTCookedAssetExporter.h"
#include "SHTMaterialInstanceCreator.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHTBlueprintToggleToolPanel"

namespace HTTogglePanel
{
	static const TCHAR* SettingsSection = TEXT("HTBlueprintToggleTool.BlueprintSettings");
	static const TCHAR* AnimBlueprintKey = TEXT("AnimBlueprintPath");
	static const TCHAR* SaveGameBlueprintKey = TEXT("SaveGameBlueprintPath");

	static FString TextBoxString(const TSharedPtr<SEditableTextBox>& TextBox)
	{
		return TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
	}

	static bool IsChecked(const TSharedPtr<SCheckBox>& CheckBox)
	{
		return CheckBox.IsValid() && CheckBox->IsChecked();
	}
}

void SHTBlueprintToggleToolPanel::Construct(const FArguments& InArgs)
{
	AnimBlueprintPath = TEXT("/Game/Characters/Player/010_nanally/nanally_animbp.nanally_animbp");
	SaveGameBlueprintPath = TEXT("/Game/Characters/Player/010_nanally/nanally_saved_bp.nanally_saved_bp");
	LoadBlueprintSettings();
	TexturePaths.SetNum(2);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(14)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Title", "HT Blueprint Toggle Tool"))
						.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("CookedAssetsTooltip", "Select cooked assets and copy them to an external packager directory."))
						.OnClicked(this, &SHTBlueprintToggleToolPanel::OnOpenCookedAssetExporterClicked)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Package"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("CookedAssetsButton", "Cooked Assets"))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ToolTipText(LOCTEXT("SettingsTooltip", "Select the Anim Blueprint and SaveGame Blueprint assets."))
						.OnClicked(this, &SHTBlueprintToggleToolPanel::OnOpenSettingsClicked)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Settings"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SettingsButton", "Settings"))
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SAssignNew(AssetSummaryText, STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 12)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Subtitle", "Generate material visibility or multi-slot, multi-texture toggle nodes. Save Variable and Save Slot are derived from Anim Variable."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 10)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(150)
						[
							SNew(STextBlock).Text(LOCTEXT("FunctionSwitch", "Function Switch"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSegmentedControl<EHTBlueprintToggleMode>)
						.Value_Lambda([this]() { return ToggleMode; })
						.OnValueChanged(this, &SHTBlueprintToggleToolPanel::OnToggleModeChanged)
						+ SSegmentedControl<EHTBlueprintToggleMode>::Slot(EHTBlueprintToggleMode::MaterialSection)
						.Text(LOCTEXT("MaterialMode", "Material visibility"))
						+ SSegmentedControl<EHTBlueprintToggleMode>::Slot(EHTBlueprintToggleMode::Texture)
						.Text(LOCTEXT("TextureMode", "Texture switch"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8, 0, 0, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("MaterialInstanceTooltip", "Rebuild a material graph and create a four-texture material instance."))
						.OnClicked(this, &SHTBlueprintToggleToolPanel::OnOpenMaterialInstanceCreatorClicked)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SImage).Image(FAppStyle::GetBrush("ClassIcon.MaterialInstanceConstant"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(LOCTEXT("MaterialInstanceButton", "Material Instance"))
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 8, 0, 8)
				[
					SNew(SSeparator)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 6)
				[
					MakeTextRow(
						LOCTEXT("ToggleVar", "Anim Variable"),
						SAssignNew(ToggleVariableBox, SEditableTextBox)
						.HintText(LOCTEXT("ToggleVarHint", "Example: Glove"))
						.ToolTipText(LOCTEXT("ToggleVarTooltip", "Save Variable will be AnimVariable + Save. Save Slot will match Anim Variable.")))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 6)
				[
					MakeTextRow(
						LOCTEXT("KeyName", "Key"),
						SAssignNew(KeyNameBox, SEditableTextBox)
						.HintText(LOCTEXT("KeyHint", "Examples: 9, =, ctrl 6, shift 6, alt 6")))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ModeOptionsSwitcher, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return ToggleMode == EHTBlueprintToggleMode::Texture ? 1 : 0; })

					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							MakeTextRow(
								LOCTEXT("MaterialIDs", "Material ID(s)"),
								SAssignNew(MaterialIDsBox, SEditableTextBox)
								.HintText(LOCTEXT("MaterialIDsHint", "Single: 16    Multiple: 13,20")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(150, 0, 0, 6)
						[
							SAssignNew(MultiMaterialCheckBox, SCheckBox)
							.IsChecked(ECheckBoxState::Unchecked)
							[
								SNew(STextBlock).Text(LOCTEXT("MultiMaterial", "Multiple materials"))
							]
						]
					]

					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							MakeTextRow(
								LOCTEXT("MaterialElementIndices", "Material Slot(s)"),
								SAssignNew(MaterialSlotsBox, SEditableTextBox)
								.Text(FText::FromString(TEXT("0")))
								.HintText(LOCTEXT("MaterialElementIndicesHint", "Single: 12    Multiple: 12,13")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							MakeMaterialPickerRow()
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							MakeTextRow(
								LOCTEXT("TextureParameter", "Texture Parameter"),
								SAssignNew(TextureParameterBox, SEditableTextBox)
								.Text(FText::FromString(TEXT("BaseColor")))
								.HintText(LOCTEXT("TextureParameterHint", "Example: BaseColor")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SBox).WidthOverride(150)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ToolTipText(LOCTEXT("AddTextureTooltip", "Add another texture state."))
								.OnClicked(this, &SHTBlueprintToggleToolPanel::OnAddTextureClicked)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0, 0, 4, 0)
									[
										SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus"))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddTexture", "Add texture"))
									]
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(TextureRowsBox, SVerticalBox)
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 8, 0, 8)
				[
					SNew(SSeparator)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 6)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 18, 0)
					[
						SAssignNew(InitGraphCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("GenerateInit", "Initialize graph"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 18, 0)
					[
						SAssignNew(UpdateGraphCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("GenerateUpdate", "Update graph"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(SaveAssetsCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveAssets", "Save assets"))
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 10, 0, 10)
				[
					SNew(SButton)
					.Text(LOCTEXT("GenerateButton", "Generate Toggle Nodes"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SHTBlueprintToggleToolPanel::OnGenerateClicked)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 6, 0, 0)
				[
					SNew(SBorder)
					.Padding(10)
					.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
					[
						SAssignNew(StatusText, STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("InitialStatus", "Ready."))
					]
				]
			]
		]
	];

	UpdateAssetSummaryText();
	RebuildTextureRows();
}

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeTextRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(Label)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			TextBox
		];
}

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeBlueprintPickerRow(const FText& Label, bool bAnimBlueprint)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(Label)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UBlueprint::StaticClass())
			.AllowClear(false)
			.DisplayThumbnail(false)
			.ObjectPath(bAnimBlueprint ? TAttribute<FString>(this, &SHTBlueprintToggleToolPanel::GetAnimBlueprintPath) : TAttribute<FString>(this, &SHTBlueprintToggleToolPanel::GetSaveGameBlueprintPath))
			.OnObjectChanged(bAnimBlueprint ? FOnSetObject::CreateSP(this, &SHTBlueprintToggleToolPanel::OnAnimBlueprintChanged) : FOnSetObject::CreateSP(this, &SHTBlueprintToggleToolPanel::OnSaveGameBlueprintChanged))
		];
}

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeMaterialPickerRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(LOCTEXT("SourceMaterial", "Source Material"))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UMaterialInterface::StaticClass())
			.AllowClear(false)
			.DisplayThumbnail(true)
			.ObjectPath(this, &SHTBlueprintToggleToolPanel::GetSourceMaterialPath)
			.OnObjectChanged(this, &SHTBlueprintToggleToolPanel::OnSourceMaterialChanged)
		];
}

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeTexturePickerRow(int32 TextureIndex)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("TextureStateLabel", "Texture {0} (State {1})"), FText::AsNumber(TextureIndex + 1), FText::AsNumber(TextureIndex)))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UTexture2D::StaticClass())
			.AllowClear(false)
			.DisplayThumbnail(true)
			.ObjectPath(TAttribute<FString>::CreateLambda([this, TextureIndex]()
			{
				return TexturePaths.IsValidIndex(TextureIndex) ? TexturePaths[TextureIndex] : FString();
			}))
			.OnObjectChanged(FOnSetObject::CreateLambda([this, TextureIndex](const FAssetData& AssetData)
			{
				OnTextureChanged(AssetData, TextureIndex);
			}))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Visibility_Lambda([this]() { return TexturePaths.Num() > 2 ? EVisibility::Visible : EVisibility::Collapsed; })
			.ToolTipText(LOCTEXT("RemoveTextureTooltip", "Remove this texture state."))
			.OnClicked(this, &SHTBlueprintToggleToolPanel::OnRemoveTextureClicked, TextureIndex)
			[
				SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete"))
			]
		];
}

void SHTBlueprintToggleToolPanel::RebuildTextureRows()
{
	if (!TextureRowsBox.IsValid())
	{
		return;
	}

	TextureRowsBox->ClearChildren();
	for (int32 TextureIndex = 0; TextureIndex < TexturePaths.Num(); ++TextureIndex)
	{
		TextureRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 6)
		[
			MakeTexturePickerRow(TextureIndex)
		];
	}
}

FReply SHTBlueprintToggleToolPanel::OnAddTextureClicked()
{
	TexturePaths.AddDefaulted();
	RebuildTextureRows();
	return FReply::Handled();
}

FReply SHTBlueprintToggleToolPanel::OnRemoveTextureClicked(int32 TextureIndex)
{
	if (TexturePaths.Num() > 2 && TexturePaths.IsValidIndex(TextureIndex))
	{
		TexturePaths.RemoveAt(TextureIndex);
		RebuildTextureRows();
	}
	return FReply::Handled();
}

FReply SHTBlueprintToggleToolPanel::OnOpenMaterialInstanceCreatorClicked()
{
	if (MaterialInstanceCreatorWindow.IsValid())
	{
		MaterialInstanceCreatorWindow.Pin()->BringToFront();
		return FReply::Handled();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("MaterialInstanceCreatorTitle", "HT Material Instance Creator"))
		.ClientSize(FVector2D(760.0f, 600.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::UserSized);

	MaterialInstanceCreatorWindow = Window;
	Window->SetContent(SNew(SHTMaterialInstanceCreator));
	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

FReply SHTBlueprintToggleToolPanel::OnOpenCookedAssetExporterClicked()
{
	if (CookedAssetExporterWindow.IsValid())
	{
		CookedAssetExporterWindow.Pin()->BringToFront();
		return FReply::Handled();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("CookedAssetExporterTitle", "HT Cooked Asset Exporter"))
		.ClientSize(FVector2D(920.0f, 680.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::UserSized);

	CookedAssetExporterWindow = Window;
	Window->SetContent(SNew(SHTCookedAssetExporter));
	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

FReply SHTBlueprintToggleToolPanel::OnOpenSettingsClicked()
{
	if (SettingsWindow.IsValid())
	{
		SettingsWindow.Pin()->BringToFront();
		return FReply::Handled();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SettingsTitle", "HT Toggle Tool Settings"))
		.ClientSize(FVector2D(660.0f, 220.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize);

	SettingsWindow = Window;

	Window->SetContent(
		SNew(SBorder)
		.Padding(14)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SettingsHelp", "Select Blueprint assets from the current project."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				MakeBlueprintPickerRow(LOCTEXT("AnimBP", "Anim Blueprint"), true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 14)
			[
				MakeBlueprintPickerRow(LOCTEXT("SaveBP", "SaveGame Blueprint"), false)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBox)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(8, 0, 0, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CloseSettings", "Close"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SHTBlueprintToggleToolPanel::OnCloseSettingsClicked)
				]
			]
		]);

	FSlateApplication::Get().AddModalWindow(Window, SharedThis(this));
	SettingsWindow.Reset();
	return FReply::Handled();
}

FReply SHTBlueprintToggleToolPanel::OnCloseSettingsClicked()
{
	if (SettingsWindow.IsValid())
	{
		SettingsWindow.Pin()->RequestDestroyWindow();
		SettingsWindow.Reset();
	}
	return FReply::Handled();
}

FString SHTBlueprintToggleToolPanel::GetAnimBlueprintPath() const
{
	return AnimBlueprintPath;
}

FString SHTBlueprintToggleToolPanel::GetSaveGameBlueprintPath() const
{
	return SaveGameBlueprintPath;
}

FString SHTBlueprintToggleToolPanel::GetSourceMaterialPath() const
{
	return SourceMaterialPath;
}

void SHTBlueprintToggleToolPanel::OnToggleModeChanged(EHTBlueprintToggleMode NewMode)
{
	ToggleMode = NewMode;
	if (ModeOptionsSwitcher.IsValid())
	{
		ModeOptionsSwitcher->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SHTBlueprintToggleToolPanel::OnAnimBlueprintChanged(const FAssetData& AssetData)
{
	if (AssetData.IsValid())
	{
		AnimBlueprintPath = AssetData.GetSoftObjectPath().ToString();
		SaveBlueprintSettings();
		UpdateAssetSummaryText();
	}
}

void SHTBlueprintToggleToolPanel::OnSaveGameBlueprintChanged(const FAssetData& AssetData)
{
	if (AssetData.IsValid())
	{
		SaveGameBlueprintPath = AssetData.GetSoftObjectPath().ToString();
		SaveBlueprintSettings();
		UpdateAssetSummaryText();
	}
}

void SHTBlueprintToggleToolPanel::LoadBlueprintSettings()
{
	if (!GConfig)
	{
		return;
	}

	FString SavedPath;
	if (GConfig->GetString(HTTogglePanel::SettingsSection, HTTogglePanel::AnimBlueprintKey, SavedPath, GEditorPerProjectIni) && !SavedPath.IsEmpty())
	{
		AnimBlueprintPath = MoveTemp(SavedPath);
	}
	if (GConfig->GetString(HTTogglePanel::SettingsSection, HTTogglePanel::SaveGameBlueprintKey, SavedPath, GEditorPerProjectIni) && !SavedPath.IsEmpty())
	{
		SaveGameBlueprintPath = MoveTemp(SavedPath);
	}
}

void SHTBlueprintToggleToolPanel::SaveBlueprintSettings() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetString(HTTogglePanel::SettingsSection, HTTogglePanel::AnimBlueprintKey, *AnimBlueprintPath, GEditorPerProjectIni);
	GConfig->SetString(HTTogglePanel::SettingsSection, HTTogglePanel::SaveGameBlueprintKey, *SaveGameBlueprintPath, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SHTBlueprintToggleToolPanel::OnSourceMaterialChanged(const FAssetData& AssetData)
{
	if (AssetData.IsValid())
	{
		SourceMaterialPath = AssetData.GetSoftObjectPath().ToString();
	}
}

void SHTBlueprintToggleToolPanel::OnTextureChanged(const FAssetData& AssetData, int32 TextureIndex)
{
	if (AssetData.IsValid() && TexturePaths.IsValidIndex(TextureIndex))
	{
		TexturePaths[TextureIndex] = AssetData.GetSoftObjectPath().ToString();
	}
}

void SHTBlueprintToggleToolPanel::UpdateAssetSummaryText() const
{
	if (AssetSummaryText.IsValid())
	{
		AssetSummaryText->SetText(FText::Format(
			LOCTEXT("AssetSummary", "AnimBP: {0}    SaveGame: {1}"),
			FText::FromString(GetShortAssetName(AnimBlueprintPath)),
			FText::FromString(GetShortAssetName(SaveGameBlueprintPath))));
	}
}

FString SHTBlueprintToggleToolPanel::GetShortAssetName(const FString& ObjectPath) const
{
	int32 DotIndex = INDEX_NONE;
	if (ObjectPath.FindLastChar(TEXT('.'), DotIndex))
	{
		return ObjectPath.Mid(DotIndex + 1);
	}

	int32 SlashIndex = INDEX_NONE;
	if (ObjectPath.FindLastChar(TEXT('/'), SlashIndex))
	{
		return ObjectPath.Mid(SlashIndex + 1);
	}

	return ObjectPath;
}

bool SHTBlueprintToggleToolPanel::ParseMaterialIDs(TArray<int32>& OutMaterialIDs, FString& OutError) const
{
	OutMaterialIDs.Reset();

	FString RawValue = HTTogglePanel::TextBoxString(MaterialIDsBox);
	RawValue.TrimStartAndEndInline();
	RawValue.ReplaceInline(TEXT("，"), TEXT(","));
	RawValue.ReplaceInline(TEXT(";"), TEXT(","));
	RawValue.ReplaceInline(TEXT("；"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\r"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\n"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\t"), TEXT(","));

	if (RawValue.IsEmpty())
	{
		OutError = TEXT("请填写 Material ID。单材质示例: 16；多材质示例: 13,20");
		return false;
	}

	TArray<FString> Parts;
	RawValue.ParseIntoArray(Parts, TEXT(","), true);
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}

		int32 MaterialID = INDEX_NONE;
		if (!LexTryParseString(MaterialID, *Part) || MaterialID < 0)
		{
			OutError = FString::Printf(TEXT("Material ID 无效: %s"), *Part);
			return false;
		}

		OutMaterialIDs.Add(MaterialID);
	}

	const bool bMultiMaterial = MultiMaterialCheckBox.IsValid() && MultiMaterialCheckBox->IsChecked();
	if (bMultiMaterial && OutMaterialIDs.Num() < 2)
	{
		OutError = TEXT("多材质模式至少需要填写两个 Material ID，例如: 13,20");
		return false;
	}

	if (!bMultiMaterial && OutMaterialIDs.Num() != 1)
	{
		OutError = TEXT("单材质模式只能填写一个 Material ID；如果要填写多个，请勾选 Multiple materials。");
		return false;
	}

	return true;
}

bool SHTBlueprintToggleToolPanel::ParseTextureMaterialSlots(TArray<int32>& OutMaterialSlots, FString& OutError) const
{
	OutMaterialSlots.Reset();

	FString RawValue = HTTogglePanel::TextBoxString(MaterialSlotsBox);
	RawValue.TrimStartAndEndInline();
	RawValue.ReplaceInline(TEXT(";"), TEXT(","));
	RawValue.ReplaceInline(TEXT(" "), TEXT(","));
	RawValue.ReplaceInline(TEXT("\r"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\n"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\t"), TEXT(","));

	if (RawValue.IsEmpty())
	{
		OutError = TEXT("Enter at least one Material Slot, for example 12 or 12,13.");
		return false;
	}

	TArray<FString> Parts;
	RawValue.ParseIntoArray(Parts, TEXT(","), true);
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}

		int32 MaterialSlot = INDEX_NONE;
		if (!LexTryParseString(MaterialSlot, *Part) || MaterialSlot < 0)
		{
			OutError = FString::Printf(TEXT("Invalid Material Slot: %s"), *Part);
			return false;
		}

		OutMaterialSlots.AddUnique(MaterialSlot);
	}

	if (OutMaterialSlots.Num() == 0)
	{
		OutError = TEXT("Enter at least one valid Material Slot.");
		return false;
	}

	return true;
}

void SHTBlueprintToggleToolPanel::ShowPanelError(const FText& ErrorText) const
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(ErrorText);
	}

	FNotificationInfo Info(ErrorText);
	Info.ExpireDuration = 6.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

FReply SHTBlueprintToggleToolPanel::OnGenerateClicked()
{
	using namespace HTTogglePanel;

	const FString ToggleVariableName = TextBoxString(ToggleVariableBox).TrimStartAndEnd();
	const FString KeyName = TextBoxString(KeyNameBox).TrimStartAndEnd();
	if (ToggleVariableName.IsEmpty())
	{
		ShowPanelError(LOCTEXT("MissingAnimVariable", "请填写 Anim Variable。"));
		return FReply::Handled();
	}

	if (KeyName.IsEmpty())
	{
		ShowPanelError(LOCTEXT("MissingKey", "请填写 Key。"));
		return FReply::Handled();
	}

	TArray<int32> MaterialIDs;
	TArray<int32> TextureMaterialSlots;
	if (ToggleMode == EHTBlueprintToggleMode::MaterialSection)
	{
		FString MaterialIDError;
		if (!ParseMaterialIDs(MaterialIDs, MaterialIDError))
		{
			ShowPanelError(FText::FromString(MaterialIDError));
			return FReply::Handled();
		}
	}
	else
	{
		FString MaterialSlotError;
		if (!ParseTextureMaterialSlots(TextureMaterialSlots, MaterialSlotError))
		{
			ShowPanelError(FText::FromString(MaterialSlotError));
			return FReply::Handled();
		}
		if (!IsChecked(InitGraphCheckBox))
		{
			ShowPanelError(LOCTEXT("TextureNeedsInit", "Texture switch mode requires Initialize graph so the dynamic material instance can be created."));
			return FReply::Handled();
		}
		if (TextBoxString(TextureParameterBox).TrimStartAndEnd().IsEmpty())
		{
			ShowPanelError(LOCTEXT("MissingTextureParameter", "Choose a Texture Parameter name, for example BaseColor."));
			return FReply::Handled();
		}
		if (SourceMaterialPath.IsEmpty())
		{
			ShowPanelError(LOCTEXT("MissingSourceMaterial", "Choose the Source Material used by these textures."));
			return FReply::Handled();
		}
		if (TexturePaths.Num() < 2)
		{
			ShowPanelError(LOCTEXT("TooFewTextures", "Texture switch mode requires at least two textures."));
			return FReply::Handled();
		}
		for (int32 TextureIndex = 0; TextureIndex < TexturePaths.Num(); ++TextureIndex)
		{
			if (TexturePaths[TextureIndex].IsEmpty())
			{
				ShowPanelError(FText::Format(LOCTEXT("MissingTextureAtIndex", "Choose Texture {0}."), FText::AsNumber(TextureIndex + 1)));
				return FReply::Handled();
			}
		}
	}

	FHTBlueprintToggleGeneratorParams Params;
	Params.Mode = ToggleMode;
	Params.AnimBlueprintPath = AnimBlueprintPath;
	Params.SaveGameBlueprintPath = SaveGameBlueprintPath;
	Params.ToggleVariableName = ToggleVariableName;
	Params.SaveVariableName.Empty();
	Params.SlotName.Empty();
	Params.KeyName = KeyName;
	Params.MaterialIDs = MaterialIDs;
	Params.MaterialID = MaterialIDs.Num() > 0 ? MaterialIDs[0] : 0;
	Params.SectionIndex = 0;
	Params.LODIndex = 0;
	Params.MaterialElementIndex = TextureMaterialSlots.Num() > 0 ? TextureMaterialSlots[0] : 0;
	Params.MaterialElementIndices = TextureMaterialSlots;
	Params.SourceMaterialPath = SourceMaterialPath;
	Params.TextureParameterName = TextBoxString(TextureParameterBox).TrimStartAndEnd();
	Params.TexturePaths = TexturePaths;
	Params.bGenerateInitializeGraph = IsChecked(InitGraphCheckBox);
	Params.bGenerateUpdateGraph = IsChecked(UpdateGraphCheckBox);
	Params.bSaveAssets = IsChecked(SaveAssetsCheckBox);

	FHTBlueprintToggleGeneratorResult Result = FHTBlueprintToggleGenerator::Generate(Params);
	const FString DisplayText = Result.ToDisplayString();

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(DisplayText));
	}

	FNotificationInfo Info(Result.bSuccess ? LOCTEXT("GenerateSuccess", "HT toggle nodes generated.") : LOCTEXT("GenerateFailed", "HT toggle node generation failed."));
	Info.ExpireDuration = Result.bSuccess ? 4.0f : 8.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(Result.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
