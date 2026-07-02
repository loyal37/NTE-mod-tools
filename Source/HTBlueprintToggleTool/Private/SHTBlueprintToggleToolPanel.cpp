#include "SHTBlueprintToggleToolPanel.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_Root.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/BlueprintFactory.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/SaveGame.h"
#include "HTBlueprintToggleGenerator.h"
#include "IContentBrowserSingleton.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/ConfigCacheIni.h"
#include "ObjectTools.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/RenderingCommon.h"
#include "RenderingThread.h"
#include "ScopedTransaction.h"
#include "SHTCookedAssetExporter.h"
#include "SHTMaterialInstanceCreator.h"
#include "SHTMaterialSlotMapper.h"
#include "Styling/AppStyle.h"
#include "Slate/SlateTextures.h"
#include "Textures/SlateTextureData.h"
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
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "SHTBlueprintToggleToolPanel"

namespace HTTogglePanel
{
	static const TCHAR* SettingsSection = TEXT("HTBlueprintToggleTool.BlueprintSettings");
	static const TCHAR* AnimBlueprintKey = TEXT("AnimBlueprintPath");
	static const TCHAR* SaveGameBlueprintKey = TEXT("SaveGameBlueprintPath");
	static const TCHAR* CharacterFolderKey = TEXT("CharacterFolderPath");

	static FString TextBoxString(const TSharedPtr<SEditableTextBox>& TextBox)
	{
		return TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
	}

	static bool IsChecked(const TSharedPtr<SCheckBox>& CheckBox)
	{
		return CheckBox.IsValid() && CheckBox->IsChecked();
	}

	static FString JoinSlotIndices(const TArray<int32>& SlotIndices, const FString& Separator = TEXT(","))
	{
		TArray<FString> Values;
		Values.Reserve(SlotIndices.Num());
		for (const int32 SlotIndex : SlotIndices)
		{
			Values.Add(FString::FromInt(SlotIndex));
		}
		return FString::Join(Values, *Separator);
	}

