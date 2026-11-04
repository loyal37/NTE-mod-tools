#include "HTToggleGraphEditor.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
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
#include "SGraphPanel.h"
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

FHTToggleGraphZoomLevels::FHTToggleGraphZoomLevels()
{
	auto Add = [this](const float Amount, const TCHAR* Text, const EGraphRenderingLOD::Type LOD)
	{
		Entries.Add({ Amount, FText::FromString(Text), LOD });
	};
	// Below 1:1: the engine's own steps.
	Add(0.100f, TEXT("-12"), EGraphRenderingLOD::LowestDetail);
	Add(0.125f, TEXT("-11"), EGraphRenderingLOD::LowestDetail);
	Add(0.150f, TEXT("-10"), EGraphRenderingLOD::LowestDetail);
	Add(0.175f, TEXT("-9"), EGraphRenderingLOD::LowestDetail);
	Add(0.200f, TEXT("-8"), EGraphRenderingLOD::LowestDetail);
	Add(0.225f, TEXT("-7"), EGraphRenderingLOD::LowDetail);
	Add(0.250f, TEXT("-6"), EGraphRenderingLOD::LowDetail);
	Add(0.375f, TEXT("-5"), EGraphRenderingLOD::MediumDetail);
	Add(0.500f, TEXT("-4"), EGraphRenderingLOD::MediumDetail);
	Add(0.675f, TEXT("-3"), EGraphRenderingLOD::MediumDetail);
	Add(0.750f, TEXT("-2"), EGraphRenderingLOD::DefaultDetail);
	Add(0.875f, TEXT("-1"), EGraphRenderingLOD::DefaultDetail);
	DefaultLevel = Entries.Num();
	Add(1.000f, TEXT("1:1"), EGraphRenderingLOD::DefaultDetail);
	// Above 1:1: larger steps up to 4x.
	Add(1.250f, TEXT("+1"), EGraphRenderingLOD::DefaultDetail);
	Add(1.500f, TEXT("+2"), EGraphRenderingLOD::FullyZoomedIn);
	Add(1.750f, TEXT("+3"), EGraphRenderingLOD::FullyZoomedIn);
	Add(2.000f, TEXT("+4"), EGraphRenderingLOD::FullyZoomedIn);
	Add(2.500f, TEXT("+5"), EGraphRenderingLOD::FullyZoomedIn);
	Add(3.000f, TEXT("+6"), EGraphRenderingLOD::FullyZoomedIn);
	Add(3.500f, TEXT("+7"), EGraphRenderingLOD::FullyZoomedIn);
	Add(4.000f, TEXT("+8"), EGraphRenderingLOD::FullyZoomedIn);
}

float FHTToggleGraphZoomLevels::GetZoomAmount(const int32 InZoomLevel) const
{
	return Entries[FMath::Clamp(InZoomLevel, 0, Entries.Num() - 1)].Amount;
}

int32 FHTToggleGraphZoomLevels::GetNearestZoomLevel(const float InZoomAmount) const
{
	int32 Nearest = 0;
	for (int32 Level = 1; Level < Entries.Num(); ++Level)
	{
		if (FMath::Abs(Entries[Level].Amount - InZoomAmount) < FMath::Abs(Entries[Nearest].Amount - InZoomAmount))
		{
			Nearest = Level;
		}
	}
	return Nearest;
}

FText FHTToggleGraphZoomLevels::GetZoomText(const int32 InZoomLevel) const
{
	return Entries[FMath::Clamp(InZoomLevel, 0, Entries.Num() - 1)].Text;
}

int32 FHTToggleGraphZoomLevels::GetNumZoomLevels() const
{
	return Entries.Num();
}

int32 FHTToggleGraphZoomLevels::GetDefaultZoomLevel() const
{
	return DefaultLevel;
}

EGraphRenderingLOD::Type FHTToggleGraphZoomLevels::GetLOD(const int32 InZoomLevel) const
{
	return Entries[FMath::Clamp(InZoomLevel, 0, Entries.Num() - 1)].LOD;
}

void FHTToggleGraphEditor::ApplyZoomLevels(SGraphEditor& Editor)
{
	if (SGraphPanel* Panel = Editor.GetGraphPanel())
	{
		Panel->SetZoomLevelsContainer<FHTToggleGraphZoomLevels>();
	}
}

FHTToggleGraphEditor::~FHTToggleGraphEditor()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void FHTToggleGraphEditor::PostUndo(bool bSuccess)
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->ClearSelectionSet();
		GraphEditor->NotifyGraphChanged();
	}
	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}
}

