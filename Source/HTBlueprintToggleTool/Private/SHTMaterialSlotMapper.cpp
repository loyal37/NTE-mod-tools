#include "SHTMaterialSlotMapper.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Rendering/RenderingCommon.h"
#include "RenderingThread.h"
#include "Slate/SlateTextures.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateTextureData.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHTMaterialSlotMapper"

namespace HTMaterialSlotMapper
{
	static FString TextBoxString(const TSharedPtr<SEditableTextBox>& TextBox)
	{
		return TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
	}

	static FString NormalizeObjectPath(FString Path)
	{
		Path.TrimStartAndEndInline();
		if (!Path.IsEmpty() && !Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
		}
		return Path;
	}

	class FRenderedMaterialThumbnailViewport final : public ISlateViewport
	{
	public:
		FRenderedMaterialThumbnailViewport(UObject* MaterialAsset, const uint32 InSize)
			: Size(static_cast<int32>(InSize), static_cast<int32>(InSize))
		{
			if (!MaterialAsset)
			{
				return;
			}

			FObjectThumbnail ObjectThumbnail;
			ThumbnailTools::RenderThumbnail(
				MaterialAsset,
				InSize,
				InSize,
				ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
				nullptr,
				&ObjectThumbnail);

			const FImageView Image = ObjectThumbnail.GetImage();
			if (!Image.RawData || Image.SizeX <= 0 || Image.SizeY <= 0)
			{
				return;
			}

			TSharedPtr<FSlateTextureData, ESPMode::ThreadSafe> TextureData = MakeShared<FSlateTextureData, ESPMode::ThreadSafe>(Image);
			Texture = MakeUnique<FSlateTexture2DRHIRef>(
				TextureData->GetWidth(),
				TextureData->GetHeight(),
				PF_B8G8R8A8,
				TextureData,
				TexCreate_SRGB);
			BeginInitResource(Texture.Get());
			FlushRenderingCommands();
		}

		virtual ~FRenderedMaterialThumbnailViewport() override
		{
			if (Texture)
			{
				BeginReleaseResource(Texture.Get());
				FlushRenderingCommands();
				Texture.Reset();
			}
		}

		virtual FIntPoint GetSize() const override
		{
			return Size;
		}

		virtual FSlateShaderResource* GetViewportRenderTargetTexture() const override
		{
			return Texture && Texture->IsValid() ? Texture.Get() : nullptr;
		}

		virtual bool RequiresVsync() const override
		{
			return false;
		}

	private:
		FIntPoint Size;
		TUniquePtr<FSlateTexture2DRHIRef> Texture;
	};
}

