#include "SHTMaterialInstanceCreator.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HTMaterialInstanceBuilder.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHTMaterialInstanceCreator"

namespace HTMaterialInstanceCreator
{
	enum EAssetIndex : int32
	{
		Material = 0,
		BaseColor,
		IDTexture,
		LightMap,
		NormalMap,
		Count
	};
}

void SHTMaterialInstanceCreator::Construct(const FArguments& InArgs)
{
	AssetPaths.SetNum(HTMaterialInstanceCreator::Count);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(14)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Heading", "Material instance creator"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
					.Text(LOCTEXT("Help", "Deletes every node in the selected material, rebuilds the required graph, and creates an instance with four texture overrides."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				MakeAssetPickerRow(LOCTEXT("Material", "Material"), UMaterial::StaticClass(), HTMaterialInstanceCreator::Material)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
						.WidthOverride(130)
						[
							SNew(STextBlock).Text(LOCTEXT("InstanceName", "Instance Name"))
						]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(InstanceNameBox, SEditableTextBox)
						.HintText(LOCTEXT("InstanceNameHint", "Defaults to MaterialName_Inst"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 6, 0, 8)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				MakeAssetPickerRow(LOCTEXT("BaseColor", "BaseColor"), UTexture2D::StaticClass(), HTMaterialInstanceCreator::BaseColor)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				MakeAssetPickerRow(LOCTEXT("IDTexture", "ID_Tex"), UTexture2D::StaticClass(), HTMaterialInstanceCreator::IDTexture)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				MakeAssetPickerRow(LOCTEXT("LightMap", "LightMap"), UTexture2D::StaticClass(), HTMaterialInstanceCreator::LightMap)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				MakeAssetPickerRow(LOCTEXT("NormalMap", "NormalMap"), UTexture2D::StaticClass(), HTMaterialInstanceCreator::NormalMap)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(130, 0, 0, 10)
			[
				SAssignNew(SaveAssetsCheckBox, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("SaveAssets", "Save assets"))
					]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SNew(SButton)
					.Text(LOCTEXT("Create", "Rebuild Material and Create Instance"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SHTMaterialInstanceCreator::OnCreateClicked)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
					.Padding(10)
					.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
					[
						SAssignNew(StatusText, STextBlock)
							.Text(LOCTEXT("Ready", "Ready."))
							.AutoWrapText(true)
					]
			]
		]
	];
}

TSharedRef<SWidget> SHTMaterialInstanceCreator::MakeAssetPickerRow(const FText& Label, UClass* AllowedClass, const int32 AssetIndex)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
				.WidthOverride(130)
				[
					SNew(STextBlock).Text(Label)
				]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SObjectPropertyEntryBox)
				.AllowedClass(AllowedClass)
				.AllowClear(true)
				.DisplayThumbnail(true)
				.ObjectPath(this, &SHTMaterialInstanceCreator::GetAssetPath, AssetIndex)
				.OnObjectChanged(FOnSetObject::CreateSP(this, &SHTMaterialInstanceCreator::OnAssetChanged, AssetIndex))
		];
}

FReply SHTMaterialInstanceCreator::OnCreateClicked()
{
	FHTMaterialInstanceBuilderParams Params;
	Params.MaterialPath = GetAssetPath(HTMaterialInstanceCreator::Material);
	Params.InstanceName = InstanceNameBox.IsValid() ? InstanceNameBox->GetText().ToString() : FString();
	Params.BaseColorTexturePath = GetAssetPath(HTMaterialInstanceCreator::BaseColor);
	Params.IDTexturePath = GetAssetPath(HTMaterialInstanceCreator::IDTexture);
	Params.LightMapTexturePath = GetAssetPath(HTMaterialInstanceCreator::LightMap);
	Params.NormalTexturePath = GetAssetPath(HTMaterialInstanceCreator::NormalMap);
	Params.bSaveAssets = SaveAssetsCheckBox.IsValid() && SaveAssetsCheckBox->IsChecked();

	const FHTMaterialInstanceBuilderResult Result = FHTMaterialInstanceBuilder::Build(Params);
	ShowStatus(FText::FromString(Result.ToDisplayString()), !Result.bSuccess);
	return FReply::Handled();
}

void SHTMaterialInstanceCreator::OnAssetChanged(const FAssetData& AssetData, const int32 AssetIndex)
{
	if (!AssetPaths.IsValidIndex(AssetIndex))
	{
		return;
	}

	AssetPaths[AssetIndex] = AssetData.IsValid() ? AssetData.GetSoftObjectPath().ToString() : FString();
	if (AssetIndex == HTMaterialInstanceCreator::Material && AssetData.IsValid() && InstanceNameBox.IsValid())
	{
		InstanceNameBox->SetText(FText::FromString(AssetData.AssetName.ToString() + TEXT("_Inst")));
	}
}

FString SHTMaterialInstanceCreator::GetAssetPath(const int32 AssetIndex) const
{
	return AssetPaths.IsValidIndex(AssetIndex) ? AssetPaths[AssetIndex] : FString();
}

void SHTMaterialInstanceCreator::ShowStatus(const FText& Text, const bool bError) const
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(Text);
		StatusText->SetColorAndOpacity(bError ? FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f)) : FSlateColor::UseForeground());
	}

	FNotificationInfo Info(bError ? LOCTEXT("Failed", "Material instance creation failed.") : LOCTEXT("Success", "Material instance created."));
	Info.ExpireDuration = bError ? 7.0f : 4.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bError ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}
}

#undef LOCTEXT_NAMESPACE