	static FString NormalizeObjectPath(FString Path)
	{
		Path.TrimStartAndEndInline();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!Path.IsEmpty() && !Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
		}
		return Path;
	}

	static FString MakeObjectPath(const FString& PackagePath, const FString& AssetName)
	{
		return PackagePath / AssetName + TEXT(".") + AssetName;
	}

	static bool IsDigitsOnly(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return false;
		}

		for (const TCHAR Character : Value)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}
		return true;
	}

	static FString MakeCharacterAssetBaseName(const FString& CharacterFolder)
	{
		FString FolderName = FPackageName::GetLongPackageAssetName(CharacterFolder);
		int32 UnderscoreIndex = INDEX_NONE;
		if (FolderName.FindChar(TEXT('_'), UnderscoreIndex))
		{
			const FString Prefix = FolderName.Left(UnderscoreIndex);
			if (IsDigitsOnly(Prefix) && UnderscoreIndex + 1 < FolderName.Len())
			{
				FolderName = FolderName.Mid(UnderscoreIndex + 1);
			}
		}

		return ObjectTools::SanitizeObjectName(FolderName);
	}

	static FString MakeCharacterScopedSlotName(const FString& ToggleVariableName, const FString& CharacterFolder)
	{
		FString SlotName = ToggleVariableName;
		SlotName.TrimStartAndEndInline();

		const FString CharacterName = MakeCharacterAssetBaseName(CharacterFolder);
		if (!SlotName.IsEmpty() && !CharacterName.IsEmpty())
		{
			SlotName += TEXT(" ");
			SlotName += CharacterName;
		}
		return SlotName;
	}

	template <typename ObjectType>
	static ObjectType* LoadAsset(const FString& RawPath)
	{
		return LoadObject<ObjectType>(nullptr, *NormalizeObjectPath(RawPath));
	}

	static void ShowNotification(const FText& Text, SNotificationItem::ECompletionState CompletionState, float ExpireDuration = 5.0f)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = ExpireDuration;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(CompletionState);
		}
	}

	static USkeletalMesh* FindSkeletalMeshInCharacterFolder(const FString& CharacterFolder, FString& OutError)
	{
		if (!FPackageName::IsValidLongPackageName(CharacterFolder))
		{
			OutError = TEXT("Character Folder must be a valid /Game path.");
			return nullptr;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*CharacterFolder), Assets, true);

		USkeletalMesh* FirstMesh = nullptr;
		for (const FAssetData& AssetData : Assets)
		{
			USkeletalMesh* Mesh = Cast<USkeletalMesh>(AssetData.GetAsset());
			if (!Mesh)
			{
				continue;
			}

			if (!FirstMesh)
			{
				FirstMesh = Mesh;
			}

			if (AssetData.AssetName.ToString().Contains(TEXT("_skin")))
			{
				return Mesh;
			}
		}

		if (!FirstMesh)
		{
			OutError = FString::Printf(TEXT("No Skeletal Mesh was found under %s."), *CharacterFolder);
		}
		return FirstMesh;
	}

	static UEdGraph* FindBlueprintGraph(UBlueprint* Blueprint, const FName GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		auto FindInGraphs = [GraphName](const TArray<TObjectPtr<UEdGraph>>& Graphs) -> UEdGraph*
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph && Graph->GetFName() == GraphName)
				{
					return Graph;
				}
			}
			return nullptr;
		};

		if (UEdGraph* Graph = FindInGraphs(Blueprint->FunctionGraphs))
		{
			return Graph;
		}
		if (UEdGraph* Graph = FindInGraphs(Blueprint->UbergraphPages))
		{
			return Graph;
		}
		if (UEdGraph* Graph = FindInGraphs(Blueprint->MacroGraphs))
		{
			return Graph;
		}
		if (UEdGraph* Graph = FindInGraphs(Blueprint->DelegateSignatureGraphs))
		{
			return Graph;
		}
		return nullptr;
	}

	static UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bOrphanedPin && Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static bool EnsureInputPoseGraph(UAnimBlueprint* AnimBlueprint, FString& OutError)
	{
		UEdGraph* AnimGraph = FindBlueprintGraph(AnimBlueprint, TEXT("AnimGraph"));
		if (!AnimGraph)
		{
			OutError = TEXT("The created Anim Blueprint does not contain an AnimGraph.");
			return false;
		}

		TArray<UAnimGraphNode_Root*> RootNodes;
		AnimGraph->GetNodesOfClass(RootNodes);

		UAnimGraphNode_Root* RootNode = RootNodes.Num() > 0 ? RootNodes[0] : nullptr;
		if (!RootNode)
		{
			FGraphNodeCreator<UAnimGraphNode_Root> RootCreator(*AnimGraph);
			RootNode = RootCreator.CreateNode();
			RootNode->NodePosX = 300;
			RootNode->NodePosY = 0;
			RootCreator.Finalize();
		}

		TArray<UAnimGraphNode_LinkedInputPose*> InputPoseNodes;
		AnimGraph->GetNodesOfClass(InputPoseNodes);

		UAnimGraphNode_LinkedInputPose* InputPoseNode = InputPoseNodes.Num() > 0 ? InputPoseNodes[0] : nullptr;
		if (!InputPoseNode)
		{
			FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> InputCreator(*AnimGraph);
			InputPoseNode = InputCreator.CreateNode();
			InputPoseNode->NodePosX = RootNode->NodePosX - 360;
			InputPoseNode->NodePosY = RootNode->NodePosY;
			InputCreator.Finalize();
			InputPoseNode->ReconstructNode();
		}

		UEdGraphPin* InputPosePin = FindPosePin(InputPoseNode, EGPD_Output);
		UEdGraphPin* RootPosePin = FindPosePin(RootNode, EGPD_Input);
		if (!InputPosePin || !RootPosePin)
		{
			OutError = TEXT("Could not find the Input Pose or Output Pose pin in AnimGraph.");
			return false;
		}

		RootPosePin->BreakAllPinLinks();
		const UEdGraphSchema* Schema = AnimGraph->GetSchema();
		if (!Schema || (!Schema->TryCreateConnection(InputPosePin, RootPosePin) && !Schema->TryCreateConnection(RootPosePin, InputPosePin)))
		{
			OutError = TEXT("Could not connect Input Pose to Output Pose.");
			return false;
		}

		AnimGraph->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		return true;
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

void SHTBlueprintToggleToolPanel::Construct(const FArguments& InArgs)
{
	AnimBlueprintPath = TEXT("/Game/Characters/Player/010_nanally/nanally_animbp.nanally_animbp");
	SaveGameBlueprintPath = TEXT("/Game/Characters/Player/010_nanally/nanally_saved_bp.nanally_saved_bp");
	CharacterFolderPath.Empty();
	LoadBlueprintSettings();
	if (CharacterFolderPath.IsEmpty())
	{
		CharacterFolderPath = InferCharacterFolderFromAnimBlueprint();
		SaveBlueprintSettings();
	}
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
					.Text(LOCTEXT("Subtitle", "Generate material visibility or multi-slot, multi-texture toggle nodes. Save Variable is derived from Anim Variable; Save Slot also includes the character name."))
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
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8, 0, 0, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("MaterialSlotMapperTooltip", "Assign materials to Skeletal Mesh material slots by exact matching or batch mappings."))
						.OnClicked(this, &SHTBlueprintToggleToolPanel::OnOpenMaterialSlotMapperClicked)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SImage).Image(FAppStyle::GetBrush("ClassIcon.SkeletalMesh"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(LOCTEXT("MaterialSlotMapperButton", "Slot Materials"))
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
						.ToolTipText(LOCTEXT("ToggleVarTooltip", "Save Variable will be AnimVariable + Save. Save Slot will be AnimVariable + character name.")))
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
					]

					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							MakeMaterialSlotsRow()
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