void SHTMaterialSlotMapper::Construct(const FArguments& InArgs)
{
	AnimBlueprintPath = InArgs._AnimBlueprintPath;
	CharacterFolderPath = InArgs._CharacterFolderPath;

	FString LoadError;
	const bool bLoaded = LoadContext(LoadError);
	if (MaterialOptions.Num() > 0)
	{
		MappingRows.Add(MakeShared<FMappingRow>());
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(14)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Heading", "Material slot mapper"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Help", "Assign materials to Skeletal Mesh material slots by exact slot-name matches or by batch mapping selected slot IDs."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SAssignNew(SummaryText, STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 10, 0)
				[
					SNew(SBox)
					.WidthOverride(720.0f)
					[
					SNew(SBorder)
					.Padding(10)
					.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SlotsHeading", "Skeletal Mesh Slots"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(SlotListBox, SVerticalBox)
							]
						]
					]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBorder)
					.Padding(10)
					.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 8)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("MappingsHeading", "Material Mappings"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 8)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0, 0, 6, 0)
							[
							SNew(SButton)
								.Text(LOCTEXT("AutoMatch", "Match Names"))
								.ToolTipText(LOCTEXT("AutoMatchTip", "Assign folder materials whose asset name exactly matches a material slot name."))
								.OnClicked(this, &SHTMaterialSlotMapper::OnAutoMatchClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0, 0, 6, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("AddMapping", "Add"))
								.OnClicked(this, &SHTMaterialSlotMapper::OnAddMappingClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0, 0, 6, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("ClearChecked", "Clear"))
								.OnClicked(this, &SHTMaterialSlotMapper::OnClearCheckedSlotsClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("RefreshPreviews", "Refresh"))
								.ToolTipText(LOCTEXT("RefreshPreviewsTip", "Refresh material preview thumbnails."))
								.OnClicked(this, &SHTMaterialSlotMapper::OnRefreshPreviewsClicked)
							]
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(MappingRowsBox, SVerticalBox)
							]
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 10, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 18, 0)
				[
					SAssignNew(SaveAssetsCheckBox, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("SaveAssets", "Save assets"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyMappings", "Apply Mappings"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SHTMaterialSlotMapper::OnApplyMappingsClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(10)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				[
					SAssignNew(StatusText, STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("Ready", "Ready."))
				]
			]
		]
	];

	if (SummaryText.IsValid())
	{
		SummaryText->SetText(FText::Format(
			LOCTEXT("Summary", "Skeletal Mesh: {0}    Character Folder: {1}    Materials found: {2}"),
			FText::FromString(SkeletalMesh.IsValid() ? SkeletalMesh->GetName() : FString(TEXT("None"))),
			FText::FromString(CharacterFolderPath),
			FText::AsNumber(MaterialOptions.Num())));
	}
	RebuildSlotList();
	RebuildMappingRows();
	if (!bLoaded)
	{
		ShowStatus(FText::FromString(LoadError), true);
	}
}

bool SHTMaterialSlotMapper::LoadContext(FString& OutError)
{
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *HTMaterialSlotMapper::NormalizeObjectPath(AnimBlueprintPath));
	if (!AnimBlueprint)
	{
		OutError = TEXT("The selected Anim Blueprint could not be loaded.");
		return false;
	}

	USkeletalMesh* Mesh = AnimBlueprint->GetPreviewMesh(false);
	if (!Mesh && AnimBlueprint->TargetSkeleton)
	{
		Mesh = AnimBlueprint->TargetSkeleton->GetPreviewMesh(true);
	}
	if (!Mesh)
	{
		OutError = TEXT("The selected Anim Blueprint has no preview Skeletal Mesh.");
		return false;
	}
	SkeletalMesh = Mesh;

	const TArray<FSkeletalMaterial>& MeshMaterials = Mesh->GetMaterials();
	Slots.Reserve(MeshMaterials.Num());
	for (int32 SlotIndex = 0; SlotIndex < MeshMaterials.Num(); ++SlotIndex)
	{
		const FSkeletalMaterial& MeshMaterial = MeshMaterials[SlotIndex];
		FSlotEntry& Entry = Slots.AddDefaulted_GetRef();
		Entry.Index = SlotIndex;
		Entry.SlotName = !MeshMaterial.MaterialSlotName.IsNone() ? MeshMaterial.MaterialSlotName : MeshMaterial.ImportedMaterialSlotName;
		Entry.Material = MeshMaterial.MaterialInterface;
	}

	if (CharacterFolderPath.IsEmpty())
	{
		OutError = TEXT("Choose a Character Folder in Settings first.");
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*CharacterFolderPath), AssetDataList, true);
	for (const FAssetData& AssetData : AssetDataList)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(AssetData.GetAsset());
		if (!Material)
		{
			continue;
		}

		TSharedRef<FMaterialOption> Option = MakeShared<FMaterialOption>();
		Option->DisplayName = Material->GetName();
		Option->ObjectPath = Material->GetPathName();
		Option->Material = Material;
		MaterialOptions.Add(Option);
	}
	MaterialOptions.Sort([](const TSharedPtr<FMaterialOption>& Left, const TSharedPtr<FMaterialOption>& Right)
	{
		return Left.IsValid() && Right.IsValid() && Left->DisplayName < Right->DisplayName;
	});

	if (MaterialOptions.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No Material or Material Instance assets were found under %s."), *CharacterFolderPath);
		return false;
	}
	return true;
}

