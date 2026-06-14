#pragma once

#include "CoreMinimal.h"
#include "HTBlueprintToggleGenerator.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class STextBlock;
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
	FReply OnOpenCookedAssetExporterClicked();
	FReply OnOpenSettingsClicked();
	FReply OnCloseSettingsClicked();

	TSharedRef<SWidget> MakeTextRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox) const;
	TSharedRef<SWidget> MakeNumberRow(const FText& Label, int32& ValueRef) const;
	TSharedRef<SWidget> MakeBlueprintPickerRow(const FText& Label, bool bAnimBlueprint);
	TSharedRef<SWidget> MakeTexturePickerRow(const FText& Label, bool bTextureA);

	bool ParseMaterialIDs(TArray<int32>& OutMaterialIDs, FString& OutError) const;
	void ShowPanelError(const FText& ErrorText) const;
	FString GetAnimBlueprintPath() const;
	FString GetSaveGameBlueprintPath() const;
	void OnAnimBlueprintChanged(const FAssetData& AssetData);
	void OnSaveGameBlueprintChanged(const FAssetData& AssetData);
	void OnTextureAChanged(const FAssetData& AssetData);
	void OnTextureBChanged(const FAssetData& AssetData);
	void UpdateAssetSummaryText() const;
	FString GetShortAssetName(const FString& ObjectPath) const;
	FString GetTextureAPath() const;
	FString GetTextureBPath() const;
	void OnToggleModeChanged(EHTBlueprintToggleMode NewMode);

	FString AnimBlueprintPath;
	FString SaveGameBlueprintPath;
	FString TextureAPath;
	FString TextureBPath;
	EHTBlueprintToggleMode ToggleMode = EHTBlueprintToggleMode::MaterialSection;
	int32 MaterialElementIndex = 0;
	TWeakPtr<SWindow> CookedAssetExporterWindow;
	TWeakPtr<SWindow> SettingsWindow;
	TSharedPtr<SEditableTextBox> ToggleVariableBox;
	TSharedPtr<SEditableTextBox> KeyNameBox;
	TSharedPtr<SEditableTextBox> MaterialIDsBox;
	TSharedPtr<SEditableTextBox> TextureParameterBox;
	TSharedPtr<SCheckBox> MultiMaterialCheckBox;
	TSharedPtr<SWidgetSwitcher> ModeOptionsSwitcher;
	TSharedPtr<SCheckBox> InitGraphCheckBox;
	TSharedPtr<SCheckBox> UpdateGraphCheckBox;
	TSharedPtr<SCheckBox> SaveAssetsCheckBox;
	TSharedPtr<STextBlock> AssetSummaryText;
	TSharedPtr<STextBlock> StatusText;
};
