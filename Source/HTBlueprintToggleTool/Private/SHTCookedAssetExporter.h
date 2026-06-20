#pragma once

#include "CoreMinimal.h"
#include "AssetThumbnail.h"
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
	FString AssetName;
	FString FolderPath;
	FString AssetType;
	TArray<FString> SourceFiles;
	TSharedPtr<FAssetThumbnail> Thumbnail;
	int64 TotalSize = 0;
	bool bSelected = false;
};

class SHTCookedAssetExporter : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHTCookedAssetExporter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SHTCookedAssetExporter() override;

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
	void OnDirectoryTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
	void OnExportAndPackageCheckStateChanged(ECheckBoxState NewState);
	void OnOpenOutputDirectoryCheckStateChanged(ECheckBoxState NewState);
	void OnItemCheckStateChanged(ECheckBoxState NewState, TSharedPtr<FHTCookedAssetExportItem> Item);
	TSharedRef<ITableRow> GenerateAssetRow(TSharedPtr<FHTCookedAssetExportItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void UpdateSelectionSummary();
	void ShowStatus(const FText& Text, bool bError = false) const;
	FString GetSourceDirectory() const;
	FString GetOutputDirectory() const;
	bool GetProjectAssetDirectory(const FString& CookedSourceDirectory, FString& OutProjectDirectory, FString& OutGamePath) const;
	FString GetExportRootDirectory(const FString& CookedSourceDirectory, const FString& OutputDirectory) const;
	FString GetSelectionConfigKey() const;
	void LoadSelectedAssetPaths(TSet<FString>& OutSelectedPaths) const;
	void SaveSelection() const;
	void SaveDirectories() const;
	bool ChooseDirectory(const FText& DialogTitle, const FString& DefaultPath, FString& OutDirectory) const;

	TArray<TSharedPtr<FHTCookedAssetExportItem>> AssetItems;
	TArray<TSharedPtr<FHTCookedAssetExportItem>> FilteredAssetItems;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<SListView<TSharedPtr<FHTCookedAssetExportItem>>> AssetListView;
	TSharedPtr<SEditableTextBox> SourceDirectoryBox;
	TSharedPtr<SEditableTextBox> OutputDirectoryBox;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SCheckBox> OverwriteCheckBox;
	TSharedPtr<SCheckBox> ExportAndPackageCheckBox;
	TSharedPtr<SCheckBox> OpenOutputDirectoryCheckBox;
	TSharedPtr<STextBlock> SelectionSummaryText;
	TSharedPtr<STextBlock> ExportStatusText;
};
