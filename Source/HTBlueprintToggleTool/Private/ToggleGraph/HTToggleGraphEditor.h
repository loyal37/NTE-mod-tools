#pragma once

#include "CoreMinimal.h"
#include "GraphEditor.h"
#include "EditorUndoClient.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class STextBlock;
class UHTToggleGraph;

// Asset editor for UHTToggleGraph: graph canvas, details of the selected nodes (or the asset),
// a result panel, and a toolbar "Compile" button that generates the Animation Blueprint nodes.
class FHTToggleGraphEditor : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	virtual ~FHTToggleGraphEditor() override;

	//~ FEditorUndoClient: rebuild node widgets after undo/redo of inline edits.
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	static const FName GraphTabId;
	static const FName DetailsTabId;
	static const FName ResultTabId;
	static const FName AppIdentifier;

	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UHTToggleGraph* InAsset);

	//~ FAssetEditorToolkit
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	// Runs the compiler and reports into the result tab; selects offending nodes on error.
	void Compile();

	// Clipboard paths shared by Copy, Cut, Paste and Duplicate (and the automation tests, which
	// must not touch the system clipboard). Text uses the engine's node export format.
	static FString ExportNodesToText(const FGraphPanelSelectionSet& Nodes);
	// One undoable "Paste" transaction: imports the nodes into Graph centred on Location, gives
	// them new GUIDs, rebuilds their pins from the links that survived the import and keeps one
	// empty trailing input. Links to nodes that were not copied are dropped. Returns the new nodes.
	static TArray<UEdGraphNode*> PasteNodesFromText(UEdGraph* Graph, const FString& Text, const FVector2f& Location);
	// One undoable "Delete" transaction for every deletable node in Nodes.
	static void DeleteNodesFromGraph(UEdGraph* Graph, const FGraphPanelSelectionSet& Nodes);

private:
	TSharedRef<SDockTab> SpawnGraphTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnResultTab(const FSpawnTabArgs& Args);
	void BindGraphCommands();
	void ExtendToolbar();
	void OnSelectedNodesChanged(const TSet<UObject*>& Selection);
	void OnNodeDoubleClicked(UEdGraphNode* Node);
	void SetResultText(const FString& Text, bool bError);

	FGraphPanelSelectionSet GetSelectedNodes() const;
	void SelectAllNodes();
	bool CanSelectAllNodes() const { return true; }
	void DeleteSelectedNodes();
	bool CanDeleteNodes() const;
	void CopySelectedNodes();
	bool CanCopyNodes() const;
	void CutSelectedNodes();
	bool CanCutNodes() const;
	void PasteNodes();
	bool CanPasteNodes() const;
	void DuplicateNodes();
	bool CanDuplicateNodes() const;

	UHTToggleGraph* Asset = nullptr;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<STextBlock> ResultText;
	TSharedPtr<FUICommandList> GraphEditorCommands;
};
