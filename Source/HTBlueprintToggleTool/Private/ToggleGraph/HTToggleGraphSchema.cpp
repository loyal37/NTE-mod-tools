#include "HTToggleGraphSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "HTToggleGraphNodes.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "HTToggleGraphSchema"

UHTToggleGraphNode* FHTToggleGraphSchemaAction_NewNode::SpawnNode(UEdGraph* ParentGraph, TSubclassOf<UHTToggleGraphNode> InNodeClass, const FVector2f& Location, UEdGraphPin* FromPin, bool bSelectNewNode)
{
	if (!ParentGraph || !InNodeClass)
	{
		return nullptr;
	}
	ParentGraph->Modify();
	UHTToggleGraphNode* Node = NewObject<UHTToggleGraphNode>(ParentGraph, InNodeClass, NAME_None, RF_Transactional);
	Node->SetFlags(RF_Transactional);
	Node->CreateNewGuid();
	Node->NodePosX = FMath::RoundToInt(Location.X);
	Node->NodePosY = FMath::RoundToInt(Location.Y);
	ParentGraph->AddNode(Node, true, bSelectNewNode);
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Node->SnapToGrid(16);
	if (FromPin)
	{
		Node->AutowireNewNode(FromPin);
	}
	ParentGraph->NotifyGraphChanged();
	return Node;
}

UEdGraphNode* FHTToggleGraphSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode)
{
	const FScopedTransaction Transaction(LOCTEXT("AddToggleGraphNode", "Add Toggle Graph Node"));
	return SpawnNode(ParentGraph, NodeClass, Location, FromPin, bSelectNewNode);
}

void UHTToggleGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	ContextMenuBuilder.AddAction(MakeShared<FHTToggleGraphSchemaAction_NewNode>(
		UHTToggleGraphNode_Part::StaticClass(),
		LOCTEXT("AddPart", "Part"),
		LOCTEXT("AddPartTooltip", "Material IDs that show together. Connect its output to a switch option, a group or the generate node, and to any number of Texture Switch targets and Material Switch slots.")));
	ContextMenuBuilder.AddAction(MakeShared<FHTToggleGraphSchemaAction_NewNode>(
		UHTToggleGraphNode_Switch::StaticClass(),
		LOCTEXT("AddSwitch", "Switch"),
		LOCTEXT("AddSwitchTooltip", "A key-driven switch. Parts or other switches hang on its options; its output hangs on a parent option, a group or the generate node.")));
	ContextMenuBuilder.AddAction(MakeShared<FHTToggleGraphSchemaAction_NewNode>(
		UHTToggleGraphNode_TextureSwitch::StaticClass(),
		LOCTEXT("AddTextureSwitch", "Texture Switch"),
		LOCTEXT("AddTextureSwitchTooltip", "A key-driven switch that sets one texture parameter on the slots of its Target parts, one texture per option. Its output hangs on a switch option, a group or the generate node.")));
	ContextMenuBuilder.AddAction(MakeShared<FHTToggleGraphSchemaAction_NewNode>(
		UHTToggleGraphNode_MaterialSwitch::StaticClass(),
		LOCTEXT("AddMaterialSwitch", "Material Switch"),
		LOCTEXT("AddMaterialSwitchTooltip", "A key-driven switch that puts one material per option on the slots of its parts. Empty options restore the original material.")));
	ContextMenuBuilder.AddAction(MakeShared<FHTToggleGraphSchemaAction_NewNode>(
		UHTToggleGraphNode_Group::StaticClass(),
		LOCTEXT("AddGroup", "Group"),
		LOCTEXT("AddGroupTooltip", "Merges several connections into one.")));
	ContextMenuBuilder.AddAction(MakeShared<FHTToggleGraphSchemaAction_NewNode>(
		UHTToggleGraphNode_Generate::StaticClass(),
		LOCTEXT("AddGenerate", "Generate"),
		LOCTEXT("AddGenerateTooltip", "Root of the tree. Everything connected to it is compiled into the Animation Blueprint.")));
}

