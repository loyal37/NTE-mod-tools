#include "HTToggleGraphEditor.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphCompiler.h"
#include "HTToggleGraphNodes.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HTToggleGraphEditor"

const FName FHTToggleGraphEditor::GraphTabId(TEXT("HTToggleGraphEditor_Graph"));
const FName FHTToggleGraphEditor::DetailsTabId(TEXT("HTToggleGraphEditor_Details"));
const FName FHTToggleGraphEditor::ResultTabId(TEXT("HTToggleGraphEditor_Result"));
const FName FHTToggleGraphEditor::AppIdentifier(TEXT("HTToggleGraphEditorApp"));

void FHTToggleGraphEditor::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UHTToggleGraph* InAsset)
{
	Asset = InAsset;
	Asset->EnsureGraph();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bHideSelectionTip = true;
	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(Asset);

	BindGraphCommands();

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FHTToggleGraphEditor::OnSelectedNodesChanged);
	Events.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FHTToggleGraphEditor::OnNodeDoubleClicked);
	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("GraphCorner", "HT TOGGLE TREE");
	GraphEditor = SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.Appearance(Appearance)
		.GraphToEdit(Asset->Graph)
		.GraphEvents(Events)
		.AutoExpandActionMenu(true);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("HTToggleGraphEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.72f)
				->SetHideTabWell(true)
				->AddTab(GraphTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.28f)
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.62f)->AddTab(DetailsTabId, ETabState::OpenedTab))
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.38f)->AddTab(ResultTabId, ETabState::OpenedTab))
			)
		);

	ExtendToolbar();
	InitAssetEditor(Mode, InitToolkitHost, AppIdentifier, Layout, true, true, Asset);
}

void FHTToggleGraphEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "HT Toggle Graph"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FHTToggleGraphEditor::SpawnGraphTab))
		.SetDisplayName(LOCTEXT("GraphTab", "蓝图"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FHTToggleGraphEditor::SpawnDetailsTab))
		.SetDisplayName(LOCTEXT("DetailsTab", "属性"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
	InTabManager->RegisterTabSpawner(ResultTabId, FOnSpawnTab::CreateSP(this, &FHTToggleGraphEditor::SpawnResultTab))
		.SetDisplayName(LOCTEXT("ResultTab", "结果"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"));
}

void FHTToggleGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(GraphTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(ResultTabId);
}

FName FHTToggleGraphEditor::GetToolkitFName() const
{
	return FName("HTToggleGraphEditor");
}

FText FHTToggleGraphEditor::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "HT Toggle Graph");
}

FString FHTToggleGraphEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("HTToggleGraph");
}

FLinearColor FHTToggleGraphEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.9f, 0.6f, 0.2f, 0.5f);
}

TSharedRef<SDockTab> FHTToggleGraphEditor::SpawnGraphTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("GraphTabLabel", "蓝图"))
		[
			GraphEditor.ToSharedRef()
		];
}

TSharedRef<SDockTab> FHTToggleGraphEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "属性"))
		[
			DetailsView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FHTToggleGraphEditor::SpawnResultTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ResultTabLabel", "结果"))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8)
			[
				SAssignNew(ResultText, STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ResultHint", "Draw the tree, then press 编译 to generate the Animation Blueprint nodes."))
			]
		];
}

void FHTToggleGraphEditor::BindGraphCommands()
{
	if (GraphEditorCommands.IsValid())
	{
		return;
	}
	GraphEditorCommands = MakeShared<FUICommandList>();
	GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::SelectAllNodes),
		FCanExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CanSelectAllNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CanDeleteNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CanCopyNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CanCutNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::PasteNodes),
		FCanExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CanPasteNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::DuplicateNodes),
		FCanExecuteAction::CreateSP(this, &FHTToggleGraphEditor::CanDuplicateNodes));
}

void FHTToggleGraphEditor::ExtendToolbar()
{
	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& Builder)
		{
			Builder.BeginSection("HTToggleGraph");
			Builder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateSP(this, &FHTToggleGraphEditor::Compile)),
				NAME_None,
				LOCTEXT("CompileButton", "编译"),
				LOCTEXT("CompileTooltip", "Convert the tree into Animation Blueprint nodes (replaces the previous tree of the same name)."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));
			Builder.EndSection();
		}));
	AddToolbarExtender(Extender);
}

void FHTToggleGraphEditor::OnSelectedNodesChanged(const TSet<UObject*>& Selection)
{
	if (!DetailsView.IsValid())
	{
		return;
	}
	TArray<UObject*> Objects;
	for (UObject* Object : Selection)
	{
		if (Object)
		{
			Objects.Add(Object);
		}
	}
	if (Objects.IsEmpty())
	{
		DetailsView->SetObject(Asset);
	}
	else
	{
		DetailsView->SetObjects(Objects);
	}
}

void FHTToggleGraphEditor::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	if (GraphEditor.IsValid() && Node)
	{
		GraphEditor->JumpToNode(Node, false, true);
	}
}