void SHTBlueprintToggleToolPanel::OpenMaterialAnalysisFromCommand()
{
	OnToggleModeChanged(EHTBlueprintToggleMode::Texture);
	OnAnalyzeMaterialsClicked();
}

void SHTBlueprintToggleToolPanel::OpenMaterialSlotMapperFromCommand()
{
	OnOpenMaterialSlotMapperClicked();
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

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeCharacterFolderPickerRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(LOCTEXT("CharacterFolder", "Character Folder"))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SAssignNew(CharacterFolderBox, SEditableTextBox)
			.Text_Lambda([this]() { return FText::FromString(CharacterFolderPath); })
			.HintText(LOCTEXT("CharacterFolderHint", "/Game/Characters/Player/004_lacrimosa"))
			.OnTextCommitted(this, &SHTBlueprintToggleToolPanel::OnCharacterFolderCommitted)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6, 0, 0, 0)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("BrowseCharacterFolderTooltip", "Choose the character folder from the current project."))
			.OnClicked(this, &SHTBlueprintToggleToolPanel::OnBrowseCharacterFolderClicked)
			[
				SNew(SImage).Image(FAppStyle::GetBrush("Icons.FolderOpen"))
			]
		];
}

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeMaterialSlotsRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150)
			[
				SNew(STextBlock).Text(LOCTEXT("MaterialElementIndices", "Material Slot(s)"))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SAssignNew(MaterialSlotsBox, SEditableTextBox)
			.Text(FText::FromString(TEXT("0")))
			.HintText(LOCTEXT("MaterialElementIndicesHint", "Single: 12    Multiple: 12,13"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6, 0, 0, 0)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("AnalyzeMaterialsTooltip", "Group the selected Anim Blueprint preview mesh slots by material."))
			.OnClicked(this, &SHTBlueprintToggleToolPanel::OnAnalyzeMaterialsClicked)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Search"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("AnalyzeMaterials", "Analyze"))
				]
			]
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

bool SHTBlueprintToggleToolPanel::BuildMaterialSlotGroups(FString& OutMeshName, FString& OutError)
{
	MaterialSlotGroups.Reset();

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		OutError = TEXT("The selected Anim Blueprint could not be loaded.");
		return false;
	}

	USkeletalMesh* SkeletalMesh = AnimBlueprint->GetPreviewMesh(false);
	if (!SkeletalMesh && AnimBlueprint->TargetSkeleton)
	{
		SkeletalMesh = AnimBlueprint->TargetSkeleton->GetPreviewMesh(true);
	}
	if (!SkeletalMesh)
	{
		OutError = TEXT("The selected Anim Blueprint has no preview Skeletal Mesh. Set a Preview Mesh in the Animation Blueprint editor first.");
		return false;
	}

	OutMeshName = SkeletalMesh->GetName();
	TMap<UMaterialInterface*, int32> GroupByMaterial;
	const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
	for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
	{
		UMaterialInterface* Material = Materials[SlotIndex].MaterialInterface;
		if (!Material)
		{
			continue;
		}

		int32* ExistingGroupIndex = GroupByMaterial.Find(Material);
		if (!ExistingGroupIndex)
		{
			FMaterialSlotGroup& NewGroup = MaterialSlotGroups.AddDefaulted_GetRef();
			NewGroup.Material = Material;
			const int32 NewGroupIndex = MaterialSlotGroups.Num() - 1;
			GroupByMaterial.Add(Material, NewGroupIndex);
			ExistingGroupIndex = GroupByMaterial.Find(Material);
		}
		MaterialSlotGroups[*ExistingGroupIndex].SlotIndices.Add(SlotIndex);
	}

	if (MaterialSlotGroups.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Skeletal Mesh %s has no assigned materials."), *OutMeshName);
		return false;
	}
	return true;
}

