#pragma once

#include "CoreMinimal.h"
#include "GraphEditor.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class STextBlock;
class UHTToggleGraph;

// Asset editor for UHTToggleGraph: graph canvas, details of the selected nodes (or the asset),
// a result panel, and a toolbar "编译" button that generates the Animation Blueprint nodes.
class FHTToggleGraphEditor : public FAssetEditorToolkit
{
public:
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