void FHTToggleGraphEditor::SetResultText(const FString& Text, const bool bError)
{
	if (ResultText.IsValid())
	{
		ResultText->SetText(FText::FromString(Text));
		ResultText->SetColorAndOpacity(bError ? FSlateColor(FLinearColor(1.0f, 0.3f, 0.25f)) : FSlateColor::UseForeground());
	}
}

void FHTToggleGraphEditor::Compile()
{
	if (!Asset)
	{
		return;
	}
	const FHTNestedToggleGeneratorResult Result = FHTToggleGraphCompiler::Generate(Asset, Asset->bSaveBlueprintsAfterCompile);
	FString Text = Result.ToDisplayString();
	// Node ids are readable but the editor can also select the nodes behind the first error.
	if (!Result.bSuccess && GraphEditor.IsValid())
	{
		GraphEditor->ClearSelectionSet();
		UEdGraphNode* FirstNode = nullptr;
		for (const FHTNestedToggleIssue& Issue : Result.Errors)
		{
			for (const FString& NodeId : Issue.NodeIds)
			{
				if (UEdGraphNode* Node = FHTToggleGraphCompiler::FindNodeById(Asset, NodeId))
				{
					GraphEditor->SetNodeSelection(Node, true);
					FirstNode = FirstNode ? FirstNode : Node;
				}
			}
		}
		if (FirstNode)
		{
			GraphEditor->JumpToNode(FirstNode, false, false);
		}
	}
	SetResultText(Text, !Result.bSuccess);

	FNotificationInfo Info(Result.bSuccess
		? LOCTEXT("CompileSucceeded", "HT toggle tree generated.")
		: LOCTEXT("CompileFailed", "HT toggle tree compile failed. See the result tab."));
	Info.ExpireDuration = Result.bSuccess ? 4.0f : 8.0f;
	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(Result.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

FGraphPanelSelectionSet FHTToggleGraphEditor::GetSelectedNodes() const
{
	return GraphEditor.IsValid() ? GraphEditor->GetSelectedNodes() : FGraphPanelSelectionSet();
}

void FHTToggleGraphEditor::SelectAllNodes()
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->SelectAllNodes();
	}
}

void FHTToggleGraphEditor::DeleteSelectedNodes()
{
	if (!GraphEditor.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(FGenericCommands::Get().Delete->GetDescription());
	GraphEditor->GetCurrentGraph()->Modify();
	const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
	GraphEditor->ClearSelectionSet();
	for (UObject* Object : SelectedNodes)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object); Node && Node->CanUserDeleteNode())
		{
			Node->Modify();
			Node->DestroyNode();
		}
	}
}

bool FHTToggleGraphEditor::CanDeleteNodes() const
{
	for (UObject* Object : GetSelectedNodes())
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(Object); Node && Node->CanUserDeleteNode())
		{
			return true;
		}
	}
	return false;
}

void FHTToggleGraphEditor::CopySelectedNodes()
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TIterator It(SelectedNodes); It; ++It)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*It);
		if (!Node || !Node->CanDuplicateNode())
		{
			It.RemoveCurrent();
			continue;
		}
		Node->PrepareForCopying();
	}
	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(SelectedNodes, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FHTToggleGraphEditor::CanCopyNodes() const
{
	for (UObject* Object : GetSelectedNodes())
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(Object); Node && Node->CanDuplicateNode())
		{
			return true;
		}
	}
	return false;
}

void FHTToggleGraphEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	DeleteSelectedNodes();
}

bool FHTToggleGraphEditor::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void FHTToggleGraphEditor::PasteNodes()
{
	if (!GraphEditor.IsValid())
	{
		return;
	}
	const FVector2f Location = GraphEditor->GetPasteLocation2f();
	const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());
	UEdGraph* Graph = GraphEditor->GetCurrentGraph();
	Graph->Modify();
	GraphEditor->ClearSelectionSet();

	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(Graph, TextToImport, PastedNodes);

	FVector2f Average(0.0f, 0.0f);
	for (const UEdGraphNode* Node : PastedNodes)
	{
		Average.X += Node->NodePosX;
		Average.Y += Node->NodePosY;
	}
	if (!PastedNodes.IsEmpty())
	{
		Average /= static_cast<float>(PastedNodes.Num());
	}
	for (UEdGraphNode* Node : PastedNodes)
	{
		GraphEditor->SetNodeSelection(Node, true);
		Node->NodePosX = FMath::RoundToInt(Node->NodePosX - Average.X + Location.X);
		Node->NodePosY = FMath::RoundToInt(Node->NodePosY - Average.Y + Location.Y);
		Node->SnapToGrid(16);
		Node->CreateNewGuid();
	}
	GraphEditor->NotifyGraphChanged();
	Asset->MarkPackageDirty();
}

bool FHTToggleGraphEditor::CanPasteNodes() const
{
	if (!GraphEditor.IsValid())
	{
		return false;
	}
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	return FEdGraphUtilities::CanImportNodesFromText(GraphEditor->GetCurrentGraph(), ClipboardContent);
}

void FHTToggleGraphEditor::DuplicateNodes()
{
	CopySelectedNodes();
	PasteNodes();
}

bool FHTToggleGraphEditor::CanDuplicateNodes() const
{
	return CanCopyNodes();
}

#undef LOCTEXT_NAMESPACE
