#pragma once

#include "CoreMinimal.h"
#include "HTBlueprintToggleGenerator.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class STextBlock;
class SVerticalBox;
class SWindow;
class SWidgetSwitcher;
class ISlateViewport;
class UMaterialInterface;
struct FAssetData;

class SHTBlueprintToggleToolPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHTBlueprintToggleToolPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void OpenMaterialAnalysisFromCommand();
	void OpenMaterialSlotMapperFromCommand();

private:
	FReply OnGenerateClicked();
	FReply OnOpenMaterialInstanceCreatorClicked();
	FReply OnOpenMaterialSlotMapperClicked();
	FReply OnOpenCookedAssetExporterClicked();
	FReply OnOpenSettingsClicked();
	FReply OnCloseSettingsClicked();
	FReply OnBrowseCharacterFolderClicked();
	FReply OnCreateCharacterBlueprintsClicked();

	TSharedRef<SWidget> MakeTextRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox) const;
	TSharedRef<SWidget> MakeBlueprintPickerRow(const FText& Label, bool bAnimBlueprint);
	TSharedRef<SWidget> MakeCharacterFolderPickerRow();
	TSharedRef<SWidget> MakeMaterialSlotsRow();
	TSharedRef<SWidget> MakeMaterialPickerRow();
	TSharedRef<SWidget> MakeTexturePickerRow(int32 TextureIndex);
	TSharedRef<SWidget> MakeMaterialGroupRow(int32 GroupIndex);
	void RebuildTextureRows();
	FReply OnAddTextureClicked();
	FReply OnRemoveTextureClicked(int32 TextureIndex);
	FReply OnAnalyzeMaterialsClicked();
	FReply OnSelectMaterialGroupClicked(int32 GroupIndex);

	bool ParseMaterialIDs(TArray<int32>& OutMaterialIDs, FString& OutError) const;
	bool ParseTextureMaterialSlots(TArray<int32>& OutMaterialSlots, FString& OutError) const;
	void ShowPanelError(const FText& ErrorText) const;
	FString GetAnimBlueprintPath() const;
	FString GetSaveGameBlueprintPath() const;
	void OnAnimBlueprintChanged(const FAssetData& AssetData);
	void OnSaveGameBlueprintChanged(const FAssetData& AssetData);
	void OnCharacterFolderCommitted(const FText& Text, ETextCommit::Type CommitType);
	void OnCharacterFolderPicked(const FString& NewPath);
	bool SyncBlueprintPathsFromCharacterFolder();
	void OnSourceMaterialChanged(const FAssetData& AssetData);
	void OnTextureChanged(const FAssetData& AssetData, int32 TextureIndex);
	void LoadBlueprintSettings();
	void SaveBlueprintSettings() const;
	void UpdateAssetSummaryText() const;
	bool CreateCharacterBlueprintAssets(FString& OutMessage);
	FString GetShortAssetName(const FString& ObjectPath) const;
	FString GetSourceMaterialPath() const;
	FString GetCharacterFolderPath() const;
	void OnToggleModeChanged(EHTBlueprintToggleMode NewMode);
	bool BuildMaterialSlotGroups(FString& OutMeshName, FString& OutError);
	FString InferCharacterFolderFromAnimBlueprint() const;

	struct FMaterialSlotGroup
	{
		TWeakObjectPtr<UMaterialInterface> Material;
		TArray<int32> SlotIndices;
	};

	FString AnimBlueprintPath;
	FString SaveGameBlueprintPath;
	FString CharacterFolderPath;
	FString SourceMaterialPath;
	TArray<FString> TexturePaths;
	EHTBlueprintToggleMode ToggleMode = EHTBlueprintToggleMode::MaterialSection;
	TWeakPtr<SWindow> CookedAssetExporterWindow;
	TWeakPtr<SWindow> MaterialInstanceCreatorWindow;
	TWeakPtr<SWindow> MaterialSlotMapperWindow;
	TWeakPtr<SWindow> MaterialAnalysisWindow;
	TWeakPtr<SWindow> CharacterFolderPickerWindow;
	TWeakPtr<SWindow> SettingsWindow;
	TArray<FMaterialSlotGroup> MaterialSlotGroups;
	TArray<TSharedPtr<ISlateViewport>> MaterialGroupRenderedThumbnails;
	TSharedPtr<SEditableTextBox> ToggleVariableBox;
	TSharedPtr<SEditableTextBox> KeyNameBox;
	TSharedPtr<SEditableTextBox> MaterialIDsBox;
	TSharedPtr<SEditableTextBox> MaterialSlotsBox;
	TSharedPtr<SEditableTextBox> CharacterFolderBox;
	TSharedPtr<SEditableTextBox> TextureParameterBox;
	TSharedPtr<SWidgetSwitcher> ModeOptionsSwitcher;
	TSharedPtr<SVerticalBox> TextureRowsBox;
	TSharedPtr<SCheckBox> InitGraphCheckBox;
	TSharedPtr<SCheckBox> UpdateGraphCheckBox;
	TSharedPtr<SCheckBox> SaveAssetsCheckBox;
	TSharedPtr<STextBlock> AssetSummaryText;
	TSharedPtr<STextBlock> StatusText;
};
