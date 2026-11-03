#pragma once

#include "CoreMinimal.h"
#include "HTBlueprintToggleGenerator.h"

class UEdGraphNode;
class UHTToggleGraph;

// Converts a UHTToggleGraph into the FHTNestedToggleGraph condition model and runs the
// nested visibility generator. Node ids in errors are UHTToggleGraphNode::GetNodeId().
class FHTToggleGraphCompiler
{
public:
	// Walks every node attached to a generate node. Unattached nodes are listed in OutWarnings.
	static bool CompileToModel(const UHTToggleGraph* Asset, FHTNestedToggleGraph& OutModel, TArray<FHTNestedToggleIssue>& OutErrors, TArray<FString>& OutWarnings);
	// CompileToModel + CompileNestedToggleGraph validation + GenerateNestedVisibility.
	static FHTNestedToggleGeneratorResult Generate(UHTToggleGraph* Asset, bool bSaveAssets);
	static UEdGraphNode* FindNodeById(const UHTToggleGraph* Asset, const FString& NodeId);
};
