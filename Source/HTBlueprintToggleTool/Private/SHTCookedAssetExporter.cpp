#include "SHTCookedAssetExporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
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
	static const TCHAR* ConfigSection = TEXT("HTBlueprintToggleTool.CookedAssetExporter");
	static const TCHAR* SourceDirectoryKey = TEXT("CookedSourceDirectory");
	static const TCHAR* OutputDirectoryKey = TEXT("OutputDirectory");
	static const TCHAR* ExportAndPackageKey = TEXT("ExportAndPackage");
	static const TCHAR* OpenOutputDirectoryKey = TEXT("OpenOutputDirectory");
	static const TCHAR* SelectedAssetsKeyPrefix = TEXT("SelectedAssets_");
	static const TCHAR* PackagerExecutable = TEXT("D:/NTE Mod Packager/NTE Mod Packager.exe");

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
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

	FString CookedDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Cooked/Windows"),
		FApp::GetProjectName(),
		TEXT("Content/Characters/Player"));
	FString OutputDirectory;
	bool bExportAndPackage = false;
	bool bOpenOutputDirectory = false;
	GConfig->GetString(HTCookedAssetExporter::ConfigSection, HTCookedAssetExporter::SourceDirectoryKey, CookedDirectory, GEditorPerProjectIni);
	GConfig->GetString(HTCookedAssetExporter::ConfigSection, HTCookedAssetExporter::OutputDirectoryKey, OutputDirectory, GEditorPerProjectIni);
	GConfig->GetBool(HTCookedAssetExporter::ConfigSection, HTCookedAssetExporter::ExportAndPackageKey, bExportAndPackage, GEditorPerProjectIni);
	GConfig->GetBool(HTCookedAssetExporter::ConfigSection, HTCookedAssetExporter::OpenOutputDirectoryKey, bOpenOutputDirectory, GEditorPerProjectIni);

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
					.Text(FText::FromString(CookedDirectory))
					.OnTextCommitted(this, &SHTCookedAssetExporter::OnDirectoryTextCommitted)
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
					.Text(FText::FromString(OutputDirectory))
					.HintText(LOCTEXT("OutputHint", "Choose the character directory used by the external packager"))
					.OnTextCommitted(this, &SHTCookedAssetExporter::OnDirectoryTextCommitted)
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
					.HintText(LOCTEXT("SearchHint", "Filter project assets"))
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
				.VAlign(VAlign_Top)
				.Padding(12, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(OverwriteCheckBox, SCheckBox)
						.IsChecked(ECheckBoxState::Checked)
						[
							SNew(STextBlock).Text(LOCTEXT("Overwrite", "Overwrite existing files"))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 6, 0, 0)
					[
						SAssignNew(ExportAndPackageCheckBox, SCheckBox)
						.IsChecked(bExportAndPackage ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.OnCheckStateChanged(this, &SHTCookedAssetExporter::OnExportAndPackageCheckStateChanged)
						.ToolTipText(LOCTEXT("ExportAndPackageTooltip", "After a successful export, launch NTE Mod Packager."))
						[
							SNew(STextBlock).Text(LOCTEXT("ExportAndPackage", "Export and package"))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 6, 0, 0)
					[
						SAssignNew(OpenOutputDirectoryCheckBox, SCheckBox)
						.IsChecked(bOpenOutputDirectory ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.OnCheckStateChanged(this, &SHTCookedAssetExporter::OnOpenOutputDirectoryCheckStateChanged)
						.ToolTipText(LOCTEXT("OpenOutputDirectoryTooltip", "Open the exported character directory after a successful export."))
						[
							SNew(STextBlock).Text(LOCTEXT("OpenOutputDirectory", "Open output directory after export"))
						]
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
					.Text(LOCTEXT("InitialStatus", "Choose a cooked character directory. Its matching project assets will be listed."))
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
	if (IFileManager::Get().DirectoryExists(*HTCookedAssetExporter::NormalizeDirectory(CookedDirectory)))
	{
		ScanSourceDirectory();
	}
}

SHTCookedAssetExporter::~SHTCookedAssetExporter()
{
	SaveDirectories();
}

FReply SHTCookedAssetExporter::OnBrowseSourceClicked()
{
	FString Directory;
	if (ChooseDirectory(LOCTEXT("ChooseSource", "Choose cooked character directory"), GetSourceDirectory(), Directory))
	{
		SourceDirectoryBox->SetText(FText::FromString(Directory));
		SaveDirectories();
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
		SaveDirectories();
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
		Item->bSelected = !Item->SourceFiles.IsEmpty();
	}
	AssetListView->RequestListRefresh();
	UpdateSelectionSummary();
	SaveSelection();
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
	SaveSelection();
	return FReply::Handled();
}

FReply SHTCookedAssetExporter::OnExportClicked()
{
	using namespace HTCookedAssetExporter;

	if (GetOutputDirectory().TrimStartAndEnd().IsEmpty())
	{
		ShowStatus(LOCTEXT("MissingOutput", "Choose an output directory."), true);
		return FReply::Handled();
	}

	const FString SourceDirectory = NormalizeDirectory(GetSourceDirectory());
	const FString OutputDirectory = NormalizeDirectory(GetOutputDirectory());
	const FString ExportRootDirectory = GetExportRootDirectory(SourceDirectory, OutputDirectory);
	const bool bExportAndPackage = ExportAndPackageCheckBox.IsValid() && ExportAndPackageCheckBox->IsChecked();
	const bool bOpenOutputDirectory = OpenOutputDirectoryCheckBox.IsValid() && OpenOutputDirectoryCheckBox->IsChecked();
	if (!IFileManager::Get().DirectoryExists(*SourceDirectory))
	{
		ShowStatus(LOCTEXT("InvalidSource", "The cooked source directory does not exist."), true);
		return FReply::Handled();
	}
	if (bExportAndPackage && !IFileManager::Get().FileExists(HTCookedAssetExporter::PackagerExecutable))
	{
		ShowStatus(FText::Format(
			LOCTEXT("MissingPackager", "NTE Mod Packager was not found: {0}"),
			FText::FromString(HTCookedAssetExporter::PackagerExecutable)), true);
		return FReply::Handled();
	}
	const FString SourcePrefix = SourceDirectory + TEXT("/");
	if (ExportRootDirectory.Equals(SourceDirectory, ESearchCase::IgnoreCase) || ExportRootDirectory.StartsWith(SourcePrefix, ESearchCase::IgnoreCase))
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
		ShowStatus(LOCTEXT("NothingSelected", "Select at least one available project asset."), true);
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

			const FString DestinationFile = FPaths::Combine(ExportRootDirectory, RelativeFile);
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

	if (bOpenOutputDirectory)
	{
		FPlatformProcess::ExploreFolder(*ExportRootDirectory);
	}

	if (bExportAndPackage)
	{
		const FString PackagerWorkingDirectory = FPaths::GetPath(HTCookedAssetExporter::PackagerExecutable);
		FProcHandle PackagerProcess = FPlatformProcess::CreateProc(
			HTCookedAssetExporter::PackagerExecutable,
			nullptr,
			true,
			false,
			false,
			nullptr,
			0,
			*PackagerWorkingDirectory,
			nullptr);
		if (!PackagerProcess.IsValid())
		{
			ShowStatus(FText::Format(
				LOCTEXT("PackagerLaunchFailed", "Export complete, but NTE Mod Packager could not be started: {0}"),
				FText::FromString(HTCookedAssetExporter::PackagerExecutable)), true);
			return FReply::Handled();
		}
		FPlatformProcess::CloseProc(PackagerProcess);

		ShowStatus(FText::Format(
			LOCTEXT("ExportAndPackageSuccess", "Export complete: {0} assets, {1} files copied, {2} skipped. NTE Mod Packager started."),
			FText::AsNumber(SelectedItems.Num()),
			FText::AsNumber(CopiedFiles),
			FText::AsNumber(SkippedFiles)));
	}
	else
	{
		ShowStatus(FText::Format(
			LOCTEXT("ExportSuccess", "Export complete: {0} assets, {1} files copied, {2} skipped."),
			FText::AsNumber(SelectedItems.Num()),
			FText::AsNumber(CopiedFiles),
			FText::AsNumber(SkippedFiles)));
	}
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

	FString ProjectAssetDirectory;
	FString GamePath;
	if (!GetProjectAssetDirectory(SourceDirectory, ProjectAssetDirectory, GamePath))
	{
		AssetListView->RequestListRefresh();
		UpdateSelectionSummary();
		ShowStatus(LOCTEXT("CannotMapProjectDirectory", "The cooked directory must be inside a Content folder so it can be mapped to the project."), true);
		return;
	}
	if (!IFileManager::Get().DirectoryExists(*ProjectAssetDirectory))
	{
		AssetListView->RequestListRefresh();
		UpdateSelectionSummary();
		ShowStatus(FText::Format(LOCTEXT("MissingProjectDirectory", "Matching project directory does not exist: {0}"), FText::FromString(ProjectAssetDirectory)), true);
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> ProjectAssets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*GamePath), ProjectAssets, true);
	ProjectAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
	{
		return Left.PackageName.LexicalLess(Right.PackageName);
	});

	const TCHAR* SidecarExtensions[] = { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl") };
	TSet<FString> SavedSelectedPaths;
	LoadSelectedAssetPaths(SavedSelectedPaths);
	int32 CookedAssetCount = 0;
	const FString GamePathPrefix = GamePath + TEXT("/");

	for (const FAssetData& AssetData : ProjectAssets)
	{
		const FString PackageName = AssetData.PackageName.ToString();
		if (!PackageName.StartsWith(GamePathPrefix, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FHTCookedAssetExportItem> Item = MakeShared<FHTCookedAssetExportItem>();
		Item->RelativeAssetPath = PackageName.Mid(GamePathPrefix.Len()) + TEXT(".uasset");
		FPaths::NormalizeFilename(Item->RelativeAssetPath);
		Item->AssetName = AssetData.AssetName.ToString();
		Item->FolderPath = FPaths::GetPath(Item->RelativeAssetPath);
		Item->AssetType = AssetData.AssetClassPath.GetAssetName().ToString();
		Item->Thumbnail = MakeShared<FAssetThumbnail>(AssetData, 48, 48, ThumbnailPool);

		const FString CookedUAssetFile = FPaths::Combine(SourceDirectory, Item->RelativeAssetPath);
		const FString BasePath = FPaths::Combine(FPaths::GetPath(CookedUAssetFile), FPaths::GetBaseFilename(CookedUAssetFile));
		for (const TCHAR* Extension : SidecarExtensions)
		{
			const FString Candidate = BasePath + TEXT(".") + Extension;
			if (IFileManager::Get().FileExists(*Candidate))
			{
				Item->SourceFiles.Add(Candidate);
				Item->TotalSize += FMath::Max<int64>(0, IFileManager::Get().FileSize(*Candidate));
			}
		}
		if (!Item->SourceFiles.IsEmpty())
		{
			++CookedAssetCount;
			Item->bSelected = SavedSelectedPaths.Contains(Item->RelativeAssetPath);
		}

		AssetItems.Add(Item);
	}

	ApplySearchFilter();
	ShowStatus(FText::Format(
		LOCTEXT("ScanComplete", "Found {0} project assets in {1}; {2} have cooked files available."),
		FText::AsNumber(AssetItems.Num()),
		FText::FromString(GamePath),
		FText::AsNumber(CookedAssetCount)));
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

void SHTCookedAssetExporter::OnDirectoryTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	(void)NewText;
	(void)CommitType;
	SaveDirectories();
}

void SHTCookedAssetExporter::OnExportAndPackageCheckStateChanged(ECheckBoxState NewState)
{
	GConfig->SetBool(
		HTCookedAssetExporter::ConfigSection,
		HTCookedAssetExporter::ExportAndPackageKey,
		NewState == ECheckBoxState::Checked,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SHTCookedAssetExporter::OnOpenOutputDirectoryCheckStateChanged(ECheckBoxState NewState)
{
	GConfig->SetBool(
		HTCookedAssetExporter::ConfigSection,
		HTCookedAssetExporter::OpenOutputDirectoryKey,
		NewState == ECheckBoxState::Checked,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SHTCookedAssetExporter::OnItemCheckStateChanged(ECheckBoxState NewState, TSharedPtr<FHTCookedAssetExportItem> Item)
{
	if (Item.IsValid())
	{
		Item->bSelected = !Item->SourceFiles.IsEmpty() && NewState == ECheckBoxState::Checked;
		UpdateSelectionSummary();
		SaveSelection();
	}
}

TSharedRef<ITableRow> SHTCookedAssetExporter::GenerateAssetRow(TSharedPtr<FHTCookedAssetExportItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SWidget> ThumbnailWidget = Item->Thumbnail.IsValid()
		? Item->Thumbnail->MakeThumbnailWidget()
		: StaticCastSharedRef<SWidget>(SNew(SImage).Image(FAppStyle::GetBrush("ClassIcon.Object")));
	const FString DetailText = Item->FolderPath.IsEmpty()
		? Item->AssetType
		: FString::Printf(TEXT("%s  |  %s"), *Item->AssetType, *Item->FolderPath);

	return SNew(STableRow<TSharedPtr<FHTCookedAssetExportItem>>, OwnerTable)
		.Padding(FMargin(4, 3))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SCheckBox)
				.IsEnabled(!Item->SourceFiles.IsEmpty())
				.IsChecked_Lambda([Item]() { return Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged(this, &SHTCookedAssetExporter::OnItemCheckStateChanged, Item)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 10, 0)
			[
				SNew(SBox)
				.WidthOverride(48)
				.HeightOverride(48)
				[
					ThumbnailWidget
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
					.Text(FText::FromString(Item->AssetName))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ToolTipText(FText::FromString(Item->RelativeAssetPath))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 3, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(DetailText))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(Item->SourceFiles.IsEmpty()
					? LOCTEXT("NotCooked", "Not cooked")
					: FText::Format(
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

bool SHTCookedAssetExporter::GetProjectAssetDirectory(const FString& CookedSourceDirectory, FString& OutProjectDirectory, FString& OutGamePath) const
{
	FString StandardSource = CookedSourceDirectory;
	FPaths::MakeStandardFilename(StandardSource);
	const FString ContentMarker = TEXT("/Content/");
	const int32 ContentIndex = StandardSource.Find(ContentMarker, ESearchCase::IgnoreCase, ESearchDir::FromStart);
	if (ContentIndex == INDEX_NONE)
	{
		return false;
	}

	const FString ContentRelativePath = StandardSource.Mid(ContentIndex + ContentMarker.Len());
	if (ContentRelativePath.IsEmpty())
	{
		return false;
	}

	OutProjectDirectory = HTCookedAssetExporter::NormalizeDirectory(FPaths::Combine(FPaths::ProjectContentDir(), ContentRelativePath));
	OutGamePath = TEXT("/Game/") + ContentRelativePath;
	FPaths::NormalizeDirectoryName(OutGamePath);
	return true;
}

FString SHTCookedAssetExporter::GetSelectionConfigKey() const
{
	const FString NormalizedSource = HTCookedAssetExporter::NormalizeDirectory(GetSourceDirectory()).ToLower();
	return FString::Printf(TEXT("%s%08X"), HTCookedAssetExporter::SelectedAssetsKeyPrefix, GetTypeHash(NormalizedSource));
}

void SHTCookedAssetExporter::LoadSelectedAssetPaths(TSet<FString>& OutSelectedPaths) const
{
	TArray<FString> SelectedPaths;
	GConfig->GetArray(HTCookedAssetExporter::ConfigSection, *GetSelectionConfigKey(), SelectedPaths, GEditorPerProjectIni);
	for (FString& Path : SelectedPaths)
	{
		FPaths::NormalizeFilename(Path);
		OutSelectedPaths.Add(MoveTemp(Path));
	}
}

void SHTCookedAssetExporter::SaveSelection() const
{
	if (!GConfig || !SourceDirectoryBox.IsValid())
	{
		return;
	}

	TArray<FString> SelectedPaths;
	for (const TSharedPtr<FHTCookedAssetExportItem>& Item : AssetItems)
	{
		if (Item.IsValid() && Item->bSelected)
		{
			SelectedPaths.Add(Item->RelativeAssetPath);
		}
	}
	SelectedPaths.Sort();
	GConfig->SetArray(HTCookedAssetExporter::ConfigSection, *GetSelectionConfigKey(), SelectedPaths, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

FString SHTCookedAssetExporter::GetExportRootDirectory(const FString& CookedSourceDirectory, const FString& OutputDirectory) const
{
	const FString CharacterDirectoryName = FPaths::GetCleanFilename(CookedSourceDirectory);
	if (FPaths::GetCleanFilename(OutputDirectory).Equals(CharacterDirectoryName, ESearchCase::IgnoreCase))
	{
		return OutputDirectory;
	}
	return FPaths::Combine(OutputDirectory, CharacterDirectoryName);
}

void SHTCookedAssetExporter::SaveDirectories() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetString(HTCookedAssetExporter::ConfigSection, HTCookedAssetExporter::SourceDirectoryKey, *GetSourceDirectory(), GEditorPerProjectIni);
	GConfig->SetString(HTCookedAssetExporter::ConfigSection, HTCookedAssetExporter::OutputDirectoryKey, *GetOutputDirectory(), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
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
