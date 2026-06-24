#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SBox;
class SEditableTextBox;
class STextBlock;
class SVerticalBox;
class ISlateViewport;
class UMaterialInterface;
class USkeletalMesh;

class SHTMaterialSlotMapper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHTMaterialSlotMapper) {}
		SLATE_ARGUMENT(FString, AnimBlueprintPath)
		SLATE_ARGUMENT(FString, CharacterFolderPath)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FSlotEntry
	{
		int32 Index = INDEX_NONE;
		FName SlotName;
		TWeakObjectPtr<UMaterialInterface> Material;
		bool bChecked = false;
	};

	struct FMaterialOption
	{
		FString DisplayName;
		FString ObjectPath;
		TWeakObjectPtr<UMaterialInterface> Material;
	};

	struct FMappingRow
	{
		FString MaterialPath;
		TSharedPtr<SBox> MaterialPreviewBox;
		TSharedPtr<SEditableTextBox> SlotIdsBox;
	};

	bool LoadContext(FString& OutError);
	void RebuildSlotList();
	void RebuildMappingRows();
	void ShowStatus(const FText& Text, bool bError = false) const;

	TSharedRef<SWidget> MakeSlotRow(int32 SlotIndex);
	TSharedRef<SWidget> MakeMappingRow(TSharedPtr<FMappingRow> Mapping);
	TSharedRef<SWidget> MakeMaterialCombo(TSharedPtr<FMappingRow> Mapping);
	TSharedRef<SWidget> MakeMaterialComboItem(TSharedPtr<FMaterialOption> Option);
	TSharedRef<SWidget> MakeMaterialPreview(UMaterialInterface* Material, uint32 Size);
	void RefreshMappingMaterialPreview(TSharedPtr<FMappingRow> Mapping);

	FReply OnAutoMatchClicked();
	FReply OnAddMappingClicked();
	FReply OnRemoveMappingClicked(TSharedPtr<FMappingRow> Mapping);
	FReply OnUseCheckedSlotsClicked(TSharedPtr<FMappingRow> Mapping);
	FReply OnClearCheckedSlotsClicked();
	FReply OnApplyMappingsClicked();

	TSharedPtr<FMaterialOption> FindMaterialOptionByPath(const FString& ObjectPath) const;
	bool ParseSlotIds(const FString& RawValue, TArray<int32>& OutSlotIds, FString& OutError) const;
	bool ApplySlotAssignments(const TMap<int32, UMaterialInterface*>& Assignments, FString& OutMessage);
	FString GetCheckedSlotIds() const;
	FString GetSlotName(const FSlotEntry& Entry) const;

	FString AnimBlueprintPath;
	FString CharacterFolderPath;
	TWeakObjectPtr<USkeletalMesh> SkeletalMesh;
	TArray<FSlotEntry> Slots;
	TArray<TSharedPtr<FMaterialOption>> MaterialOptions;
	TArray<TSharedPtr<FMappingRow>> MappingRows;
	TArray<TSharedPtr<ISlateViewport>> MaterialPreviewViewports;

	TSharedPtr<SVerticalBox> SlotListBox;
	TSharedPtr<SVerticalBox> MappingRowsBox;
	TSharedPtr<SCheckBox> SaveAssetsCheckBox;
	TSharedPtr<STextBlock> SummaryText;
	TSharedPtr<STextBlock> StatusText;
};
