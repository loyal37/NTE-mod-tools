#include "HTToggleGraphNodes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "HTToggleGraph.h"
#include "ScopedTransaction.h"

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

void UHTToggleGraphNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Titles and option pins depend on the edited properties.
	ReconstructNode();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UHTToggleGraphNode::ReconstructNode()
{
	// Record every linked node first so undo restores both ends of each link.
	TSet<UEdGraphNode*> LinkedNodes;
	for (UEdGraphPin* Pin : Pins)
	{
		for (UEdGraphPin* Linked : Pin ? Pin->LinkedTo : TArray<UEdGraphPin*>())
		{
			if (Linked && Linked->GetOwningNode())
			{
				LinkedNodes.Add(Linked->GetOwningNode());
			}
		}
	}
	for (UEdGraphNode* Linked : LinkedNodes)
	{
		Linked->Modify();
	}
	Modify();

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
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

// ---------------------------------------------------------------- Part

FString UHTToggleGraphNode_Part::GetDisplayLabel() const
{
	// Material IDs stay in the title so error messages and zoomed-out graphs remain readable.
	const FString IDs = FString::JoinBy(MaterialIDs, TEXT(","), [](const int32 ID) { return FString::FromInt(ID); });
	const FString Trimmed = Label.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return IDs.IsEmpty() ? FString(TEXT("Part")) : FString::Printf(TEXT("Part %s"), *IDs);
	}
	return IDs.IsEmpty() ? FString::Printf(TEXT("Part %s"), *Trimmed) : FString::Printf(TEXT("Part %s (%s)"), *Trimmed, *IDs);
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

FString UHTToggleGraphNode_Part::GetMaterialIDsText() const
{
	return FString::JoinBy(MaterialIDs, TEXT(", "), [](const int32 ID) { return FString::FromInt(ID); });
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
	if (Sorted == MaterialIDs)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditMaterialIDs", "Edit Part Material IDs"));
	Modify();
	MaterialIDs = MoveTemp(Sorted);
	return true;
}

bool UHTToggleGraphNode_Part::SetMaterialIDSelectedInline(const int32 MaterialID, const bool bSelected)
{
	TArray<int32> NewIDs = MaterialIDs;
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

FString UHTToggleGraphNode_Switch::GetOptionLabel(const int32 Option) const
{
	return OptionLabels.IsValidIndex(Option) ? OptionLabels[Option] : FString();
}

FString UHTToggleGraphNode_Switch::GetDisplayLabel() const
{
	const FString Trimmed = Label.TrimStartAndEnd();
	const FString Name = Trimmed.IsEmpty() ? VariableName.TrimStartAndEnd() : Trimmed;
	const FString Key = KeyName.TrimStartAndEnd();
	FString Title = TEXT("Switch");
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

bool UHTToggleGraphNode_Switch::SetVariableNameInline(const FString& NewVariableName)
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

bool UHTToggleGraphNode_Switch::SetKeyNameInline(const FString& NewKeyName)
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

bool UHTToggleGraphNode_Switch::SetOptionCountInline(const int32 NewOptionCount)
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
	ReconstructNode();
	return true;
}

bool UHTToggleGraphNode_Switch::SetInitialOptionInline(const int32 NewInitialOption)
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

bool UHTToggleGraphNode_Switch::SetOptionLabelInline(const int32 Option, const FString& NewLabel)
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

// ---------------------------------------------------------------- Group

FName UHTToggleGraphNode_Group::GroupInputPinName(const int32 Index)
{
	// The first pin keeps the original name so graphs saved by v1.5.30 keep their links.
	return Index == 0 ? InputPinName : FName(*FString::Printf(TEXT("In_%d"), Index));
}

UEdGraphPin* UHTToggleGraphNode_Group::GetGroupInputPin(const int32 Index) const
{
	const FName Name = GroupInputPinName(Index);
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

FString UHTToggleGraphNode_Group::GetDisplayLabel() const
{
	const FString Trimmed = Label.TrimStartAndEnd();
	return Trimmed.IsEmpty() ? FString(TEXT("Group")) : FString::Printf(TEXT("Group %s"), *Trimmed);
}

void UHTToggleGraphNode_Group::AllocateDefaultPins()
{
	InputCount = FMath::Clamp(InputCount, MinInputs, MaxInputs);
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("GroupOutput", "Out");
	for (int32 Index = 0; Index < InputCount; ++Index)
	{
		UEdGraphPin* In = CreatePin(EGPD_Input, PinCategory, GroupInputPinName(Index));
		In->PinFriendlyName = FText::Format(LOCTEXT("GroupInput", "In {0}"), FText::AsNumber(Index + 1));
	}
}

FLinearColor UHTToggleGraphNode_Group::GetNodeTitleColor() const
{
	return FLinearColor(0.30f, 0.30f, 0.34f);
}

void UHTToggleGraphNode_Group::PostLoad()
{
	Super::PostLoad();
	// Graphs saved before Input Count existed have one input pin.
	int32 InputPins = 0;
	for (const UEdGraphPin* Pin : Pins)
	{
		InputPins += Pin && Pin->Direction == EGPD_Input ? 1 : 0;
	}
	if (InputPins > 0)
	{
		InputCount = InputPins;
	}
}

bool UHTToggleGraphNode_Group::SetInputCountInline(const int32 NewInputCount)
{
	const int32 Clamped = FMath::Clamp(NewInputCount, MinInputs, MaxInputs);
	if (Clamped == InputCount)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("EditGroupInputs", "Change Group Input Count"));
	Modify();
	InputCount = Clamped;
	ReconstructNode();
	return true;
}

// ---------------------------------------------------------------- Generate

FString UHTToggleGraphNode_Generate::GetDisplayLabel() const
{
	return TEXT("Generate");
}

void UHTToggleGraphNode_Generate::AllocateDefaultPins()
{
	UEdGraphPin* In = CreatePin(EGPD_Input, PinCategory, InputPinName);
	In->PinFriendlyName = LOCTEXT("GenerateInput", "In");
}

FLinearColor UHTToggleGraphNode_Generate::GetNodeTitleColor() const
{
	return FLinearColor(0.12f, 0.50f, 0.20f);
}

#undef LOCTEXT_NAMESPACE
