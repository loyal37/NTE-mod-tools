#include "HTToggleGraphCompiler.h"

#include "Algo/Reverse.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphNodes.h"

namespace HTToggleGraphCompiler
{
	struct FStep
	{
		const UHTToggleGraphNode_Switch* Switch = nullptr;
		int32 Option = 0;
	};

	// Follows one output pin to the generate node. Returns false when the chain ends elsewhere.
	// OutSteps lists (switch, option) from the root down to the attachment of the output's node.
	static bool ResolveAttachment(const UEdGraphPin* OutputPin, TArray<FStep>& OutSteps, TSet<const UEdGraphNode*>& Visited)
	{
		OutSteps.Reset();
		const UEdGraphPin* Cursor = OutputPin;
		while (Cursor)
		{
			if (Cursor->LinkedTo.IsEmpty() || !Cursor->LinkedTo[0])
			{
				return false;
			}
			const UEdGraphPin* Target = Cursor->LinkedTo[0];
			const UEdGraphNode* TargetNode = Target->GetOwningNode();
			if (!TargetNode || Visited.Contains(TargetNode))
			{
				return false;
			}
			Visited.Add(TargetNode);
			if (TargetNode->IsA<UHTToggleGraphNode_Generate>())
			{
				Algo::Reverse(OutSteps);
				return true;
			}
			if (const UHTToggleGraphNode_Group* Group = Cast<UHTToggleGraphNode_Group>(TargetNode))
			{
				Cursor = Group->GetOutputPin();
				continue;
			}
			if (const UHTToggleGraphNode_Switch* Switch = Cast<UHTToggleGraphNode_Switch>(TargetNode))
			{
				const int32 Option = Switch->GetOptionIndex(Target);
				if (Option == INDEX_NONE)
				{
					return false;
				}
				FStep& Step = OutSteps.AddDefaulted_GetRef();
				Step.Switch = Switch;
				Step.Option = Option;
				Cursor = Switch->GetOutputPin();
				continue;
			}
			return false;
		}
		return false;
	}
}

bool FHTToggleGraphCompiler::CompileToModel(const UHTToggleGraph* Asset, FHTNestedToggleGraph& OutModel, TArray<FHTNestedToggleIssue>& OutErrors, TArray<FString>& OutWarnings)
{
	using namespace HTToggleGraphCompiler;
	OutModel = FHTNestedToggleGraph();
	if (!Asset || !Asset->Graph)
	{
		FHTNestedToggleIssue& Issue = OutErrors.AddDefaulted_GetRef();
		Issue.Message = TEXT("The toggle graph asset has no graph.");
		return false;
	}
	OutModel.Name = Asset->GetEffectiveTreeName();

	int32 GenerateCount = 0;
	for (const UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		GenerateCount += Node && Node->IsA<UHTToggleGraphNode_Generate>() ? 1 : 0;
	}
	if (GenerateCount == 0)
	{
		FHTNestedToggleIssue& Issue = OutErrors.AddDefaulted_GetRef();
		Issue.Message = TEXT("The graph has no generate node. Add one and connect the tree to it.");
		return false;
	}

	for (UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		if (const UHTToggleGraphNode_Switch* Switch = Cast<UHTToggleGraphNode_Switch>(Node))
		{
			TArray<FStep> Steps;
			TSet<const UEdGraphNode*> Visited = { Switch };
			if (!ResolveAttachment(Switch->GetOutputPin(), Steps, Visited))
			{
				OutWarnings.Add(FString::Printf(TEXT("Switch %s is not connected to the generate node and was ignored."), *Switch->GetNodeId()));
				continue;
			}
			FHTNestedToggleSwitch& Model = OutModel.Switches.AddDefaulted_GetRef();
			Model.NodeId = Switch->GetNodeId();
			Model.Label = Switch->Label.TrimStartAndEnd();
			// Empty stays empty: the generator derives <TreeName>_<Key> so same-key switches share.
			Model.VariableName = Switch->VariableName.TrimStartAndEnd();
			Model.KeyName = Switch->KeyName.TrimStartAndEnd();
			Model.OptionCount = Switch->OptionCount;
			Model.InitialOption = Switch->InitialOption;
			Model.OptionLabels = Switch->OptionLabels;
			if (!Steps.IsEmpty())
			{
				Model.ParentNodeId = Steps.Last().Switch->GetNodeId();
				Model.ParentOption = Steps.Last().Option;
			}
		}
	}

	for (UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		if (const UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(Node))
		{
			TArray<FStep> Steps;
			TSet<const UEdGraphNode*> Visited = { Part };
			if (!ResolveAttachment(Part->GetOutputPin(), Steps, Visited))
			{
				OutWarnings.Add(FString::Printf(TEXT("Part %s is not connected to the generate node and was ignored."), *Part->GetNodeId()));
				continue;
			}
			FHTNestedTogglePart& Model = OutModel.Parts.AddDefaulted_GetRef();
			Model.NodeId = Part->GetNodeId();
			Model.MaterialIDs = Part->MaterialIDs;
			for (const FStep& Step : Steps)
			{
				FHTNestedTogglePathStep& PathStep = Model.Path.AddDefaulted_GetRef();
				PathStep.SwitchNodeId = Step.Switch->GetNodeId();
				PathStep.Option = Step.Option;
			}
		}
	}

	if (OutModel.Parts.IsEmpty())
	{
		FHTNestedToggleIssue& Issue = OutErrors.AddDefaulted_GetRef();
		Issue.Message = TEXT("No part is connected to the generate node.");
		return false;
	}
	return true;
}

FHTNestedToggleGeneratorResult FHTToggleGraphCompiler::Generate(UHTToggleGraph* Asset, const bool bSaveAssets)
{
	FHTNestedToggleGeneratorResult Result;
	TArray<FString> Warnings;
	FHTNestedToggleGeneratorParams Params;
	if (!CompileToModel(Asset, Params.Graph, Result.Errors, Warnings))
	{
		Result.Messages.Append(Warnings);
		return Result;
	}
	FHTNestedToggleCompiledGraph Compiled;
	if (!FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Params.Graph, Compiled, Result.Errors))
	{
		Result.Messages.Append(Warnings);
		return Result;
	}
	if (Asset->AnimBlueprint.IsNull() || Asset->SaveGameBlueprint.IsNull())
	{
		FHTNestedToggleIssue& Issue = Result.Errors.AddDefaulted_GetRef();
		Issue.Message = TEXT("Set the Animation Blueprint and SaveGame Blueprint on the asset (select nothing to edit the asset).");
		Result.Messages.Append(Warnings);
		return Result;
	}
	Params.AnimBlueprintPath = Asset->AnimBlueprint.ToSoftObjectPath().ToString();
	Params.SaveGameBlueprintPath = Asset->SaveGameBlueprint.ToSoftObjectPath().ToString();
	Params.CharacterName = Asset->CharacterName.TrimStartAndEnd();
	Params.SectionIndex = Asset->SectionIndex;
	Params.LODIndex = Asset->LODIndex;
	Params.bSaveAssets = bSaveAssets;
	Result = FHTBlueprintToggleGenerator::GenerateNestedVisibility(Params);
	Result.Messages.Append(Warnings);
	return Result;
}

UEdGraphNode* FHTToggleGraphCompiler::FindNodeById(const UHTToggleGraph* Asset, const FString& NodeId)
{
	if (!Asset || !Asset->Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		if (const UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node); ToggleNode && ToggleNode->GetNodeId() == NodeId)
		{
			return Node;
		}
	}
	return nullptr;
}
