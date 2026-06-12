#include "SHTCookedAssetExporter.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SHTCookedAssetExporter"

namespace HTCookedAssetExporter
{
	static FString NormalizeDirectory(FString Directory)
	{
		Directory.TrimStartAndEndInline();
		Directory = FPaths::ConvertRelativePathToFull(Directory);
		FPaths::NormalizeDirectoryName(Directory);
		return Directory;
	}

	static FString FormatFileSize(const int64 Bytes)
	{
		if (Bytes >= 1024ll * 1024ll)
		{
			return FString::Printf(TEXT("%.1f MB"), static_cast<double>(Bytes) / (1024.0 * 1024.0));
		}
		if (Bytes >= 1024ll)
		{
			return FString::Printf(TEXT("%.1f KB"), static_cast<double>(Bytes) / 1024.0);
		}
		return FString::Printf(TEXT("%lld B"), Bytes);
	}
}

void SHTCookedAssetExporter::Construct(const FArguments& InArgs)
{
	const FString DefaultCookedDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Cooked/Windows"),
		FApp::GetProjectName(),
		TEXT("Content/Characters/Player"));

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
				.Text(LOCTEXT("Heading", "Cooked asset export"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
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
					.WidthOverride(110)
					[
						SNew(STextBlock).Text(LOCTEXT("SourceDirectory", "Cooked source"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0, 0, 6, 0)
				[
					SAssignNew(SourceDirectoryBox, SEditableTextBox)
					.Text(FText::FromString(DefaultCookedDirectory))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("BrowseSourceTooltip", "Choose the cooked character asset directory."))
					.OnClicked(this, &SHTCookedAssetExporter::OnBrowseSourceClicked)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.FolderOpen"))
					]
				]
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
					.WidthOverride(110)
					[
						SNew(STextBlock).Text(LOCTEXT("OutputDirectory", "Output directory"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0, 0, 6, 0)
				[
					SAssignNew(OutputDirectoryBox, SEditableTextBox)
					.HintText(LOCTEXT("OutputHint", "Choose the character directory used by the external packager"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("BrowseOutputTooltip", "Choose the destination directory."))
					.OnClicked(this, &SHTCookedAssetExporter::OnBrowseOutputClicked)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.FolderOpen"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0, 0, 8, 0)
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(LOCTEXT("SearchHint", "Filter cooked assets"))
					.OnTextChanged(this, &SHTCookedAssetExporter::OnSearchTextChanged)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Scan", "Scan"))
					.OnClicked(this, &SHTCookedAssetExporter::OnScanClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SelectAll", "Select visible"))
					.OnClicked(this, &SHTCookedAssetExporter::OnSelectAllClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Clear", "Clear"))
					.OnClicked(this, &SHTCookedAssetExporter::OnClearSelectionClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.MinHeight(320.0f)
			[
				SNew(SBorder)
				.Padding(4)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				[
					SAssignNew(AssetListView, SListView<TSharedPtr<FHTCookedAssetExportItem>>)
					.ListItemsSource(&FilteredAssetItems)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SHTCookedAssetExporter::GenerateAssetRow)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(SelectionSummaryText, STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12, 0, 0, 0)
				[
					SAssignNew(OverwriteCheckBox, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("Overwrite", "Overwrite existing files"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ExportStatusText, STextBlock)
					.Text(LOCTEXT("InitialStatus", "Choose a cooked character directory, then scan."))
					.AutoWrapText(true)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Export", "Export selected assets"))
					.OnClicked(this, &SHTCookedAssetExporter::OnExportClicked)
				]
			]
		]
	];

	UpdateSelectionSummary();
}

FReply SHTCookedAssetExporter::OnBrowseSourceClicked()
{
	FString Directory;
	if (ChooseDirectory(LOCTEXT("ChooseSource", "Choose cooked character directory"), GetSourceDirectory(), Directory))
	{
		SourceDirectoryBox->SetText(FText::FromString(Directory));
		ScanSourceDirectory();
	}
	return FReply::Handled();
}

FReply SHTCookedAssetExporter::OnBrowseOutputClicked()
{
	FString Directory;
	if (ChooseDirectory(LOCTEXT("ChooseOutput", "Choose output directory"), GetOutputDirectory(), Directory))
	{
		OutputDirectoryBox->SetText(FText::FromString(Directory));
	}
	return FReply::Handled();
}

FReply SHTCookedAssetExporter::OnScanClicked()
{
	ScanSourceDirectory();
	return FReply::Handled();
}

FReply SHTCookedAssetExporter::OnSelectAllClicked()
{
	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : FilteredAssetItems)
	{
		Item->bSelected = true;
	}
	AssetListView->RequestListRefresh();
	UpdateSelectionSummary();
	return FReply::Handled();
}

FReply SHTCookedAssetExporter::OnClearSelectionClicked()
{
	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : AssetItems)
	{
		Item->bSelected = false;
	}
	AssetListView->RequestListRefresh();
	UpdateSelectionSummary();
	return FReply::Handled();
}

FReply SHTCookedAssetExporter::OnExportClicked()
{
	using namespace HTCookedAssetExporter;

	const FString SourceDirectory = NormalizeDirectory(GetSourceDirectory());
	const FString OutputDirectory = NormalizeDirectory(GetOutputDirectory());
	if (!IFileManager::Get().DirectoryExists(*SourceDirectory))
	{
		ShowStatus(LOCTEXT("InvalidSource", "The cooked source directory does not exist."), true);
		return FReply::Handled();
	}
	if (GetOutputDirectory().TrimStartAndEnd().IsEmpty())
	{
		ShowStatus(LOCTEXT("MissingOutput", "Choose an output directory."), true);
		return FReply::Handled();
	}

	const FString SourcePrefix = SourceDirectory + TEXT("/");
	if (OutputDirectory.Equals(SourceDirectory, ESearchCase::IgnoreCase) || OutputDirectory.StartsWith(SourcePrefix, ESearchCase::IgnoreCase))
	{
		ShowStatus(LOCTEXT("InvalidOutput", "The output directory cannot be the source directory or a child of it."), true);
		return FReply::Handled();
	}

	TArray<TSharedPtr<FHTCookedAssetExportItem>> SelectedItems;
	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : AssetItems)
	{
		if (Item->bSelected)
		{
			SelectedItems.Add(Item);
		}
	}
	if (SelectedItems.IsEmpty())
	{
		ShowStatus(LOCTEXT("NothingSelected", "Select at least one cooked asset."), true);
		return FReply::Handled();
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const bool bOverwrite = OverwriteCheckBox.IsValid() && OverwriteCheckBox->IsChecked();
	int32 CopiedFiles = 0;
	int32 SkippedFiles = 0;
	TArray<FString> FailedFiles;

	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : SelectedItems)
	{
		for (const FString& SourceFile : Item->SourceFiles)
		{
			FString RelativeFile = SourceFile;
			if (!FPaths::MakePathRelativeTo(RelativeFile, *SourcePrefix))
			{
				FailedFiles.Add(SourceFile);
				continue;
			}

			const FString DestinationFile = FPaths::Combine(OutputDirectory, RelativeFile);
			PlatformFile.CreateDirectoryTree(*FPaths::GetPath(DestinationFile));
			if (PlatformFile.FileExists(*DestinationFile))
			{
				if (!bOverwrite)
				{
					++SkippedFiles;
					continue;
				}
				if (!PlatformFile.DeleteFile(*DestinationFile))
				{
					FailedFiles.Add(RelativeFile);
					continue;
				}
			}

			if (PlatformFile.CopyFile(*DestinationFile, *SourceFile))
			{
				++CopiedFiles;
			}
			else
			{
				FailedFiles.Add(RelativeFile);
			}
		}
	}

	if (!FailedFiles.IsEmpty())
	{
		ShowStatus(FText::Format(
			LOCTEXT("ExportPartialFailure", "Copied {0} files, skipped {1}, failed {2}. First failure: {3}"),
			FText::AsNumber(CopiedFiles),
			FText::AsNumber(SkippedFiles),
			FText::AsNumber(FailedFiles.Num()),
			FText::FromString(FailedFiles[0])), true);
		return FReply::Handled();
	}

	ShowStatus(FText::Format(
		LOCTEXT("ExportSuccess", "Export complete: {0} assets, {1} files copied, {2} skipped."),
		FText::AsNumber(SelectedItems.Num()),
		FText::AsNumber(CopiedFiles),
		FText::AsNumber(SkippedFiles)));
	return FReply::Handled();
}

void SHTCookedAssetExporter::ScanSourceDirectory()
{
	using namespace HTCookedAssetExporter;

	AssetItems.Reset();
	FilteredAssetItems.Reset();
	const FString SourceDirectory = NormalizeDirectory(GetSourceDirectory());
	if (!IFileManager::Get().DirectoryExists(*SourceDirectory))
	{
		AssetListView->RequestListRefresh();
		UpdateSelectionSummary();
		ShowStatus(LOCTEXT("ScanInvalidSource", "The cooked source directory does not exist."), true);
		return;
	}

	TArray<FString> UAssetFiles;
	IFileManager::Get().FindFilesRecursive(UAssetFiles, *SourceDirectory, TEXT("*.uasset"), true, false, false);
	UAssetFiles.Sort();
	const FString SourcePrefix = SourceDirectory + TEXT("/");
	const TCHAR* SidecarExtensions[] = { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl") };

	for (const FString& UAssetFile : UAssetFiles)
	{
		TSharedPtr<FHTCookedAssetExportItem> Item = MakeShared<FHTCookedAssetExportItem>();
		Item->RelativeAssetPath = UAssetFile;
		FPaths::MakePathRelativeTo(Item->RelativeAssetPath, *SourcePrefix);
		FPaths::MakeStandardFilename(Item->RelativeAssetPath);

		const FString BasePath = FPaths::Combine(FPaths::GetPath(UAssetFile), FPaths::GetBaseFilename(UAssetFile));
		for (const TCHAR* Extension : SidecarExtensions)
		{
			const FString Candidate = BasePath + TEXT(".") + Extension;
			if (IFileManager::Get().FileExists(*Candidate))
			{
				Item->SourceFiles.Add(Candidate);
				Item->TotalSize += FMath::Max<int64>(0, IFileManager::Get().FileSize(*Candidate));
			}
		}

		AssetItems.Add(Item);
	}

	ApplySearchFilter();
	ShowStatus(FText::Format(LOCTEXT("ScanComplete", "Scan complete: {0} cooked assets found."), FText::AsNumber(AssetItems.Num())));
}

void SHTCookedAssetExporter::ApplySearchFilter()
{
	FilteredAssetItems.Reset();
	const FString Filter = SearchBox.IsValid() ? SearchBox->GetText().ToString().TrimStartAndEnd() : FString();
	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : AssetItems)
	{
		if (Filter.IsEmpty() || Item->RelativeAssetPath.Contains(Filter, ESearchCase::IgnoreCase))
		{
			FilteredAssetItems.Add(Item);
		}
	}
	if (AssetListView.IsValid())
	{
		AssetListView->RequestListRefresh();
	}
	UpdateSelectionSummary();
}

void SHTCookedAssetExporter::OnSearchTextChanged(const FText& NewText)
{
	ApplySearchFilter();
}

void SHTCookedAssetExporter::OnItemCheckStateChanged(ECheckBoxState NewState, TSharedPtr<FHTCookedAssetExportItem> Item)
{
	if (Item.IsValid())
	{
		Item->bSelected = NewState == ECheckBoxState::Checked;
		UpdateSelectionSummary();
	}
}

TSharedRef<ITableRow> SHTCookedAssetExporter::GenerateAssetRow(TSharedPtr<FHTCookedAssetExportItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FHTCookedAssetExportItem>>, OwnerTable)
		.Padding(FMargin(4, 2))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() { return Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged(this, &SHTCookedAssetExporter::OnItemCheckStateChanged, Item)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->RelativeAssetPath))
				.ToolTipText(FText::FromString(Item->RelativeAssetPath))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("AssetDetails", "{0} files | {1}"),
					FText::AsNumber(Item->SourceFiles.Num()),
					FText::FromString(HTCookedAssetExporter::FormatFileSize(Item->TotalSize))))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

void SHTCookedAssetExporter::UpdateSelectionSummary()
{
	if (!SelectionSummaryText.IsValid())
	{
		return;
	}

	int32 SelectedAssets = 0;
	int32 SelectedFiles = 0;
	int64 SelectedBytes = 0;
	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : AssetItems)
	{
		if (Item->bSelected)
		{
			++SelectedAssets;
			SelectedFiles += Item->SourceFiles.Num();
			SelectedBytes += Item->TotalSize;
		}
	}

	SelectionSummaryText->SetText(FText::Format(
		LOCTEXT("SelectionSummary", "Showing {0}/{1} assets | Selected {2} assets, {3} files, {4}"),
		FText::AsNumber(FilteredAssetItems.Num()),
		FText::AsNumber(AssetItems.Num()),
		FText::AsNumber(SelectedAssets),
		FText::AsNumber(SelectedFiles),
		FText::FromString(HTCookedAssetExporter::FormatFileSize(SelectedBytes))));
}

void SHTCookedAssetExporter::ShowStatus(const FText& Text, bool bError) const
{
	if (ExportStatusText.IsValid())
	{
		ExportStatusText->SetText(Text);
		ExportStatusText->SetColorAndOpacity(bError ? FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f)) : FSlateColor::UseForeground());
	}

	FNotificationInfo Info(Text);
	Info.ExpireDuration = bError ? 7.0f : 4.0f;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bError ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}
}

FString SHTCookedAssetExporter::GetSourceDirectory() const
{
	return SourceDirectoryBox.IsValid() ? SourceDirectoryBox->GetText().ToString() : FString();
}

FString SHTCookedAssetExporter::GetOutputDirectory() const
{
	return OutputDirectoryBox.IsValid() ? OutputDirectoryBox->GetText().ToString() : FString();
}

bool SHTCookedAssetExporter::ChooseDirectory(const FText& DialogTitle, const FString& DefaultPath, FString& OutDirectory) const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return false;
	}

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	void* ParentHandle = ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid()
		? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;
	return DesktopPlatform->OpenDirectoryDialog(ParentHandle, DialogTitle.ToString(), DefaultPath, OutDirectory);
}

#undef LOCTEXT_NAMESPACE
