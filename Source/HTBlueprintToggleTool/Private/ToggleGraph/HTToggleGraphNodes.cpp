#include "HTToggleGraphNodes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphSchema.h"
#include "Materials/MaterialInterface.h"
#include "MaterialTypes.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "HTToggleGraphNodes"

const FName UHTToggleGraphNode::PinCategory(TEXT("HTToggle"));
const FName UHTToggleGraphNode::OutputPinName(TEXT("Out"));
const FName UHTToggleGraphNode::InputPinName(TEXT("In"));

FName UHTToggleGraphNode::OptionPinName(const int32 Option)
{
	return FName(*FString::Printf(TEXT("Option_%d"), Option));
}

FString UHTToggleGraphNode::GetNodeId() const
{
	FString DisplayLabel = GetDisplayLabel();
	DisplayLabel.ReplaceInline(TEXT("#"), TEXT(" "));
	return FString::Printf(TEXT("%s#%s"), *DisplayLabel, *NodeGuid.ToString(EGuidFormats::Digits).Left(6));
}

UEdGraphPin* UHTToggleGraphNode::GetOutputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* UHTToggleGraphNode::GetInputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == InputPinName)
		{
			return Pin;
		}
	}
	return nullptr;
}

UHTToggleGraph* UHTToggleGraphNode::GetToggleGraphAsset() const
{
	return GetTypedOuter<UHTToggleGraph>();
}

bool UHTToggleGraphNode::SetLabelInline(const FString& NewLabel)
{
	FString* Target = GetMutableLabel();
	const FString Trimmed = NewLabel.TrimStartAndEnd();
	if (!Target || *Target == Trimmed)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditComment", "Edit Toggle Node Comment"));
	Modify();
	*Target = Trimmed;
	return true;
}

FName UHTToggleGraphNode::DynamicInputPinName(const int32 Index)
{
	// The first pin keeps the original name so graphs saved by v1.5.30 keep their links.
	return Index == 0 ? InputPinName : FName(*FString::Printf(TEXT("In_%d"), Index));
}

UEdGraphPin* UHTToggleGraphNode::GetDynamicInputPin(const int32 Index) const
{
	const FName Name = DynamicInputPinName(Index);
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

void UHTToggleGraphNode::AllocateDynamicInputPins()
{
	InputCount = FMath::Clamp(InputCount, 1, MaxDynamicInputs);
	for (int32 Index = 0; Index < InputCount; ++Index)
	{
		UEdGraphPin* In = CreatePin(EGPD_Input, PinCategory, DynamicInputPinName(Index));
		In->PinFriendlyName = MakePinDisplayName(In);
	}
}

bool UHTToggleGraphNode::NormalizeDynamicInputs(const bool bRecord)
{
	if (!HasDynamicInputs() || bNormalizingInputs)
	{
		return false;
	}
	bNormalizingInputs = true;
	ON_SCOPE_EXIT
	{
		bNormalizingInputs = false;
	};

	int32 Present = 0;
	while (Present < MaxDynamicInputs && GetDynamicInputPin(Present))
	{
		++Present;
	}
	int32 LastLinked = INDEX_NONE;
	for (int32 Index = 0; Index < Present; ++Index)
	{
		if (!GetDynamicInputPin(Index)->LinkedTo.IsEmpty())
		{
			LastLinked = Index;
		}
	}
	// Keep everything up to the last linked input plus exactly one empty input after it.
	const int32 Desired = FMath::Clamp(LastLinked + 2, 1, MaxDynamicInputs);
	if (Desired == Present && InputCount == Present)
	{
		return false;
	}
	if (bRecord)
	{
		Modify();
	}
	for (int32 Index = Present; Index < Desired; ++Index)
	{
		UEdGraphPin* In = CreatePin(EGPD_Input, PinCategory, DynamicInputPinName(Index));
		In->PinFriendlyName = MakePinDisplayName(In);
	}
	for (int32 Index = Present - 1; Index >= Desired; --Index)
	{
		// Only empty inputs after the last link are removed. The pin memory stays valid until
		// the pin deletion queue ticks, so callers further up the connection call stack are safe.
		if (UEdGraphPin* Pin = GetDynamicInputPin(Index))
		{
			Pins.Remove(Pin);
			Pin->MarkAsGarbage();
		}
	}
	InputCount = Desired;
	if (bRecord)
	{
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyNodeChanged(this);
		}
	}
	return true;
}

void UHTToggleGraphNode::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	NormalizeDynamicInputs(true);
}

void UHTToggleGraphNode::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();
	NormalizeDynamicInputs(true);
}

