#include "HTToggleGraphCompiler.h"

#include "Algo/Reverse.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"

namespace HTToggleGraphCompiler
{
	struct FStep
	{
		const UHTToggleGraphNode_Switch* Switch = nullptr;
		int32 Option = 0;
	};

	// Inputs a part, switch or group can hang on for visibility (and nesting): a switch option,
	// a group or the generate node. Texture Switch Target / Material Switch Slots inputs are not.
	static bool IsVisibilityInput(const UEdGraphPin* Pin)
	{
		return Pin && Pin->Direction == EGPD_Input && !UHTToggleGraphSchema::IsEffectInput(Pin);
	}

	// Follows a visibility link to the generate node. Returns false when the chain ends elsewhere.
	// OutSteps lists (switch, option) from the root down to the attachment of the link's start.
	static bool ResolveAttachment(const UEdGraphPin* FirstTarget, TArray<FStep>& OutSteps, TSet<const UEdGraphNode*>& Visited)
	{
		OutSteps.Reset();
		const UEdGraphPin* Target = FirstTarget;
		while (Target)
		{
			const UEdGraphNode* TargetNode = Target->GetOwningNodeUnchecked();
			if (!TargetNode || Visited.Contains(TargetNode) || !IsVisibilityInput(Target))
			{
				return false;
			}
			Visited.Add(TargetNode);
			if (TargetNode->IsA<UHTToggleGraphNode_Generate>())
			{
				Algo::Reverse(OutSteps);
				return true;
			}
			const UEdGraphPin* Next = nullptr;
			if (const UHTToggleGraphNode_Group* Group = Cast<UHTToggleGraphNode_Group>(TargetNode))
			{
				Next = Group->GetOutputPin();
			}
			else if (const UHTToggleGraphNode_Switch* Switch = Cast<UHTToggleGraphNode_Switch>(TargetNode))
			{
				const int32 Option = Switch->GetOptionIndex(Target);
				if (Option == INDEX_NONE)
				{
					return false;
				}
				FStep& Step = OutSteps.AddDefaulted_GetRef();
				Step.Switch = Switch;
				Step.Option = Option;
				Next = Switch->GetOutputPin();
			}
			else
			{
				return false;
			}
			Target = Next && !Next->LinkedTo.IsEmpty() ? Next->LinkedTo[0] : nullptr;
		}
		return false;
	}

	static TArray<const UEdGraphPin*> GetVisibilityLinks(const UEdGraphPin* OutputPin)
	{
		TArray<const UEdGraphPin*> Links;
		for (const UEdGraphPin* Linked : OutputPin ? OutputPin->LinkedTo : TArray<UEdGraphPin*>())
		{
			if (IsVisibilityInput(Linked))
			{
				Links.Add(Linked);
			}
		}
		return Links;
	}

