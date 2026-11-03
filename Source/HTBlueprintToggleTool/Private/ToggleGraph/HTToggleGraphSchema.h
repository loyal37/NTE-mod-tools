#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "Templates/SubclassOf.h"
#include "HTToggleGraphSchema.generated.h"

class UHTToggleGraphNode;

// Right-click action that places one toggle graph node.
USTRUCT()
struct FHTToggleGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UHTToggleGraphNode> NodeClass;

	FHTToggleGraphSchemaAction_NewNode() : FEdGraphSchemaAction() {}
	FHTToggleGraphSchemaAction_NewNode(TSubclassOf<UHTToggleGraphNode> InNodeClass, FText InMenuDesc, FText InToolTip)
		: FEdGraphSchemaAction(FText(), MoveTemp(InMenuDesc), MoveTemp(InToolTip), 0)
		, NodeClass(InNodeClass)
	{}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override;

	// Shared by the action, the editor and tests.
	static UHTToggleGraphNode* SpawnNode(UEdGraph* ParentGraph, TSubclassOf<UHTToggleGraphNode> InNodeClass, const FVector2f& Location, UEdGraphPin* FromPin = nullptr, bool bSelectNewNode = true);
};

UCLASS()
class UHTToggleGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	//~ UEdGraphSchema
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
	virtual bool ShouldHidePinDefaultValue(UEdGraphPin* Pin) const override { return true; }

	// True when following output links from Start reaches Target (used to refuse cycles).
	static bool ReachesDownstream(const UEdGraphNode* Start, const UEdGraphNode* Target);
};