namespace HTToggleGraphNodes
{
	// Dynamic inputs are "In N" on groups and the generate node, "Target N" on Texture Switches and
	// "Slots N" on Material Switches (pin names stay In / In_N so older graphs keep their links).
	static FText MakeDynamicInputName(const UEdGraphPin* Pin, const int32 Index)
	{
		const UEdGraphNode* Owner = Pin ? Pin->GetOwningNodeUnchecked() : nullptr;
		if (Owner && Owner->IsA<UHTToggleGraphNode_TextureSwitch>())
		{
			return FText::Format(LOCTEXT("TargetInput", "Target {0}"), FText::AsNumber(Index + 1));
		}
		if (Owner && Owner->IsA<UHTToggleGraphNode_MaterialSwitch>())
		{
			return FText::Format(LOCTEXT("SlotsInput", "Slots {0}"), FText::AsNumber(Index + 1));
		}
		return FText::Format(LOCTEXT("DynamicInput", "In {0}"), FText::AsNumber(Index + 1));
	}
}

bool UHTToggleGraphNode::SetCollapsedInline(const bool bNewCollapsed)
{
	if (!CanCollapse() || bCollapsed == bNewCollapsed)
	{
		return false;
	}
	const FScopedTransaction Transaction(bNewCollapsed ? LOCTEXT("CollapseNode", "Collapse Toggle Node") : LOCTEXT("ExpandNode", "Expand Toggle Node"));
	Modify();
	bCollapsed = bNewCollapsed;
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	return true;
}

int32 UHTToggleGraphNode::SetAllCollapsedInline(UEdGraph* Graph, const bool bNewCollapsed)
{
	if (!Graph)
	{
		return 0;
	}
	TArray<UHTToggleGraphNode*> Changing;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node);
		if (ToggleNode && ToggleNode->CanCollapse() && ToggleNode->bCollapsed != bNewCollapsed)
		{
			Changing.Add(ToggleNode);
		}
	}
	if (Changing.IsEmpty())
	{
		return 0;
	}
	const FScopedTransaction Transaction(bNewCollapsed ? LOCTEXT("CollapseAllNodes", "Collapse All Toggle Nodes") : LOCTEXT("ExpandAllNodes", "Expand All Toggle Nodes"));
	for (UHTToggleGraphNode* Node : Changing)
	{
		Node->Modify();
		Node->bCollapsed = bNewCollapsed;
	}
	Graph->NotifyGraphChanged();
	return Changing.Num();
}

void UHTToggleGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);
	if (!Menu)
	{
		return;
	}
	TWeakObjectPtr<UHTToggleGraphNode> WeakNode = const_cast<UHTToggleGraphNode*>(this);
	FToolMenuSection& Section = Menu->AddSection(TEXT("HTToggleGraphNodeView"), LOCTEXT("ViewSection", "View"));
	if (CanCollapse())
	{
		Section.AddMenuEntry(
			TEXT("HTToggleCollapseNode"),
			IsCollapsed() ? LOCTEXT("ExpandMenu", "Expand") : LOCTEXT("CollapseMenu", "Collapse"),
			IsCollapsed() ? LOCTEXT("ExpandMenuTooltip", "Show this node's fields again.") : LOCTEXT("CollapseMenuTooltip", "Show only the title, a summary and the pins."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (WeakNode.IsValid()) { WeakNode->SetCollapsedInline(!WeakNode->IsCollapsed()); } })));
	}
	Section.AddMenuEntry(
		TEXT("HTToggleCollapseAll"),
		LOCTEXT("CollapseAllMenu", "Collapse all"),
		LOCTEXT("CollapseAllMenuTooltip", "Collapse every part and switch of this graph."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (WeakNode.IsValid()) { SetAllCollapsedInline(WeakNode->GetGraph(), true); } })));
	Section.AddMenuEntry(
		TEXT("HTToggleExpandAll"),
		LOCTEXT("ExpandAllMenu", "Expand all"),
		LOCTEXT("ExpandAllMenuTooltip", "Expand every part and switch of this graph."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (WeakNode.IsValid()) { SetAllCollapsedInline(WeakNode->GetGraph(), false); } })));
}

UHTToggleGraphNode_Part* UHTToggleGraphNode::GetLinkedPart(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return nullptr;
	}
	for (const UEdGraphPin* Linked : Pin->LinkedTo)
	{
		if (UHTToggleGraphNode_Part* Part = Linked ? Cast<UHTToggleGraphNode_Part>(Linked->GetOwningNodeUnchecked()) : nullptr)
		{
			return Part;
		}
	}
	return nullptr;
}