TSharedRef<SWidget> SHTBlueprintToggleToolPanel::MakeMaterialGroupRow(const int32 GroupIndex)
{
	const FMaterialSlotGroup& Group = MaterialSlotGroups[GroupIndex];
	UMaterialInterface* Material = Group.Material.Get();
	const FString MaterialName = GetNameSafe(Material);
	const FString MaterialPath = Material ? Material->GetPathName() : FString(TEXT("None"));
	const FString SlotList = HTTogglePanel::JoinSlotIndices(Group.SlotIndices, TEXT(", "));
	constexpr uint32 ThumbnailSize = 64;
	TSharedPtr<ISlateViewport> ThumbnailViewport = MakeShared<HTTogglePanel::FRenderedMaterialThumbnailViewport>(Material, ThumbnailSize);
	MaterialGroupRenderedThumbnails.Add(ThumbnailViewport);

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ToolTipText(FText::Format(LOCTEXT("SelectMaterialGroupTooltip", "Use slots {0} and material {1}."), FText::FromString(SlotList), FText::FromString(MaterialName)))
		.OnClicked(this, &SHTBlueprintToggleToolPanel::OnSelectMaterialGroupClicked, GroupIndex)
		.ContentPadding(FMargin(8, 5))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 10, 0)
			[
				SNew(SBox)
				.WidthOverride(64)
				.HeightOverride(64)
				[
					SNew(SViewport)
					.EnableGammaCorrection(true)
					.IgnoreTextureAlpha(false)
					.ViewportSize(FVector2D(ThumbnailSize, ThumbnailSize))
					.ViewportInterface(ThumbnailViewport)
				]
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
					.Text(FText::FromString(MaterialName))
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("MaterialGroupSlots", "Slots: {0}"), FText::FromString(SlotList)))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(MaterialPath))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		];
}

FReply SHTBlueprintToggleToolPanel::OnAnalyzeMaterialsClicked()
{
	FString MeshName;
	FString Error;
	if (!BuildMaterialSlotGroups(MeshName, Error))
	{
		ShowPanelError(FText::FromString(Error));
		return FReply::Handled();
	}

	MaterialGroupRenderedThumbnails.Reset();
	TSharedRef<SVerticalBox> GroupList = SNew(SVerticalBox);
	for (int32 GroupIndex = 0; GroupIndex < MaterialSlotGroups.Num(); ++GroupIndex)
	{
		GroupList->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 3)
		[
			MakeMaterialGroupRow(GroupIndex)
		];
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("MaterialAnalysisTitle", "HT Material Slot Analysis"))
		.ClientSize(FVector2D(760.0f, 620.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::UserSized);
	MaterialAnalysisWindow = Window;
	Window->SetContent(
		SNew(SBorder)
		.Padding(14)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("AnalyzedMesh", "Skeletal Mesh: {0}"), FText::FromString(MeshName)))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("MaterialGroupCount", "{0} material groups. Select one to fill Material Slot(s) and Source Material."), FText::AsNumber(MaterialSlotGroups.Num())))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					GroupList
				]
			]
		]);

	FSlateApplication::Get().AddModalWindow(Window, SharedThis(this));
	MaterialAnalysisWindow.Reset();
	MaterialGroupRenderedThumbnails.Reset();
	return FReply::Handled();
}