	static FString PathOf(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
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
	auto AddIssue = [&OutErrors](TArray<FString> NodeIds, const FString& Message)
	{
		FHTNestedToggleIssue& Issue = OutErrors.AddDefaulted_GetRef();
		Issue.NodeIds = MoveTemp(NodeIds);
		Issue.Message = Message;
	};

	int32 GenerateCount = 0;
	for (const UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		GenerateCount += Node && Node->IsA<UHTToggleGraphNode_Generate>() ? 1 : 0;
	}
	if (GenerateCount == 0)
	{
		AddIssue({}, TEXT("The graph has no generate node. Add one and connect the tree to it."));
		return false;
	}

	// By material parts are looked up on the preview mesh again; warn once when it is missing.
	bool bWarnedMissingMesh = false;
	auto EffectiveIDs = [&](const UHTToggleGraphNode_Part* Part)
	{
		bool bMeshMissing = false;
		TArray<int32> IDs = Part->GetEffectiveMaterialIDs(&bMeshMissing);
		if (bMeshMissing && !bWarnedMissingMesh)
		{
			bWarnedMissingMesh = true;
			OutWarnings.Add(TEXT("Parts set By material kept their stored material IDs because the Anim Blueprint has no preview mesh."));
		}
		else if (!bMeshMissing && !Part->SourceMaterial.IsNull() && IDs.IsEmpty())
		{
			AddIssue({ Part->GetNodeId() }, FString::Printf(TEXT("Part %s picks slots by material %s, but the preview mesh no longer uses it."), *Part->GetNodeId(), *Part->SourceMaterial.GetAssetName()));
		}
		return IDs;
	};

	bool bHasEffects = false;
	for (UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		const UHTToggleGraphNode_SwitchBase* Switch = Cast<UHTToggleGraphNode_SwitchBase>(Node);
		if (!Switch)
		{
			continue;
		}
		TArray<FStep> Steps;
		TSet<const UEdGraphNode*> Visited = { Switch };
		const UEdGraphPin* Out = Switch->GetOutputPin();
		if (!Out || Out->LinkedTo.IsEmpty() || !ResolveAttachment(Out->LinkedTo[0], Steps, Visited))
		{
			OutWarnings.Add(FString::Printf(TEXT("%s is not connected to the generate node and was ignored."), *Switch->GetNodeId()));
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
		if (const UHTToggleGraphNode_TextureSwitch* Texture = Cast<UHTToggleGraphNode_TextureSwitch>(Switch))
		{
			bHasEffects = true;
			Model.Kind = EHTNestedToggleSwitchKind::Texture;
			Model.TextureParameterName = Texture->ParameterName.TrimStartAndEnd();
			for (int32 Input = 0; Input < Texture->GetDynamicInputCount(); ++Input)
			{
				const UHTToggleGraphNode_Part* Part = Texture->GetTargetPart(Input);
				if (!Part)
				{
					continue;
				}
				FHTNestedToggleTextureTarget& Target = Model.TextureTargets.AddDefaulted_GetRef();
				Target.PartNodeId = Part->GetNodeId();
				Target.MaterialIDs = EffectiveIDs(Part);
				Target.ParentMaterialPath = Part->SourceMaterial.IsNull() ? FString() : Part->SourceMaterial.ToSoftObjectPath().ToString();
				for (int32 Option = 0; Option < Texture->OptionCount; ++Option)
				{
					const TSoftObjectPtr<UTexture> TextureAsset = Texture->GetTargetTexture(Input, Option);
					Target.TexturePaths.Add(TextureAsset.IsNull() ? FString() : TextureAsset.ToSoftObjectPath().ToString());
				}
			}
		}
		else if (const UHTToggleGraphNode_MaterialSwitch* Material = Cast<UHTToggleGraphNode_MaterialSwitch>(Switch))
		{
			bHasEffects = true;
			Model.Kind = EHTNestedToggleSwitchKind::Material;
			for (int32 Input = 0; Input < Material->GetDynamicInputCount(); ++Input)
			{
				if (const UHTToggleGraphNode_Part* Part = Material->GetSlotsPart(Input))
				{
					Model.TargetPartNodeIds.AddUnique(Part->GetNodeId());
					for (const int32 ID : EffectiveIDs(Part))
					{
						Model.MaterialIDs.AddUnique(ID);
					}
				}
			}
			Model.MaterialIDs.Sort();
			for (int32 Option = 0; Option < Material->OptionCount; ++Option)
			{
				const TSoftObjectPtr<UMaterialInterface> MaterialAsset = Material->GetOptionMaterial(Option);
				Model.OptionMaterialPaths.Add(MaterialAsset.IsNull() ? FString() : MaterialAsset.ToSoftObjectPath().ToString());
			}
		}
	}

	for (UEdGraphNode* Node : Asset->Graph->Nodes)
	{
		const UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(Node);
		if (!Part)
		{
			continue;
		}
		const UEdGraphPin* Out = Part->GetOutputPin();
		const TArray<const UEdGraphPin*> VisibilityLinks = GetVisibilityLinks(Out);
		if (VisibilityLinks.Num() > 1)
		{
			AddIssue({ Part->GetNodeId() }, FString::Printf(TEXT("Part %s has %d visibility connections. A part can only show through one path; Texture and Material switch inputs do not count."), *Part->GetNodeId(), VisibilityLinks.Num()));
			continue;
		}
		if (VisibilityLinks.IsEmpty())
		{
			if (!Out || Out->LinkedTo.IsEmpty())
			{
				OutWarnings.Add(FString::Printf(TEXT("Part %s is not connected and was ignored."), *Part->GetNodeId()));
			}
			continue;
		}
		TArray<FStep> Steps;
		TSet<const UEdGraphNode*> Visited = { Part };
		if (!ResolveAttachment(VisibilityLinks[0], Steps, Visited))
		{
			OutWarnings.Add(FString::Printf(TEXT("Part %s is not connected to the generate node and was ignored."), *Part->GetNodeId()));
			continue;
		}
		FHTNestedTogglePart& Model = OutModel.Parts.AddDefaulted_GetRef();
		Model.NodeId = Part->GetNodeId();
		Model.MaterialIDs = EffectiveIDs(Part);
		for (const FStep& Step : Steps)
		{
			FHTNestedTogglePathStep& PathStep = Model.Path.AddDefaulted_GetRef();
			PathStep.SwitchNodeId = Step.Switch->GetNodeId();
			PathStep.Option = Step.Option;
		}
	}

	if (bHasEffects)
	{
		// Original slot materials: what an empty material option restores, and the material a
		// manual-mode texture target belongs to.
		const USkeletalMesh* Mesh = Asset->ResolvePreviewMesh();
		if (!Mesh)
		{
			AddIssue({}, TEXT("Texture and Material switches need the Anim Blueprint's preview mesh to know each slot's original material. Set a preview mesh on the Anim Blueprint."));
		}
		else
		{
			const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
			for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
			{
				OutModel.SlotMaterialPaths.Add(Slot, PathOf(Materials[Slot].MaterialInterface));
			}
		}
	}

	if (!OutErrors.IsEmpty())
	{
		return false;
	}
	if (OutModel.Parts.IsEmpty() && !bHasEffects)
	{
		AddIssue({}, TEXT("No part, Texture Switch or Material Switch is connected to the generate node."));
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