FText UHTToggleGraphNode::MakePinDisplayName(const UEdGraphPin* Pin)
{
	using namespace HTToggleGraphNodes;
	if (!Pin)
	{
		return FText::GetEmpty();
	}
	if (Pin->Direction == EGPD_Output)
	{
		return LOCTEXT("OutPin", "Out");
	}
	const FString Name = Pin->PinName.ToString();
	FString Suffix;
	if (Name.Split(TEXT("_"), nullptr, &Suffix) && Suffix.IsNumeric())
	{
		const int32 Index = FCString::Atoi(*Suffix);
		if (Name.StartsWith(TEXT("Option_")))
		{
			return FText::Format(LOCTEXT("OptionPin", "Option {0}"), FText::AsNumber(Index + 1));
		}
		if (Name.StartsWith(TEXT("In_")))
		{
			return MakeDynamicInputName(Pin, Index);
		}
	}
	if (Pin->PinName == InputPinName)
	{
		return MakeDynamicInputName(Pin, 0);
	}
	return FText::FromName(Pin->PinName);
}

int32 UHTToggleGraphNode::RefreshPinDisplayNames()
{
	int32 Changed = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin)
		{
			continue;
		}
		const FText DisplayName = MakePinDisplayName(Pin);
		if (!Pin->PinFriendlyName.EqualTo(DisplayName))
		{
			Pin->PinFriendlyName = DisplayName;
			++Changed;
		}
	}
	return Changed;
}

void UHTToggleGraphNode::PostLoad()
{
	Super::PostLoad();
	RefreshPinDisplayNames();
	if (HasDynamicInputs())
	{
		// Graphs saved before InputCount existed (v1.5.30) have one input pin.
		int32 Present = 0;
		while (Present < MaxDynamicInputs && GetDynamicInputPin(Present))
		{
			++Present;
		}
		InputCount = FMath::Max(1, Present);
	}
}

FText UHTToggleGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(GetDisplayLabel());
}

FText UHTToggleGraphNode::GetTooltipText() const
{
	return FText::FromString(GetNodeId());
}

void UHTToggleGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!FromPin)
	{
		return;
	}
	const UEdGraphSchema* Schema = GetSchema();
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction != FromPin->Direction && Schema->TryCreateConnection(FromPin, Pin))
		{
			FromPin->GetOwningNode()->NodeConnectionListChanged();
			NodeConnectionListChanged();
			return;
		}
	}
}

bool UHTToggleGraphNode::DoesPropertyRebuildNode(const FName MemberName)
{
	// Pins (option count, dynamic inputs) and the node title. Everything else only redraws.
	static const FName Names[] = {
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode, InputCount),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Switch, OptionCount),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Switch, Label),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Switch, VariableName),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Switch, KeyName),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Part, Label),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Part, MaterialIDs),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Part, SourceMaterial),
		GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Group, Label),
	};
	for (const FName& Name : Names)
	{
		if (Name == MemberName)
		{
			return true;
		}
	}
	return false;
}

const FProperty* UHTToggleGraphNode::GetEditedMemberProperty(const FPropertyChangedEvent& PropertyChangedEvent) const
{
	// The engine also calls PostEditChange without a property: while importing pasted or
	// duplicated text (links to nodes later in the clipboard are still null and the node is not in
	// the graph yet), from PostEditUndo (the transaction already restored the pins) and while
	// loading. Reacting then would move half-resolved links, so only real property edits count.
	const FProperty* Changed = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty : PropertyChangedEvent.Property;
	const UEdGraph* Graph = GetGraph();
	if (!Changed || GIsTransacting || HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad) || !Graph || !Graph->Nodes.Contains(this))
	{
		return nullptr;
	}
	return Changed;
}

void UHTToggleGraphNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FProperty* Changed = GetEditedMemberProperty(PropertyChangedEvent);
	if (!Changed)
	{
		return;
	}
	if (DoesPropertyRebuildNode(Changed->GetFName()))
	{
		ReconstructNode();
	}
	GetGraph()->NotifyGraphChanged();
}

int32 UHTToggleGraphNode::RemoveDeadLinks()
{
	// Null entries (unresolved pasted links), trashed pins and pins of nodes that are not in this
	// graph cannot be moved to a rebuilt pin; the engine asserts on the first null one. A live pin
	// on the other end also forgets this pin, so nothing keeps pointing at a pin that is trashed
	// next (a node breaks all its links when it is destroyed).
	const UEdGraph* Graph = GetGraph();
	int32 Removed = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin)
		{
			continue;
		}
		Removed += Pin->LinkedTo.RemoveAll([Graph, Pin](UEdGraphPin* Linked)
		{
			const UEdGraphNode* Owner = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
			const bool bDead = !Linked || Linked->WasTrashed() || !IsValid(Owner) || (Graph && (Owner->GetGraph() != Graph || !Graph->Nodes.Contains(Owner)));
			if (bDead && Linked && !Linked->WasTrashed())
			{
				Linked->LinkedTo.Remove(Pin);
			}
			return bDead;
		});
	}
	return Removed;
}