FReply SHTBlueprintToggleToolPanel::OnSelectMaterialGroupClicked(const int32 GroupIndex)
{
	if (!MaterialSlotGroups.IsValidIndex(GroupIndex) || !MaterialSlotsBox.IsValid())
	{
		return FReply::Handled();
	}

	const FMaterialSlotGroup& Group = MaterialSlotGroups[GroupIndex];
	UMaterialInterface* Material = Group.Material.Get();
	if (!Material)
	{
		ShowPanelError(LOCTEXT("MaterialGroupMissing", "The selected material is no longer available."));
		return FReply::Handled();
	}

	const FString SlotList = HTTogglePanel::JoinSlotIndices(Group.SlotIndices);
	MaterialSlotsBox->SetText(FText::FromString(SlotList));
	SourceMaterialPath = Material->GetPathName();
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("MaterialGroupSelected", "Selected material {0}; slots: {1}"),
			FText::FromString(Material->GetName()),
			FText::FromString(SlotList)));
	}
	if (MaterialAnalysisWindow.IsValid())
	{
		MaterialAnalysisWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
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

FReply SHTBlueprintToggleToolPanel::OnOpenMaterialSlotMapperClicked()
{
	if (MaterialSlotMapperWindow.IsValid())
	{
		MaterialSlotMapperWindow.Pin()->BringToFront();
		return FReply::Handled();
	}

	if (CharacterFolderPath.IsEmpty())
	{
		CharacterFolderPath = InferCharacterFolderFromAnimBlueprint();
		SaveBlueprintSettings();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("MaterialSlotMapperTitle", "HT Material Slot Mapper"))
		.ClientSize(FVector2D(1180.0f, 720.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::UserSized);

	MaterialSlotMapperWindow = Window;
	Window->SetContent(
		SNew(SHTMaterialSlotMapper)
		.AnimBlueprintPath(AnimBlueprintPath)
		.CharacterFolderPath(CharacterFolderPath));
	FSlateApplication::Get().AddWindow(Window);
	const FSlateRect WorkArea = FSlateApplication::Get().GetPreferredWorkArea();
	const FVector2D DesiredWindowSize(1180.0f, 720.0f);
	const float WorkAreaWidth = WorkArea.Right - WorkArea.Left;
	const float WorkAreaHeight = WorkArea.Bottom - WorkArea.Top;
	const float WindowX = WorkArea.Left + FMath::Max(0.0f, (WorkAreaWidth - DesiredWindowSize.X) * 0.5f);
	const float WindowY = WorkArea.Top + FMath::Max(0.0f, (WorkAreaHeight - DesiredWindowSize.Y) * 0.5f);
	Window->MoveWindowTo(FVector2D(WindowX, WindowY));
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
		.ClientSize(FVector2D(760.0f, 340.0f))
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
			.Padding(0, 0, 0, 8)
			[
				MakeBlueprintPickerRow(LOCTEXT("SaveBP", "SaveGame Blueprint"), false)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 10)
			[
				MakeCharacterFolderPickerRow()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 14)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(150)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreateBlueprintsLabel", "Create Blueprints"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateCharacterBlueprints", "Create AnimBP + SaveGame and bind Skeletal Mesh"))
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("CreateCharacterBlueprintsTooltip", "Creates character AnimBP and SaveGame Blueprint in Character Folder, connects Input Pose to Output Pose, assigns the AnimBP as the Skeletal Mesh post-process Anim Blueprint, and fills the paths above."))
					.OnClicked(this, &SHTBlueprintToggleToolPanel::OnCreateCharacterBlueprintsClicked)
				]
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

FReply SHTBlueprintToggleToolPanel::OnBrowseCharacterFolderClicked()
{
	if (CharacterFolderPickerWindow.IsValid())
	{
		CharacterFolderPickerWindow.Pin()->BringToFront();
		return FReply::Handled();
	}

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.DefaultPath = CharacterFolderPath.IsEmpty() ? InferCharacterFolderFromAnimBlueprint() : CharacterFolderPath;
	PathPickerConfig.bAllowContextMenu = false;
	PathPickerConfig.bShowFavorites = false;
	PathPickerConfig.bShowViewOptions = true;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateSP(this, &SHTBlueprintToggleToolPanel::OnCharacterFolderPicked);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("CharacterFolderPickerTitle", "Select Character Folder"))
		.ClientSize(FVector2D(420.0f, 500.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::UserSized);

	CharacterFolderPickerWindow = Window;
	Window->SetContent(
		SNew(SBorder)
		.Padding(8)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig)
		]);
	if (SettingsWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, SettingsWindow.Pin().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
	return FReply::Handled();
}

void SHTBlueprintToggleToolPanel::OnCharacterFolderCommitted(const FText& Text, ETextCommit::Type)
{
	CharacterFolderPath = Text.ToString().TrimStartAndEnd();
	CharacterFolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	SyncBlueprintPathsFromCharacterFolder();
	SaveBlueprintSettings();
	UpdateAssetSummaryText();
}

void SHTBlueprintToggleToolPanel::OnCharacterFolderPicked(const FString& NewPath)
{
	CharacterFolderPath = NewPath;
	CharacterFolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	SyncBlueprintPathsFromCharacterFolder();
	if (CharacterFolderBox.IsValid())
	{
		CharacterFolderBox->SetText(FText::FromString(CharacterFolderPath));
	}
	SaveBlueprintSettings();
	UpdateAssetSummaryText();
	if (CharacterFolderPickerWindow.IsValid())
	{
		CharacterFolderPickerWindow.Pin()->RequestDestroyWindow();
		CharacterFolderPickerWindow.Reset();
	}
}