void FHTToggleGraphEditor::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void FHTToggleGraphEditor::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UHTToggleGraph* InAsset)
{
	Asset = InAsset;
	Asset->EnsureGraph();
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

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
	ApplyZoomLevels(*GraphEditor);

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
		.SetDisplayName(LOCTEXT("GraphTab", "Graph"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FHTToggleGraphEditor::SpawnDetailsTab))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
	InTabManager->RegisterTabSpawner(ResultTabId, FOnSpawnTab::CreateSP(this, &FHTToggleGraphEditor::SpawnResultTab))
		.SetDisplayName(LOCTEXT("ResultTab", "Result"))
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
		.Label(LOCTEXT("GraphTabLabel", "Graph"))
		[
			GraphEditor.ToSharedRef()
		];
}

TSharedRef<SDockTab> FHTToggleGraphEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			DetailsView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FHTToggleGraphEditor::SpawnResultTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ResultTabLabel", "Result"))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8)
			[
				SAssignNew(ResultText, STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ResultHint", "Draw the tree, then press Compile to generate the Animation Blueprint nodes."))
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
				LOCTEXT("CompileButton", "Compile"),
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

void FHTToggleGraphEditor::DeleteNodesFromGraph(UEdGraph* Graph, const FGraphPanelSelectionSet& Nodes)
{
	if (!Graph)
	{
		return;
	}
	const FScopedTransaction Transaction(FGenericCommands::Get().Delete->GetDescription());
	Graph->Modify();
	for (UObject* Object : Nodes)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object); Node && Node->GetGraph() == Graph && Node->CanUserDeleteNode())
		{
			Node->Modify();
			Node->DestroyNode();
		}
	}
}

void FHTToggleGraphEditor::DeleteSelectedNodes()
{
	if (!GraphEditor.IsValid())
	{
		return;
	}
	const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
	GraphEditor->ClearSelectionSet();
	DeleteNodesFromGraph(GraphEditor->GetCurrentGraph(), SelectedNodes);
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

FString FHTToggleGraphEditor::ExportNodesToText(const FGraphPanelSelectionSet& Nodes)
{
	FGraphPanelSelectionSet Exported;
	for (UObject* Object : Nodes)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object); Node && Node->CanDuplicateNode())
		{
			Node->PrepareForCopying();
			Exported.Add(Node);
		}
	}
	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(Exported, ExportedText);
	return ExportedText;
}

void FHTToggleGraphEditor::CopySelectedNodes()
{
	FPlatformApplicationMisc::ClipboardCopy(*ExportNodesToText(GetSelectedNodes()));
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

TArray<UEdGraphNode*> FHTToggleGraphEditor::PasteNodesFromText(UEdGraph* Graph, const FString& Text, const FVector2f& Location)
{
	TArray<UEdGraphNode*> Result;
	if (!Graph || !FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
	{
		return Result;
	}
	const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());
	Graph->Modify();
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, PastedNodes);

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
		Node->NodePosX = FMath::RoundToInt(Node->NodePosX - Average.X + Location.X);
		Node->NodePosY = FMath::RoundToInt(Node->NodePosY - Average.Y + Location.Y);
		Node->SnapToGrid(16);
		Node->CreateNewGuid();
	}
	// Links to nodes outside the clipboard are only cleared at the very end of the import, so
	// rebuild the pins once more from the links that survived, then keep exactly one empty input.
	for (UEdGraphNode* Node : PastedNodes)
	{
		if (UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node))
		{
			ToggleNode->ReconstructNode();
		}
	}
	for (UEdGraphNode* Node : PastedNodes)
	{
		if (UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node))
		{
			ToggleNode->NormalizeDynamicInputs(true);
		}
	}
	Graph->NotifyGraphChanged();
	Graph->MarkPackageDirty();
	Result = PastedNodes.Array();
	return Result;
}

void FHTToggleGraphEditor::PasteNodes()
{
	if (!GraphEditor.IsValid())
	{
		return;
	}
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	GraphEditor->ClearSelectionSet();
	for (UEdGraphNode* Node : PasteNodesFromText(GraphEditor->GetCurrentGraph(), TextToImport, GraphEditor->GetPasteLocation2f()))
	{
		GraphEditor->SetNodeSelection(Node, true);
	}
	GraphEditor->NotifyGraphChanged();
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