void UHTToggleGraphNode::ReconstructNode()
{
	// A node that is not in its graph yet (text import in progress) is rebuilt by the paste
	// afterwards, once every link is resolved.
	const UEdGraph* Graph = GetGraph();
	if (Graph && !Graph->Nodes.Contains(this))
	{
		return;
	}
	Modify();
	RemoveDeadLinks();
	Pins.Remove(nullptr);

	// Record every linked node first so undo restores both ends of each link.
	TSet<UEdGraphNode*> LinkedNodes;
	for (UEdGraphPin* Pin : Pins)
	{
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			LinkedNodes.Add(Linked->GetOwningNode());
		}
	}
	for (UEdGraphNode* Linked : LinkedNodes)
	{
		Linked->Modify();
	}

	TArray<UEdGraphPin*> OldPins = Pins;
	Pins.Reset();
	AllocateDefaultPins();
	for (UEdGraphPin* OldPin : OldPins)
	{
		UEdGraphPin* NewPin = nullptr;
		for (UEdGraphPin* Candidate : Pins)
		{
			if (Candidate->PinName == OldPin->PinName && Candidate->Direction == OldPin->Direction)
			{
				NewPin = Candidate;
				break;
			}
		}
		if (NewPin)
		{
			NewPin->MovePersistentDataFromOldPin(*OldPin);
		}
		else
		{
			OldPin->BreakAllPinLinks();
		}
		OldPin->MarkAsGarbage();
	}
	if (UEdGraph* OwningGraph = GetGraph())
	{
		OwningGraph->NotifyNodeChanged(this);
	}
}

// ---------------------------------------------------------------- Part

FString UHTToggleGraphNode_Part::GetDisplayLabel() const
{
	// Material IDs stay in the title so error messages and zoomed-out graphs remain readable.
	// By material parts show the slots looked up on the preview mesh, like the compile does.
	const FString IDs = FString::JoinBy(GetEffectiveMaterialIDs(), TEXT(","), [](const int32 ID) { return FString::FromInt(ID); });
	const FString Trimmed = Label.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return IDs.IsEmpty() ? FString(TEXT("Part")) : FString::Printf(TEXT("Part %s"), *IDs);
	}
	return IDs.IsEmpty() ? FString::Printf(TEXT("Part %s"), *Trimmed) : FString::Printf(TEXT("Part %s (%s)"), *Trimmed, *IDs);
}

FString UHTToggleGraphNode_Part::GetCollapsedSummary() const
{
	const FString IDs = FString::JoinBy(GetEffectiveMaterialIDs(), TEXT(", "), [](const int32 ID) { return FString::FromInt(ID); });
	const FString Summary = IDs.IsEmpty() ? FString(TEXT("No IDs")) : FString::Printf(TEXT("IDs %s"), *IDs);
	return SourceMaterial.IsNull() ? Summary : FString::Printf(TEXT("%s \u00B7 %s"), *Summary, *SourceMaterial.GetAssetName());
}

void UHTToggleGraphNode_Part::AllocateDefaultPins()
{
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("PartOutput", "Out");
}

FLinearColor UHTToggleGraphNode_Part::GetNodeTitleColor() const
{
	return FLinearColor(0.10f, 0.45f, 0.65f);
}

TArray<int32> UHTToggleGraphNode_Part::FindSlotsUsingMaterial(const USkeletalMesh* Mesh, const UMaterialInterface* Material)
{
	TArray<int32> Slots;
	if (!Mesh || !Material)
	{
		return Slots;
	}
	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
	{
		if (Materials[Slot].MaterialInterface == Material)
		{
			Slots.Add(Slot);
		}
	}
	return Slots;
}

TArray<int32> UHTToggleGraphNode_Part::GetEffectiveMaterialIDs(bool* bOutMeshMissing) const
{
	if (bOutMeshMissing)
	{
		*bOutMeshMissing = false;
	}
	if (SourceMaterial.IsNull())
	{
		return MaterialIDs;
	}
	const UHTToggleGraph* Asset = GetToggleGraphAsset();
	const USkeletalMesh* Mesh = Asset ? Asset->ResolvePreviewMesh() : nullptr;
	if (!Mesh)
	{
		if (bOutMeshMissing)
		{
			*bOutMeshMissing = true;
		}
		return MaterialIDs;
	}
	return FindSlotsUsingMaterial(Mesh, SourceMaterial.LoadSynchronous());
}