FReply SHTBlueprintToggleToolPanel::OnCreateCharacterBlueprintsClicked()
{
	FString Message;
	if (!CreateCharacterBlueprintAssets(Message))
	{
		ShowPanelError(FText::FromString(Message));
		return FReply::Handled();
	}

	if (CharacterFolderBox.IsValid())
	{
		CharacterFolderBox->SetText(FText::FromString(CharacterFolderPath));
	}

	UpdateAssetSummaryText();
	HTTogglePanel::ShowNotification(FText::FromString(Message), SNotificationItem::CS_Success);
	return FReply::Handled();
}

bool SHTBlueprintToggleToolPanel::CreateCharacterBlueprintAssets(FString& OutMessage)
{
	using namespace HTTogglePanel;

	if (CharacterFolderPath.IsEmpty())
	{
		CharacterFolderPath = InferCharacterFolderFromAnimBlueprint();
	}
	CharacterFolderPath.TrimStartAndEndInline();
	CharacterFolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	if (!FPackageName::IsValidLongPackageName(CharacterFolderPath))
	{
		OutMessage = TEXT("Please choose a valid Character Folder under /Game.");
		return false;
	}

	FString MeshError;
	USkeletalMesh* SkeletalMesh = FindSkeletalMeshInCharacterFolder(CharacterFolderPath, MeshError);
	if (!SkeletalMesh)
	{
		OutMessage = MeshError;
		return false;
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		OutMessage = FString::Printf(TEXT("Skeletal Mesh %s does not have a Skeleton."), *SkeletalMesh->GetName());
		return false;
	}

	const FString BaseName = MakeCharacterAssetBaseName(CharacterFolderPath);
	if (BaseName.IsEmpty())
	{
		OutMessage = TEXT("Could not derive a character name from Character Folder.");
		return false;
	}

	const FString AnimAssetName = BaseName + TEXT("_animbp");
	const FString SaveAssetName = BaseName + TEXT("_save");
	const FString AnimObjectPath = MakeObjectPath(CharacterFolderPath, AnimAssetName);
	const FString SaveObjectPath = MakeObjectPath(CharacterFolderPath, SaveAssetName);

	FScopedTransaction Transaction(LOCTEXT("CreateCharacterBlueprintAssetsTransaction", "Create character Blueprints"));
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	UAnimBlueprint* AnimBlueprint = LoadAsset<UAnimBlueprint>(AnimObjectPath);
	if (!AnimBlueprint)
	{
		if (UObject* ExistingObject = LoadAsset<UObject>(AnimObjectPath))
		{
			OutMessage = FString::Printf(TEXT("Asset %s already exists but is not an Anim Blueprint."), *AnimObjectPath);
			return false;
		}

		UAnimBlueprintFactory* AnimFactory = NewObject<UAnimBlueprintFactory>();
		AnimFactory->BlueprintType = BPTYPE_Normal;
		AnimFactory->ParentClass = UAnimInstance::StaticClass();
		AnimFactory->TargetSkeleton = Skeleton;
		AnimFactory->PreviewSkeletalMesh = SkeletalMesh;
		AnimFactory->bTemplate = false;

		AnimBlueprint = Cast<UAnimBlueprint>(AssetToolsModule.Get().CreateAsset(AnimAssetName, CharacterFolderPath, UAnimBlueprint::StaticClass(), AnimFactory));
		if (!AnimBlueprint)
		{
			OutMessage = FString::Printf(TEXT("Failed to create Anim Blueprint: %s."), *AnimObjectPath);
			return false;
		}
	}

	UBlueprint* SaveBlueprint = LoadAsset<UBlueprint>(SaveObjectPath);
	if (!SaveBlueprint)
	{
		if (UObject* ExistingObject = LoadAsset<UObject>(SaveObjectPath))
		{
			OutMessage = FString::Printf(TEXT("Asset %s already exists but is not a Blueprint."), *SaveObjectPath);
			return false;
		}

		UBlueprintFactory* SaveFactory = NewObject<UBlueprintFactory>();
		SaveFactory->ParentClass = USaveGame::StaticClass();
		SaveFactory->BlueprintType = BPTYPE_Normal;
		SaveFactory->bSkipClassPicker = true;

		SaveBlueprint = Cast<UBlueprint>(AssetToolsModule.Get().CreateAsset(SaveAssetName, CharacterFolderPath, UBlueprint::StaticClass(), SaveFactory));
		if (!SaveBlueprint)
		{
			OutMessage = FString::Printf(TEXT("Failed to create SaveGame Blueprint: %s."), *SaveObjectPath);
			return false;
		}
	}

	AnimBlueprint->Modify();
	AnimBlueprint->TargetSkeleton = Skeleton;
	AnimBlueprint->SetPreviewMesh(SkeletalMesh, true);

	FString GraphError;
	if (!EnsureInputPoseGraph(AnimBlueprint, GraphError))
	{
		OutMessage = GraphError;
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (SaveBlueprint->Status == BS_Error)
	{
		OutMessage = TEXT("The created SaveGame Blueprint has compile errors.");
		return false;
	}
	if (AnimBlueprint->Status == BS_Error)
	{
		OutMessage = TEXT("The created Anim Blueprint has compile errors.");
		return false;
	}
	if (!AnimBlueprint->GeneratedClass || !AnimBlueprint->GeneratedClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		OutMessage = TEXT("The created Anim Blueprint does not have a valid generated AnimInstance class.");
		return false;
	}

	SkeletalMesh->Modify();
	SkeletalMesh->SetPostProcessAnimBlueprint(AnimBlueprint->GeneratedClass.Get());
	SkeletalMesh->MarkPackageDirty();

	AnimBlueprintPath = AnimBlueprint->GetPathName();
	SaveGameBlueprintPath = SaveBlueprint->GetPathName();
	SaveBlueprintSettings();

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.AddUnique(AnimBlueprint->GetOutermost());
	PackagesToSave.AddUnique(SaveBlueprint->GetOutermost());
	PackagesToSave.AddUnique(SkeletalMesh->GetOutermost());
	if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
	{
		OutMessage = TEXT("Blueprints were created, but one or more assets could not be saved.");
		return false;
	}

	OutMessage = FString::Printf(
		TEXT("Created/updated %s and %s, then assigned %s as the Skeletal Mesh post-process Anim Blueprint."),
		*AnimAssetName,
		*SaveAssetName,
		*AnimAssetName);
	return true;
}

FString SHTBlueprintToggleToolPanel::GetAnimBlueprintPath() const
{
	return AnimBlueprintPath;
}

FString SHTBlueprintToggleToolPanel::GetSaveGameBlueprintPath() const
{
	return SaveGameBlueprintPath;
}

FString SHTBlueprintToggleToolPanel::GetCharacterFolderPath() const
{
	return CharacterFolderPath;
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
		if (CharacterFolderPath.IsEmpty())
		{
			CharacterFolderPath = InferCharacterFolderFromAnimBlueprint();
		}
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
	if (GConfig->GetString(HTTogglePanel::SettingsSection, HTTogglePanel::CharacterFolderKey, SavedPath, GEditorPerProjectIni) && !SavedPath.IsEmpty())
	{
		CharacterFolderPath = MoveTemp(SavedPath);
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
	GConfig->SetString(HTTogglePanel::SettingsSection, HTTogglePanel::CharacterFolderKey, *CharacterFolderPath, GEditorPerProjectIni);
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
			LOCTEXT("AssetSummary", "AnimBP: {0}    SaveGame: {1}    Character Folder: {2}"),
			FText::FromString(GetShortAssetName(AnimBlueprintPath)),
			FText::FromString(GetShortAssetName(SaveGameBlueprintPath)),
			FText::FromString(CharacterFolderPath)));
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

FString SHTBlueprintToggleToolPanel::InferCharacterFolderFromAnimBlueprint() const
{
	FString PackageName = AnimBlueprintPath;
	const int32 DotIndex = PackageName.Find(TEXT("."));
	if (DotIndex != INDEX_NONE)
	{
		PackageName.LeftInline(DotIndex);
	}
	if (FPackageName::IsValidLongPackageName(PackageName))
	{
		return FPackageName::GetLongPackagePath(PackageName);
	}
	return TEXT("/Game/Characters/Player");
}

bool SHTBlueprintToggleToolPanel::SyncBlueprintPathsFromCharacterFolder()
{
	CharacterFolderPath.TrimStartAndEndInline();
	CharacterFolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (!FPackageName::IsValidLongPackageName(CharacterFolderPath))
	{
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*CharacterFolderPath), Assets, true);

	const FString BaseName = HTTogglePanel::MakeCharacterAssetBaseName(CharacterFolderPath).ToLower();
	FString BestAnimPath;
	FString BestSavePath;
	int32 BestAnimScore = MIN_int32;
	int32 BestSaveScore = MIN_int32;
	const TArray<FString> AnimKeywords = { TEXT("animbp"), TEXT("animblueprint"), TEXT("anim_blueprint") };
	const TArray<FString> SaveKeywords = { TEXT("savegame"), TEXT("saved"), TEXT("save") };

	auto ScoreAssetName = [&BaseName, this](const FAssetData& AssetData, const TArray<FString>& Keywords)
	{
		const FString LowerName = AssetData.AssetName.ToString().ToLower();
		int32 Score = AssetData.PackagePath.ToString() == CharacterFolderPath ? 100 : 0;
		if (!BaseName.IsEmpty() && LowerName.Contains(BaseName))
		{
			Score += 30;
		}
		for (const FString& Keyword : Keywords)
		{
			if (LowerName.Contains(Keyword))
			{
				Score += 20;
			}
		}
		if (LowerName == BaseName || LowerName.StartsWith(BaseName + TEXT("_")))
		{
			Score += 10;
		}
		return Score;
	};

	for (const FAssetData& AssetData : Assets)
	{
		if (AssetData.AssetClassPath == UAnimBlueprint::StaticClass()->GetClassPathName())
		{
			const int32 Score = ScoreAssetName(AssetData, AnimKeywords);
			if (Score > BestAnimScore)
			{
				BestAnimScore = Score;
				BestAnimPath = AssetData.GetSoftObjectPath().ToString();
			}
			continue;
		}

		if (AssetData.AssetClassPath != UBlueprint::StaticClass()->GetClassPathName())
		{
			continue;
		}

		const FString LowerName = AssetData.AssetName.ToString().ToLower();
		bool bIsSaveGameBlueprint = LowerName.Contains(TEXT("save"));
		if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
		{
			bIsSaveGameBlueprint =
				bIsSaveGameBlueprint ||
				(Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(USaveGame::StaticClass())) ||
				(Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(USaveGame::StaticClass()));
		}

		if (bIsSaveGameBlueprint)
		{
			const int32 Score = ScoreAssetName(AssetData, SaveKeywords);
			if (Score > BestSaveScore)
			{
				BestSaveScore = Score;
				BestSavePath = AssetData.GetSoftObjectPath().ToString();
			}
		}
	}

	bool bChanged = false;
	if (!BestAnimPath.IsEmpty() && AnimBlueprintPath != BestAnimPath)
	{
		AnimBlueprintPath = MoveTemp(BestAnimPath);
		bChanged = true;
	}
	if (!BestSavePath.IsEmpty() && SaveGameBlueprintPath != BestSavePath)
	{
		SaveGameBlueprintPath = MoveTemp(BestSavePath);
		bChanged = true;
	}
	return bChanged;
}

