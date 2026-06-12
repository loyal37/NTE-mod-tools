#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCheckBox;
class SEditableTextBox;
class SSearchBox;
class STextBlock;
class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;

struct FHTCookedAssetExportItem
{
	FString RelativeAssetPath;
	TArray<FString> SourceFiles;
	int64 TotalSize = 0;
	bool bSelected = false;
};

class SHTCookedAssetExporter : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHTCookedAssetExporter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnBrowseSourceClicked();
	FReply OnBrowseOutputClicked();
	FReply OnScanClicked();
	FReply OnSelectAllClicked();
	FReply OnClearSelectionClicked();
	FReply OnExportClicked();

	void ScanSourceDirectory();
	void ApplySearchFilter();
	void OnSearchTextChanged(const FText& NewText);
	void OnItemCheckStateChanged(ECheckBoxState NewState, TSharedPtr<FHTCookedAssetExportItem> Item);
	TSharedRef<ITableRow> GenerateAssetRow(TSharedPtr<FHTCookedAssetExportItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void UpdateSelectionSummary();
	void ShowStatus(const FText& Text, bool bError = false) const;
	FString GetSourceDirectory() const;
	FString GetOutputDirectory() const;
	bool ChooseDirectory(const FText& DialogTitle, const FString& DefaultPath, FString& OutDirectory) const;

	TArray<TSharedPtr<FHTCookedAssetExportItem>> AssetItems;
	TArray<TSharedPtr<FHTCookedAssetExportItem>> FilteredAssetItems;
	TSharedPtr<SListView<TSharedPtr<FHTCookedAssetExportItem>>> AssetListView;
	TSharedPtr<SEditableTextBox> SourceDirectoryBox;
	TSharedPtr<SEditableTextBox> OutputDirectoryBox;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SCheckBox> OverwriteCheckBox;
	TSharedPtr<STextBlock> SelectionSummaryText;
	TSharedPtr<STextBlock> ExportStatusText;
};