bool UHTToggleGraphSchema::ReachesDownstream(const UEdGraphNode* Start, const UEdGraphNode* Target)
{
	TArray<const UEdGraphNode*> Pending = { Start };
	TSet<const UEdGraphNode*> Visited;
	while (!Pending.IsEmpty())
	{
		const UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
		if (!Node || Visited.Contains(Node))
		{
			continue;
		}
		if (Node == Target)
		{
			return true;
		}
		Visited.Add(Node);
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* Link : Pin->LinkedTo)
				{
					if (Link)
					{
						Pending.Add(Link->GetOwningNode());
					}
				}
			}
		}
	}
	return false;
}

bool UHTToggleGraphSchema::IsEffectInput(const UEdGraphPin* Pin)
{
	const UEdGraphNode* Owner = Pin ? Pin->GetOwningNodeUnchecked() : nullptr;
	return Pin && Pin->Direction == EGPD_Input && Owner
		&& (Owner->IsA<UHTToggleGraphNode_TextureSwitch>() || Owner->IsA<UHTToggleGraphNode_MaterialSwitch>());
}

const FPinConnectionResponse UHTToggleGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (!A || !B || A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SameNode", "Cannot connect a node to itself."));
	}
	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SameDirection", "Connect an output to an input."));
	}
	if (A->PinType.PinCategory != UHTToggleGraphNode::PinCategory || B->PinType.PinCategory != UHTToggleGraphNode::PinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ForeignPin", "Only toggle graph pins can be connected."));
	}
	const UEdGraphPin* Output = A->Direction == EGPD_Output ? A : B;
	const UEdGraphPin* Input = A->Direction == EGPD_Output ? B : A;
	// The input side must not already lead back to the output's node.
	if (ReachesDownstream(Input->GetOwningNode(), Output->GetOwningNode()))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Cycle", "This connection would form a loop."));
	}
	const bool bFromPart = Output->GetOwningNode()->IsA<UHTToggleGraphNode_Part>();
	if (IsEffectInput(Input))
	{
		if (!bFromPart)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("PartsOnly", "Only parts connect to Target and Slots inputs."));
		}
		// One part per input; a part can still feed any number of other inputs.
		if (!Input->LinkedTo.IsEmpty())
		{
			return FPinConnectionResponse(A == Input ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("ReplaceTarget", "Replace the part on this input."));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}
	if (bFromPart)
	{
		// A part shows through one path only: TryCreateConnection moves its visibility link and
		// keeps its Target / Slots links.
		for (const UEdGraphPin* Linked : Output->LinkedTo)
		{
			if (Linked && !IsEffectInput(Linked))
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("MoveVisibility", "Move the part's visibility connection here."));
			}
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
	}
	// A switch or group can hang on one place only; replacing the old link keeps one path.
	if (!Output->LinkedTo.IsEmpty())
	{
		return FPinConnectionResponse(A == Output ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("ReplaceParent", "Replace the existing connection."));
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

bool UHTToggleGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	UEdGraphPin* Output = A && A->Direction == EGPD_Output ? A : B;
	UEdGraphPin* Input = Output == A ? B : A;
	if (Output && Input && Output != Input && Output->GetOwningNodeUnchecked() && Output->GetOwningNode()->IsA<UHTToggleGraphNode_Part>()
		&& !IsEffectInput(Input) && CanCreateConnection(A, B).Response == CONNECT_RESPONSE_MAKE)
	{
		const TArray<UEdGraphPin*> Links = Output->LinkedTo;
		for (UEdGraphPin* Linked : Links)
		{
			if (Linked && Linked != Input && !IsEffectInput(Linked))
			{
				BreakSinglePinLink(Output, Linked);
			}
		}
	}
	return Super::TryCreateConnection(A, B);
}

FLinearColor UHTToggleGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor(0.95f, 0.65f, 0.25f);
}

void UHTToggleGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	FHTToggleGraphSchemaAction_NewNode::SpawnNode(&Graph, UHTToggleGraphNode_Generate::StaticClass(), FVector2f(600.0f, 0.0f), nullptr, false);
}

#undef LOCTEXT_NAMESPACE