FString UHTToggleGraphNode_Part::GetMaterialIDsText() const
{
	return FString::JoinBy(GetEffectiveMaterialIDs(), TEXT(", "), [](const int32 ID) { return FString::FromInt(ID); });
}

bool UHTToggleGraphNode_Part::ParseMaterialIDs(const FString& Text, TArray<int32>& OutIDs)
{
	OutIDs.Reset();
	FString Normalized = Text;
	for (const TCHAR* Separator : { TEXT(","), TEXT(";"), TEXT("\t"), TEXT("\r"), TEXT("\n") })
	{
		Normalized.ReplaceInline(Separator, TEXT(" "));
	}
	TArray<FString> Tokens;
	Normalized.ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		if (!Token.IsNumeric() || Token.Contains(TEXT(".")) || Token.StartsWith(TEXT("-")) || Token.Len() > 9)
		{
			OutIDs.Reset();
			return false;
		}
		OutIDs.AddUnique(FCString::Atoi(*Token));
	}
	return true;
}

bool UHTToggleGraphNode_Part::SetMaterialIDsInline(const TArray<int32>& NewMaterialIDs)
{
	TArray<int32> Sorted;
	for (const int32 ID : NewMaterialIDs)
	{
		if (ID >= 0)
		{
			Sorted.AddUnique(ID);
		}
	}
	Sorted.Sort();
	// Picking slots by hand leaves By material mode.
	if (Sorted == MaterialIDs && SourceMaterial.IsNull())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditMaterialIDs", "Edit Part Material IDs"));
	Modify();
	MaterialIDs = MoveTemp(Sorted);
	SourceMaterial.Reset();
	return true;
}

bool UHTToggleGraphNode_Part::SetMaterialIDSelectedInline(const int32 MaterialID, const bool bSelected)
{
	TArray<int32> NewIDs = GetEffectiveMaterialIDs();
	if (bSelected)
	{
		NewIDs.AddUnique(MaterialID);
	}
	else
	{
		NewIDs.Remove(MaterialID);
	}
	return SetMaterialIDsInline(NewIDs);
}

bool UHTToggleGraphNode_Part::SetSourceMaterialInline(UMaterialInterface* Material)
{
	const UHTToggleGraph* Asset = GetToggleGraphAsset();
	TArray<int32> Slots = Material ? FindSlotsUsingMaterial(Asset ? Asset->ResolvePreviewMesh() : nullptr, Material) : MaterialIDs;
	if (SourceMaterial.Get() == Material && Slots == MaterialIDs)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditPartMaterial", "Pick Part Slots By Material"));
	Modify();
	SourceMaterial = Material;
	MaterialIDs = MoveTemp(Slots);
	return true;
}

