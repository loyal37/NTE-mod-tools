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
struct FAssetData;

class SHTBlueprintToggleToolPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHTBlueprintToggleToolPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnGenerateClicked();
	FReply OnOpenMaterialInstanceCreatorClicked();
	FReply OnOpenCookedAssetExporterClicked();
	FReply OnOpenSettingsClicked();
	FReply OnCloseSettingsClicked();

	TSharedRef<SWidget> MakeTextRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox) const;
	TSharedRef<SWidget> MakeBlueprintPickerRow(const FText& Label, bool bAnimBlueprint);
	TSharedRef<SWidget> MakeMaterialPickerRow();
	TSharedRef<SWidget> MakeTexturePickerRow(int32 TextureIndex);
	void RebuildTextureRows();
	FReply OnAddTextureClicked();
	FReply OnRemoveTextureClicked(int32 TextureIndex);

	bool ParseMaterialIDs(TArray<int32>& OutMaterialIDs, FString& OutError) const;
	bool ParseTextureMaterialSlots(TArray<int32>& OutMaterialSlots, FString& OutError) const;
	void ShowPanelError(const FText& ErrorText) const;
	FString GetAnimBlueprintPath() const;
	FString GetSaveGameBlueprintPath() const;
	void OnAnimBlueprintChanged(const FAssetData& AssetData);
	void OnSaveGameBlueprintChanged(const FAssetData& AssetData);
	void OnSourceMaterialChanged(const FAssetData& AssetData);
	void OnTextureChanged(const FAssetData& AssetData, int32 TextureIndex);
	void UpdateAssetSummaryText() const;
	FString GetShortAssetName(const FString& ObjectPath) const;
	FString GetSourceMaterialPath() const;
	void OnToggleModeChanged(EHTBlueprintToggleMode NewMode);

	FString AnimBlueprintPath;
	FString SaveGameBlueprintPath;
	FString SourceMaterialPath;
	TArray<FString> TexturePaths;
	EHTBlueprintToggleMode ToggleMode = EHTBlueprintToggleMode::MaterialSection;
	TWeakPtr<SWindow> CookedAssetExporterWindow;
	TWeakPtr<SWindow> MaterialInstanceCreatorWindow;
	TWeakPtr<SWindow> SettingsWindow;
	TSharedPtr<SEditableTextBox> ToggleVariableBox;
	TSharedPtr<SEditableTextBox> KeyNameBox;
	TSharedPtr<SEditableTextBox> MaterialIDsBox;
	TSharedPtr<SEditableTextBox> MaterialSlotsBox;
	TSharedPtr<SEditableTextBox> TextureParameterBox;
	TSharedPtr<SCheckBox> MultiMaterialCheckBox;
	TSharedPtr<SWidgetSwitcher> ModeOptionsSwitcher;
	TSharedPtr<SVerticalBox> TextureRowsBox;
	TSharedPtr<SCheckBox> InitGraphCheckBox;
	TSharedPtr<SCheckBox> UpdateGraphCheckBox;
	TSharedPtr<SCheckBox> SaveAssetsCheckBox;
	TSharedPtr<STextBlock> AssetSummaryText;
	TSharedPtr<STextBlock> StatusText;
};
