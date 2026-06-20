#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class STextBlock;
struct FAssetData;

class SHTMaterialInstanceCreator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHTMaterialInstanceCreator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakeAssetPickerRow(const FText& Label, UClass* AllowedClass, int32 AssetIndex);
	FReply OnCreateClicked();
	void OnAssetChanged(const FAssetData& AssetData, int32 AssetIndex);
	FString GetAssetPath(int32 AssetIndex) const;
	void ShowStatus(const FText& Text, bool bError) const;

	TArray<FString> AssetPaths;
	TSharedPtr<SEditableTextBox> InstanceNameBox;
	TSharedPtr<SCheckBox> SaveAssetsCheckBox;
	TSharedPtr<STextBlock> StatusText;
};