void UHTToggleGraphNode_Part::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Details panel edits behave like the node's own controls.
	if (const FProperty* Changed = GetEditedMemberProperty(PropertyChangedEvent))
	{
		if (Changed->GetFName() == GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Part, SourceMaterial) && !SourceMaterial.IsNull())
		{
			const UHTToggleGraph* Asset = GetToggleGraphAsset();
			MaterialIDs = FindSlotsUsingMaterial(Asset ? Asset->ResolvePreviewMesh() : nullptr, SourceMaterial.LoadSynchronous());
		}
		else if (Changed->GetFName() == GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Part, MaterialIDs))
		{
			SourceMaterial.Reset();
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

// ---------------------------------------------------------------- Switch base

FString UHTToggleGraphNode_SwitchBase::GetOptionLabel(const int32 Option) const
{
	return OptionLabels.IsValidIndex(Option) ? OptionLabels[Option] : FString();
}

FString UHTToggleGraphNode_SwitchBase::GetDisplayLabel() const
{
	const FString Trimmed = Label.TrimStartAndEnd();
	const FString Name = Trimmed.IsEmpty() ? VariableName.TrimStartAndEnd() : Trimmed;
	const FString Key = KeyName.TrimStartAndEnd();
	FString Title = GetKindTitle();
	if (!Name.IsEmpty())
	{
		Title += TEXT(" ") + Name;
	}
	if (!Key.IsEmpty())
	{
		Title += FString::Printf(TEXT(" [%s]"), *Key);
	}
	return Title;
}

FString UHTToggleGraphNode_SwitchBase::GetCollapsedSummary() const
{
	const FString Key = KeyName.TrimStartAndEnd();
	const FString KeyText = Key.IsEmpty() ? FString(TEXT("No key")) : FString::Printf(TEXT("Key %s"), *Key);
	return FString::Printf(TEXT("%s \u00B7 %d options"), *KeyText, OptionCount);
}

UHTToggleGraphNode_Part* UHTToggleGraphNode_SwitchBase::FindTargetPartWithMaterial(const UMaterialInterface* Material) const
{
	for (int32 Input = 0; Material && AcceptsTargetParts() && Input < InputCount; ++Input)
	{
		UHTToggleGraphNode_Part* Part = GetLinkedPart(GetDynamicInputPin(Input));
		if (Part && Part->SourceMaterial.Get() == Material)
		{
			return Part;
		}
	}
	return nullptr;
}

UHTToggleGraphNode_Part* UHTToggleGraphNode_SwitchBase::AddPartFromMaterial(UMaterialInterface* Material)
{
	UEdGraph* Graph = GetGraph();
	const UHTToggleGraph* Asset = GetToggleGraphAsset();
	if (!AcceptsTargetParts() || !Graph || !Material)
	{
		return nullptr;
	}
	if (UHTToggleGraphNode_Part* Existing = FindTargetPartWithMaterial(Material))
	{
		return Existing;
	}
	TArray<int32> Slots = UHTToggleGraphNode_Part::FindSlotsUsingMaterial(Asset ? Asset->ResolvePreviewMesh() : nullptr, Material);
	int32 FreeInput = INDEX_NONE;
	for (int32 Input = 0; Input < InputCount && FreeInput == INDEX_NONE; ++Input)
	{
		const UEdGraphPin* Pin = GetDynamicInputPin(Input);
		FreeInput = Pin && Pin->LinkedTo.IsEmpty() ? Input : INDEX_NONE;
	}
	if (Slots.IsEmpty() || FreeInput == INDEX_NONE)
	{
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddPartFromMaterial", "Add Part From Material"));
	Modify();
	const FVector2f Location(NodePosX - 560.0f, NodePosY + 140.0f + FreeInput * 150.0f);
	UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(FHTToggleGraphSchemaAction_NewNode::SpawnNode(Graph, UHTToggleGraphNode_Part::StaticClass(), Location, nullptr, false));
	if (!Part)
	{
		return nullptr;
	}
	Part->Label = Material->GetName();
	Part->SourceMaterial = Material;
	Part->MaterialIDs = MoveTemp(Slots);
	Graph->GetSchema()->TryCreateConnection(Part->GetOutputPin(), GetDynamicInputPin(FreeInput));
	Graph->NotifyGraphChanged();
	return Part;
}

bool UHTToggleGraphNode_SwitchBase::SetVariableNameInline(const FString& NewVariableName)
{
	const FString Trimmed = NewVariableName.TrimStartAndEnd();
	if (Trimmed == VariableName)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditVariable", "Edit Switch Variable"));
	Modify();
	VariableName = Trimmed;
	return true;
}

bool UHTToggleGraphNode_SwitchBase::SetKeyNameInline(const FString& NewKeyName)
{
	const FString Trimmed = NewKeyName.TrimStartAndEnd();
	if (Trimmed == KeyName)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditKey", "Edit Switch Key"));
	Modify();
	KeyName = Trimmed;
	return true;
}

bool UHTToggleGraphNode_SwitchBase::SetOptionCountInline(const int32 NewOptionCount)
{
	const int32 Clamped = FMath::Clamp(NewOptionCount, MinOptions, MaxOptions);
	if (Clamped == OptionCount)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditOptionCount", "Change Switch Option Count"));
	Modify();
	OptionCount = Clamped;
	InitialOption = FMath::Clamp(InitialOption, 0, OptionCount - 1);
	if (OptionLabels.Num() > OptionCount)
	{
		OptionLabels.SetNum(OptionCount);
	}
	while (!OptionLabels.IsEmpty() && OptionLabels.Last().IsEmpty())
	{
		OptionLabels.Pop();
	}
	OnOptionCountChanged();
	ReconstructNode();
	return true;
}

bool UHTToggleGraphNode_SwitchBase::SetInitialOptionInline(const int32 NewInitialOption)
{
	const int32 Clamped = FMath::Clamp(NewInitialOption, 0, OptionCount - 1);
	if (Clamped == InitialOption)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditInitialOption", "Change Switch Initial Option"));
	Modify();
	InitialOption = Clamped;
	return true;
}

bool UHTToggleGraphNode_SwitchBase::SetOptionLabelInline(const int32 Option, const FString& NewLabel)
{
	if (Option < 0 || Option >= OptionCount)
	{
		return false;
	}
	const FString Trimmed = NewLabel.TrimStartAndEnd();
	if (GetOptionLabel(Option) == Trimmed)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditOptionLabel", "Edit Switch Option Label"));
	Modify();
	if (OptionLabels.Num() <= Option)
	{
		OptionLabels.SetNum(Option + 1);
	}
	OptionLabels[Option] = Trimmed;
	// Trailing empty labels carry no information.
	while (!OptionLabels.IsEmpty() && OptionLabels.Last().IsEmpty())
	{
		OptionLabels.Pop();
	}
	return true;
}

// ---------------------------------------------------------------- Switch

UEdGraphPin* UHTToggleGraphNode_Switch::GetOptionPin(const int32 Option) const
{
	const FName Name = OptionPinName(Option);
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

int32 UHTToggleGraphNode_Switch::GetOptionIndex(const UEdGraphPin* Pin) const
{
	if (!Pin || Pin->GetOwningNode() != this || Pin->Direction != EGPD_Input)
	{
		return INDEX_NONE;
	}
	for (int32 Option = 0; Option < OptionCount; ++Option)
	{
		if (Pin->PinName == OptionPinName(Option))
		{
			return Option;
		}
	}
	return INDEX_NONE;
}

void UHTToggleGraphNode_Switch::AllocateDefaultPins()
{
	OptionCount = FMath::Clamp(OptionCount, MinOptions, MaxOptions);
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("SwitchOutput", "Out");
	for (int32 Option = 0; Option < OptionCount; ++Option)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, PinCategory, OptionPinName(Option));
		Pin->PinFriendlyName = FText::Format(LOCTEXT("OptionPin", "Option {0}"), FText::AsNumber(Option + 1));
	}
}

FLinearColor UHTToggleGraphNode_Switch::GetNodeTitleColor() const
{
	return FLinearColor(0.62f, 0.32f, 0.08f);
}

// ---------------------------------------------------------------- Texture Switch

TSoftObjectPtr<UTexture> UHTToggleGraphNode_TextureSwitch::GetTargetTexture(const int32 Target, const int32 Option) const
{
	return Targets.IsValidIndex(Target) && Targets[Target].OptionTextures.IsValidIndex(Option) ? Targets[Target].OptionTextures[Option] : TSoftObjectPtr<UTexture>();
}

bool UHTToggleGraphNode_TextureSwitch::SetParameterNameInline(const FString& NewParameterName)
{
	const FString Trimmed = NewParameterName.TrimStartAndEnd();
	if (Trimmed == ParameterName)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditTextureParameter", "Edit Texture Switch Parameter"));
	Modify();
	ParameterName = Trimmed;
	return true;
}

bool UHTToggleGraphNode_TextureSwitch::SetTargetTextureInline(const int32 Target, const int32 Option, UTexture* Texture)
{
	if (Target < 0 || Target >= MaxDynamicInputs || Option < 0 || Option >= OptionCount || GetTargetTexture(Target, Option).Get() == Texture)
	{
		return false;
	}
	if (!Texture && GetTargetTexture(Target, Option).IsNull())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditTargetTexture", "Edit Texture Switch Texture"));
	Modify();
	if (Targets.Num() <= Target)
	{
		Targets.SetNum(Target + 1);
	}
	TArray<TSoftObjectPtr<UTexture>>& Textures = Targets[Target].OptionTextures;
	if (Textures.Num() <= Option)
	{
		Textures.SetNum(Option + 1);
	}
	Textures[Option] = Texture;
	while (!Textures.IsEmpty() && Textures.Last().IsNull())
	{
		Textures.Pop();
	}
	return true;
}

TArray<UMaterialInterface*> UHTToggleGraphNode_TextureSwitch::GetTargetMaterials(const int32 Target) const
{
	TArray<UMaterialInterface*> Materials;
	const UHTToggleGraphNode_Part* Part = GetTargetPart(Target);
	if (!Part)
	{
		return Materials;
	}
	if (UMaterialInterface* Source = Part->SourceMaterial.LoadSynchronous())
	{
		Materials.Add(Source);
		return Materials;
	}
	const UHTToggleGraph* Asset = GetToggleGraphAsset();
	const USkeletalMesh* Mesh = Asset ? Asset->ResolvePreviewMesh() : nullptr;
	if (!Mesh)
	{
		return Materials;
	}
	const TArray<FSkeletalMaterial>& MeshMaterials = Mesh->GetMaterials();
	for (const int32 Slot : Part->GetEffectiveMaterialIDs())
	{
		if (MeshMaterials.IsValidIndex(Slot) && MeshMaterials[Slot].MaterialInterface)
		{
			Materials.AddUnique(MeshMaterials[Slot].MaterialInterface);
		}
	}
	return Materials;
}

TArray<FName> UHTToggleGraphNode_TextureSwitch::GetCandidateParameters() const
{
	TArray<FName> Common;
	bool bFirst = true;
	for (int32 Target = 0; Target < InputCount; ++Target)
	{
		for (UMaterialInterface* Material : GetTargetMaterials(Target))
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Material->GetAllTextureParameterInfo(Infos, Ids);
			TArray<FName> Names;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				Names.AddUnique(Info.Name);
			}
			if (bFirst)
			{
				Common = MoveTemp(Names);
				bFirst = false;
			}
			else
			{
				Common.RemoveAll([&Names](const FName Name) { return !Names.Contains(Name); });
			}
		}
	}
	Common.Sort(FNameLexicalLess());
	return Common;
}