void SHTMaterialSlotMapper::RebuildSlotList()
{
	if (!SlotListBox.IsValid())
	{
		return;
	}

	SlotListBox->ClearChildren();
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		SlotListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			MakeSlotRow(SlotIndex)
		];
	}
}

void SHTMaterialSlotMapper::RebuildMappingRows()
{
	if (!MappingRowsBox.IsValid())
	{
		return;
	}

	MaterialPreviewViewports.Reset();
	MappingRowsBox->ClearChildren();
	for (const TSharedPtr<FMappingRow>& Mapping : MappingRows)
	{
		MappingRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 8)
		[
			MakeMappingRow(Mapping)
		];
	}
}

TSharedRef<SWidget> SHTMaterialSlotMapper::MakeSlotRow(const int32 SlotIndex)
{
	FSlotEntry& Entry = Slots[SlotIndex];
	const FString MaterialName = Entry.Material.IsValid() ? Entry.Material->GetName() : FString(TEXT("None"));
	const bool bAssigned = IsSlotAssigned(Entry.Index);
	const FSlateColor SlotTextColor = bAssigned
		? FSlateColor(FLinearColor(0.44f, 0.44f, 0.44f, 1.0f))
		: FSlateColor::UseForeground();
	const FSlateColor CurrentTextColor = bAssigned
		? FSlateColor(FLinearColor(0.34f, 0.34f, 0.34f, 1.0f))
		: FSlateColor::UseSubduedForeground();
	return SNew(SBorder)
		.Padding(FMargin(8, 7))
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.BorderBackgroundColor(bAssigned ? FLinearColor(0.08f, 0.08f, 0.08f, 1.0f) : FLinearColor::White)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, SlotIndex]()
				{
					return Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, SlotIndex](const ECheckBoxState NewState)
				{
					OnSlotCheckStateChanged(NewState, SlotIndex);
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("SlotLabel", "Slot {0}: {1}"), FText::AsNumber(Entry.Index), FText::FromString(GetSlotName(Entry))))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.ColorAndOpacity(SlotTextColor)
					.MinDesiredWidth(0.0f)
					.Clipping(EWidgetClipping::ClipToBounds)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("CurrentMaterial", "Current: {0}"), FText::FromString(MaterialName)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
					.ColorAndOpacity(CurrentTextColor)
					.MinDesiredWidth(0.0f)
					.Clipping(EWidgetClipping::ClipToBounds)
				]
			]
		];
}

TSharedRef<SWidget> SHTMaterialSlotMapper::MakeMappingRow(TSharedPtr<FMappingRow> Mapping)
{
	return SNew(SBorder)
		.Padding(8)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				MakeMaterialCombo(Mapping)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SBox)
					.WidthOverride(70)
					[
						SNew(STextBlock).Text(LOCTEXT("SlotIds", "Slot IDs"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(Mapping->SlotIdsBox, SEditableTextBox)
					.Text(FText::FromString(Mapping->SlotIds))
					.HintText(LOCTEXT("SlotIdsHint", "Example: 1,5,15,16"))
					.OnTextChanged_Lambda([this, Mapping](const FText& NewText)
					{
						if (Mapping.IsValid())
						{
							Mapping->SlotIds = NewText.ToString();
							RefreshAssignedSlotIds();
							RebuildSlotList();
						}
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseChecked", "Use checked slots"))
					.OnClicked(this, &SHTMaterialSlotMapper::OnUseCheckedSlotsClicked, Mapping)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBox)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveMapping", "Remove"))
					.Visibility_Lambda([this]() { return MappingRows.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked(this, &SHTMaterialSlotMapper::OnRemoveMappingClicked, Mapping)
				]
			]
		];
}

TSharedRef<SWidget> SHTMaterialSlotMapper::MakeMaterialCombo(TSharedPtr<FMappingRow> Mapping)
{
	if (Mapping->MaterialPath.IsEmpty() && MaterialOptions.Num() > 0)
	{
		Mapping->MaterialPath = MaterialOptions[0]->ObjectPath;
	}

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	Row->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0, 0, 8, 0)
	[
		SNew(SBox)
		.WidthOverride(70)
		[
			SNew(STextBlock).Text(LOCTEXT("Material", "Material"))
		]
	];
	Row->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0, 0, 8, 0)
	[
		SAssignNew(Mapping->MaterialPreviewBox, SBox)
		.WidthOverride(42)
		.HeightOverride(42)
	];
	Row->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(SComboBox<TSharedPtr<FMaterialOption>>)
		.OptionsSource(&MaterialOptions)
		.OnGenerateWidget(this, &SHTMaterialSlotMapper::MakeMaterialComboItem)
		.OnSelectionChanged_Lambda([this, Mapping](TSharedPtr<FMaterialOption> Selected, ESelectInfo::Type)
		{
			if (Selected.IsValid())
			{
				Mapping->MaterialPath = Selected->ObjectPath;
				RefreshMappingMaterialPreview(Mapping);
			}
		})
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
			.Text_Lambda([this, Mapping]()
			{
				const TSharedPtr<FMaterialOption> Option = FindMaterialOptionByPath(Mapping->MaterialPath);
				return FText::FromString(Option.IsValid() ? Option->DisplayName : FString(TEXT("Choose material")));
			})
		]
	];
	RefreshMappingMaterialPreview(Mapping);
	return Row;
}

