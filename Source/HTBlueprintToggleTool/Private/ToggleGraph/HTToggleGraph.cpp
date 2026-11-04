#include "HTToggleGraph.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

UHTToggleGraph* UHTToggleEdGraph::GetToggleGraphAsset() const
{
	return GetTypedOuter<UHTToggleGraph>();
}

UHTToggleGraph::UHTToggleGraph()
{
}

UEdGraph* UHTToggleGraph::EnsureGraph()
{
	if (!Graph)
	{
		UHTToggleEdGraph* NewGraph = NewObject<UHTToggleEdGraph>(this, TEXT("ToggleGraph"), RF_Transactional);
		NewGraph->Schema = UHTToggleGraphSchema::StaticClass();
		NewGraph->GetSchema()->CreateDefaultNodesForGraph(*NewGraph);
		Graph = NewGraph;
	}
	// Older graphs may lack the trailing empty input on groups and the generate node.
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node))
		{
			ToggleNode->NormalizeDynamicInputs(false);
		}
	}
	return Graph;
}

FString UHTToggleGraph::GetEffectiveTreeName() const
{
	const FString Trimmed = TreeName.TrimStartAndEnd();
	return Trimmed.IsEmpty() ? GetName() : Trimmed;
}

UAnimBlueprint* UHTToggleGraph::LoadAnimBlueprint() const
{
	return AnimBlueprint.IsNull() ? nullptr : AnimBlueprint.LoadSynchronous();
}

USkeletalMesh* UHTToggleGraph::ResolvePreviewMesh() const
{
	UAnimBlueprint* Anim = LoadAnimBlueprint();
	if (!Anim)
	{
		return nullptr;
	}
	USkeletalMesh* Mesh = Anim->GetPreviewMesh(false);
	if (!Mesh && Anim->TargetSkeleton)
	{
		Mesh = Anim->TargetSkeleton->GetPreviewMesh(true);
	}
	return Mesh;
}
