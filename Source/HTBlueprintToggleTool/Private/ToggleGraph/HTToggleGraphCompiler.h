#pragma once

#include "CoreMinimal.h"
#include "HTBlueprintToggleGenerator.h"

class UEdGraphNode;
class UHTToggleGraph;

// Every Part's saved slot names looked up on the current preview mesh.
struct FHTPartSlotReport
{
	// IDs moved onto their slot again, e.g. "Part 10: ID 10 -> 11 (slot sleeve)".
	TArray<FString> Changes;
	// Parts that had no slot names yet and were bound to the slots their IDs point at now.
	TArray<FString> Bindings;
	// Slot names missing from the mesh or used by several slots, and (on Compile) IDs that moved.
	TArray<FHTNestedToggleIssue> Errors;
	int32 ChangedParts = 0;

	FString ToDisplayString() const;
};

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
	// Compile's check: changes nothing; an ID that moved is an error, Bindings lists unbound parts.
	static FHTPartSlotReport CheckPartSlots(const UHTToggleGraph* Asset);
	// The Refresh button: moves IDs back onto their slots and binds unbound parts, in one undoable
	// transaction. Parts whose slot name is missing or not unique keep their IDs.
	static FHTPartSlotReport RefreshPartSlots(UHTToggleGraph* Asset);
};