TSharedRef<SWidget> SHTMaterialSlotMapper::MakeMaterialComboItem(TSharedPtr<FMaterialOption> Option)
{
	UMaterialInterface* Material = Option.IsValid() ? Option->Material.Get() : nullptr;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 2, 8, 2)
		[
			MakeMaterialPreview(Material, 42)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
			.Text(FText::FromString(Option.IsValid() ? Option->DisplayName : FString(TEXT("None"))))
		];
}

TSharedRef<SWidget> SHTMaterialSlotMapper::MakeMaterialPreview(UMaterialInterface* Material, const uint32 Size)
{
	if (!Material)
	{
		return SNew(SBorder)
			.Padding(1)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			[
				SNew(SBox)
				.WidthOverride(Size)
				.HeightOverride(Size)
			];
	}

	TSharedPtr<ISlateViewport> Viewport = MakeShared<HTMaterialSlotMapper::FRenderedMaterialThumbnailViewport>(Material, Size);
	MaterialPreviewViewports.Add(Viewport);
	return SNew(SBox)
		.WidthOverride(Size)
		.HeightOverride(Size)
		[
			SNew(SViewport)
			.EnableGammaCorrection(false)
			.ViewportInterface(Viewport.ToSharedRef())
		];
}

void SHTMaterialSlotMapper::RefreshMappingMaterialPreview(TSharedPtr<FMappingRow> Mapping)
{
	if (!Mapping.IsValid() || !Mapping->MaterialPreviewBox.IsValid())
	{
		return;
	}

	const TSharedPtr<FMaterialOption> Option = FindMaterialOptionByPath(Mapping->MaterialPath);
	Mapping->MaterialPreviewBox->SetContent(MakeMaterialPreview(Option.IsValid() ? Option->Material.Get() : nullptr, 42));
}

void SHTMaterialSlotMapper::RefreshAllMaterialPreviews()
{
	MaterialPreviewViewports.Reset();
	RebuildMappingRows();
}