FString UHTToggleGraphNode_TextureSwitch::GetCollapsedSummary() const
{
	const FString Parameter = ParameterName.TrimStartAndEnd();
	return FString::Printf(TEXT("%s \u00B7 %s"), *Super::GetCollapsedSummary(), Parameter.IsEmpty() ? TEXT("no parameter") : *Parameter);
}

void UHTToggleGraphNode_TextureSwitch::AllocateDefaultPins()
{
	OptionCount = FMath::Clamp(OptionCount, MinOptions, MaxOptions);
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("TextureSwitchOutput", "Out");
	AllocateDynamicInputPins();
}

FLinearColor UHTToggleGraphNode_TextureSwitch::GetNodeTitleColor() const
{
	return FLinearColor(0.42f, 0.20f, 0.58f);
}

void UHTToggleGraphNode_TextureSwitch::OnOptionCountChanged()
{
	for (FHTToggleTextureTarget& Target : Targets)
	{
		if (Target.OptionTextures.Num() > OptionCount)
		{
			Target.OptionTextures.SetNum(OptionCount);
		}
	}
}

// ---------------------------------------------------------------- Material Switch

TSoftObjectPtr<UMaterialInterface> UHTToggleGraphNode_MaterialSwitch::GetOptionMaterial(const int32 Option) const
{
	return OptionMaterials.IsValidIndex(Option) ? OptionMaterials[Option] : TSoftObjectPtr<UMaterialInterface>();
}