bool SHTBlueprintToggleToolPanel::ParseMaterialIDs(TArray<int32>& OutMaterialIDs, FString& OutError) const
{
	OutMaterialIDs.Reset();

	FString RawValue = HTTogglePanel::TextBoxString(MaterialIDsBox);
	RawValue.TrimStartAndEndInline();
	RawValue.ReplaceInline(TEXT("，"), TEXT(","));
	RawValue.ReplaceInline(TEXT(";"), TEXT(","));
	RawValue.ReplaceInline(TEXT("；"), TEXT(","));
	RawValue.ReplaceInline(TEXT(" "), TEXT(","));
	RawValue.ReplaceInline(TEXT("\r"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\n"), TEXT(","));
	RawValue.ReplaceInline(TEXT("\t"), TEXT(","));

	if (RawValue.IsEmpty())
	{
		OutError = TEXT("Enter at least one Material ID. Single example: 16; multiple example: 13,20.");
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
			OutError = FString::Printf(TEXT("Invalid Material ID: %s"), *Part);
			return false;
		}

		OutMaterialIDs.AddUnique(MaterialID);
	}

	if (OutMaterialIDs.Num() == 0)
	{
		OutError = TEXT("Enter at least one valid Material ID.");
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
	FString EffectiveCharacterFolder = CharacterFolderPath.IsEmpty() ? InferCharacterFolderFromAnimBlueprint() : CharacterFolderPath;
	EffectiveCharacterFolder.TrimStartAndEndInline();
	EffectiveCharacterFolder.ReplaceInline(TEXT("\\"), TEXT("/"));
	Params.SlotName = MakeCharacterScopedSlotName(ToggleVariableName, EffectiveCharacterFolder);
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