void SHTMaterialSlotMapper::OnSlotCheckStateChanged(const ECheckBoxState NewState, const int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	const bool bChecked = NewState == ECheckBoxState::Checked;
	const bool bShiftDown = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	if (bShiftDown && LastCheckedSlotIndex != INDEX_NONE && Slots.IsValidIndex(LastCheckedSlotIndex))
	{
		const int32 RangeStart = FMath::Min(LastCheckedSlotIndex, SlotIndex);
		const int32 RangeEnd = FMath::Max(LastCheckedSlotIndex, SlotIndex);
		for (int32 Index = RangeStart; Index <= RangeEnd; ++Index)
		{
			Slots[Index].bChecked = bChecked;
		}
	}
	else
	{
		Slots[SlotIndex].bChecked = bChecked;
	}

	LastCheckedSlotIndex = SlotIndex;
	if (SlotListBox.IsValid())
	{
		SlotListBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SHTMaterialSlotMapper::OnAutoMatchClicked()
{
	TMap<FString, UMaterialInterface*> MaterialsByName;
	for (const TSharedPtr<FMaterialOption>& Option : MaterialOptions)
	{
		if (Option.IsValid() && Option->Material.IsValid())
		{
			MaterialsByName.Add(Option->DisplayName.ToLower(), Option->Material.Get());
		}
	}

	TMap<int32, UMaterialInterface*> Assignments;
	for (const FSlotEntry& Slot : Slots)
	{
		const FString SlotName = GetSlotName(Slot);
		if (SlotName.IsEmpty())
		{
			continue;
		}
		if (UMaterialInterface** Material = MaterialsByName.Find(SlotName.ToLower()))
		{
			Assignments.Add(Slot.Index, *Material);
		}
	}

	if (Assignments.IsEmpty())
	{
		ShowStatus(LOCTEXT("NoExactMatches", "No exact slot-name/material-name matches were found."), true);
		return FReply::Handled();
	}

	FString Message;
	if (ApplySlotAssignments(Assignments, Message))
	{
		ShowStatus(FText::FromString(Message));
	}
	else
	{
		ShowStatus(FText::FromString(Message), true);
	}
	return FReply::Handled();
}

FReply SHTMaterialSlotMapper::OnAddMappingClicked()
{
	MappingRows.Add(MakeShared<FMappingRow>());
	RebuildMappingRows();
	RefreshAssignedSlotIds();
	RebuildSlotList();
	return FReply::Handled();
}

FReply SHTMaterialSlotMapper::OnRemoveMappingClicked(TSharedPtr<FMappingRow> Mapping)
{
	if (MappingRows.Num() > 1)
	{
		MappingRows.Remove(Mapping);
		RefreshAssignedSlotIds();
		RebuildMappingRows();
		RebuildSlotList();
	}
	return FReply::Handled();
}

FReply SHTMaterialSlotMapper::OnUseCheckedSlotsClicked(TSharedPtr<FMappingRow> Mapping)
{
	if (Mapping.IsValid())
	{
		Mapping->SlotIds = GetCheckedSlotIds();
		if (Mapping->SlotIdsBox.IsValid())
		{
			Mapping->SlotIdsBox->SetText(FText::FromString(Mapping->SlotIds));
		}
		for (FSlotEntry& Slot : Slots)
		{
			Slot.bChecked = false;
		}
		LastCheckedSlotIndex = INDEX_NONE;
		RefreshAssignedSlotIds();
		RebuildSlotList();
	}
	return FReply::Handled();
}

FReply SHTMaterialSlotMapper::OnClearCheckedSlotsClicked()
{
	for (FSlotEntry& Slot : Slots)
	{
		Slot.bChecked = false;
	}
	LastCheckedSlotIndex = INDEX_NONE;
	RebuildSlotList();
	return FReply::Handled();
}

FReply SHTMaterialSlotMapper::OnRefreshPreviewsClicked()
{
	RefreshAllMaterialPreviews();
	ShowStatus(LOCTEXT("PreviewsRefreshed", "Material previews refreshed."));
	return FReply::Handled();
}

FReply SHTMaterialSlotMapper::OnApplyMappingsClicked()
{
	TMap<int32, UMaterialInterface*> Assignments;
	for (const TSharedPtr<FMappingRow>& Mapping : MappingRows)
	{
		if (!Mapping.IsValid())
		{
			continue;
		}

		const TSharedPtr<FMaterialOption> MaterialOption = FindMaterialOptionByPath(Mapping->MaterialPath);
		if (!MaterialOption.IsValid() || !MaterialOption->Material.IsValid())
		{
			ShowStatus(LOCTEXT("MissingMappingMaterial", "Every mapping row needs a valid material."), true);
			return FReply::Handled();
		}

		TArray<int32> SlotIds;
		FString Error;
		const FString SlotIdsText = Mapping->SlotIdsBox.IsValid() ? HTMaterialSlotMapper::TextBoxString(Mapping->SlotIdsBox) : Mapping->SlotIds;
		if (!ParseSlotIds(SlotIdsText, SlotIds, Error))
		{
			ShowStatus(FText::FromString(Error), true);
			return FReply::Handled();
		}
		for (const int32 SlotId : SlotIds)
		{
			Assignments.Add(SlotId, MaterialOption->Material.Get());
		}
	}

	if (Assignments.IsEmpty())
	{
		ShowStatus(LOCTEXT("NoMappings", "Add at least one valid material mapping."), true);
		return FReply::Handled();
	}

	FString Message;
	if (ApplySlotAssignments(Assignments, Message))
	{
		ShowStatus(FText::FromString(Message));
	}
	else
	{
		ShowStatus(FText::FromString(Message), true);
	}
	return FReply::Handled();
}

TSharedPtr<SHTMaterialSlotMapper::FMaterialOption> SHTMaterialSlotMapper::FindMaterialOptionByPath(const FString& ObjectPath) const
{
	for (const TSharedPtr<FMaterialOption>& Option : MaterialOptions)
	{
		if (Option.IsValid() && Option->ObjectPath == ObjectPath)
		{
			return Option;
		}
	}
	return nullptr;
}

bool SHTMaterialSlotMapper::ParseSlotIds(const FString& RawValue, TArray<int32>& OutSlotIds, FString& OutError) const
{
	OutSlotIds.Reset();
	FString Normalized = RawValue;
	Normalized.TrimStartAndEndInline();
	Normalized.ReplaceInline(TEXT(";"), TEXT(","));
	Normalized.ReplaceInline(TEXT("；"), TEXT(","));
	Normalized.ReplaceInline(TEXT("，"), TEXT(","));
	Normalized.ReplaceInline(TEXT("、"), TEXT(","));
	Normalized.ReplaceInline(TEXT(" "), TEXT(","));
	Normalized.ReplaceInline(TEXT("\r"), TEXT(","));
	Normalized.ReplaceInline(TEXT("\n"), TEXT(","));
	Normalized.ReplaceInline(TEXT("\t"), TEXT(","));

	if (Normalized.IsEmpty())
	{
		OutError = TEXT("Enter one or more Slot IDs, for example 1,5,15.");
		return false;
	}

	TArray<FString> Parts;
	Normalized.ParseIntoArray(Parts, TEXT(","), true);
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}

		int32 SlotId = INDEX_NONE;
		if (!LexTryParseString(SlotId, *Part) || !Slots.IsValidIndex(SlotId))
		{
			OutError = FString::Printf(TEXT("Invalid Slot ID: %s"), *Part);
			return false;
		}
		OutSlotIds.AddUnique(SlotId);
	}

	if (OutSlotIds.IsEmpty())
	{
		OutError = TEXT("Enter at least one valid Slot ID.");
		return false;
	}
	return true;
}