bool UHTToggleGraphNode_MaterialSwitch::SetOptionMaterialInline(const int32 Option, UMaterialInterface* Material)
{
	if (Option < 0 || Option >= OptionCount || GetOptionMaterial(Option).Get() == Material || (!Material && GetOptionMaterial(Option).IsNull()))
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditOptionMaterial", "Edit Material Switch Material"));
	Modify();
	if (OptionMaterials.Num() <= Option)
	{
		OptionMaterials.SetNum(Option + 1);
	}
	OptionMaterials[Option] = Material;
	while (!OptionMaterials.IsEmpty() && OptionMaterials.Last().IsNull())
	{
		OptionMaterials.Pop();
	}
	return true;
}

void UHTToggleGraphNode_MaterialSwitch::AllocateDefaultPins()
{
	OptionCount = FMath::Clamp(OptionCount, MinOptions, MaxOptions);
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("MaterialSwitchOutput", "Out");
	AllocateDynamicInputPins();
}

FLinearColor UHTToggleGraphNode_MaterialSwitch::GetNodeTitleColor() const
{
	return FLinearColor(0.08f, 0.46f, 0.44f);
}

void UHTToggleGraphNode_MaterialSwitch::OnOptionCountChanged()
{
	if (OptionMaterials.Num() > OptionCount)
	{
		OptionMaterials.SetNum(OptionCount);
	}
}

// ---------------------------------------------------------------- Group

FString UHTToggleGraphNode_Group::GetDisplayLabel() const
{
	const FString Trimmed = Label.TrimStartAndEnd();
	return Trimmed.IsEmpty() ? FString(TEXT("Group")) : FString::Printf(TEXT("Group %s"), *Trimmed);
}

void UHTToggleGraphNode_Group::AllocateDefaultPins()
{
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("GroupOutput", "Out");
	AllocateDynamicInputPins();
}

FLinearColor UHTToggleGraphNode_Group::GetNodeTitleColor() const
{
	return FLinearColor(0.30f, 0.30f, 0.34f);
}

// ---------------------------------------------------------------- Generate

FString UHTToggleGraphNode_Generate::GetDisplayLabel() const
{
	return TEXT("Generate");
}

void UHTToggleGraphNode_Generate::AllocateDefaultPins()
{
	AllocateDynamicInputPins();
}

FLinearColor UHTToggleGraphNode_Generate::GetNodeTitleColor() const
{
	return FLinearColor(0.12f, 0.50f, 0.20f);
}

#undef LOCTEXT_NAMESPACE