bool SHTMaterialSlotMapper::ApplySlotAssignments(const TMap<int32, UMaterialInterface*>& Assignments, FString& OutMessage)
{
	USkeletalMesh* Mesh = SkeletalMesh.Get();
	if (!Mesh)
	{
		OutMessage = TEXT("The Skeletal Mesh is no longer available.");
		return false;
	}

	TArray<FSkeletalMaterial> MeshMaterials = Mesh->GetMaterials();
	TArray<FString> AppliedLines;
	Mesh->Modify();
	for (const TPair<int32, UMaterialInterface*>& Assignment : Assignments)
	{
		const int32 SlotId = Assignment.Key;
		UMaterialInterface* Material = Assignment.Value;
		if (!MeshMaterials.IsValidIndex(SlotId) || !Material)
		{
			OutMessage = FString::Printf(TEXT("Invalid material assignment for Slot %d."), SlotId);
			return false;
		}

		MeshMaterials[SlotId].MaterialInterface = Material;
		AppliedLines.Add(FString::Printf(TEXT("Slot %d (%s) -> %s"), SlotId, *GetSlotName(Slots[SlotId]), *Material->GetName()));
	}

	Mesh->SetMaterials(MeshMaterials);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	for (FSlotEntry& Slot : Slots)
	{
		if (MeshMaterials.IsValidIndex(Slot.Index))
		{
			Slot.Material = MeshMaterials[Slot.Index].MaterialInterface;
		}
	}
	RebuildSlotList();

	if (SaveAssetsCheckBox.IsValid() && SaveAssetsCheckBox->IsChecked())
	{
		TArray<UPackage*> PackagesToSave = { Mesh->GetOutermost() };
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
		{
			OutMessage = TEXT("Material slots were updated, but the Skeletal Mesh package could not be saved.");
			return false;
		}
	}

	OutMessage = FString::Printf(TEXT("Applied %d material slot assignment(s):\n%s"), Assignments.Num(), *FString::Join(AppliedLines, TEXT("\n")));
	return true;
}

void SHTMaterialSlotMapper::RefreshAssignedSlotIds()
{
	AssignedSlotIds.Reset();
	for (const TSharedPtr<FMappingRow>& Mapping : MappingRows)
	{
		if (!Mapping.IsValid())
		{
			continue;
		}

		FString Normalized = Mapping->SlotIdsBox.IsValid() ? HTMaterialSlotMapper::TextBoxString(Mapping->SlotIdsBox) : Mapping->SlotIds;
		Mapping->SlotIds = Normalized;
		Normalized.TrimStartAndEndInline();
		Normalized.ReplaceInline(TEXT(";"), TEXT(","));
		Normalized.ReplaceInline(TEXT("；"), TEXT(","));
		Normalized.ReplaceInline(TEXT("，"), TEXT(","));
		Normalized.ReplaceInline(TEXT("、"), TEXT(","));
		Normalized.ReplaceInline(TEXT(" "), TEXT(","));
		Normalized.ReplaceInline(TEXT("\r"), TEXT(","));
		Normalized.ReplaceInline(TEXT("\n"), TEXT(","));
		Normalized.ReplaceInline(TEXT("\t"), TEXT(","));

		TArray<FString> Parts;
		Normalized.ParseIntoArray(Parts, TEXT(","), true);
		for (FString Part : Parts)
		{
			Part.TrimStartAndEndInline();
			int32 SlotId = INDEX_NONE;
			if (LexTryParseString(SlotId, *Part) && Slots.IsValidIndex(SlotId))
			{
				AssignedSlotIds.Add(SlotId);
			}
		}
	}
}

bool SHTMaterialSlotMapper::IsSlotAssigned(const int32 SlotIndex) const
{
	return AssignedSlotIds.Contains(SlotIndex);
}

FString SHTMaterialSlotMapper::GetCheckedSlotIds() const
{
	TArray<FString> SlotIds;
	for (const FSlotEntry& Slot : Slots)
	{
		if (Slot.bChecked)
		{
			SlotIds.Add(FString::FromInt(Slot.Index));
		}
	}
	return FString::Join(SlotIds, TEXT(","));
}

FString SHTMaterialSlotMapper::GetSlotName(const FSlotEntry& Entry) const
{
	return Entry.SlotName.IsNone() ? FString(TEXT("None")) : Entry.SlotName.ToString();
}

void SHTMaterialSlotMapper::ShowStatus(const FText& Text, const bool bError) const
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(Text);
		StatusText->SetColorAndOpacity(bError ? FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f)) : FSlateColor::UseForeground());
	}

	FNotificationInfo Info(Text);
	Info.ExpireDuration = bError ? 7.0f : 4.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bError ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}
}

#undef LOCTEXT_NAMESPACE
