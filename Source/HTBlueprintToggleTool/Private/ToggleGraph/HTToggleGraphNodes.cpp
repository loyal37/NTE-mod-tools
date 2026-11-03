#include "HTToggleGraphNodes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "HTToggleGraph.h"

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
	FString Label = GetDisplayLabel();
	Label.ReplaceInline(TEXT("#"), TEXT(" "));
	return FString::Printf(TEXT("%s#%s"), *Label, *NodeGuid.ToString(EGuidFormats::Digits).Left(6));
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
	GetGraph()->NotifyNodeChanged(this);
}

// ---------------------------------------------------------------- Part

FString UHTToggleGraphNode_Part::GetDisplayLabel() const
{
	const FString IDs = FString::JoinBy(MaterialIDs, TEXT(","), [](const int32 ID) { return FString::FromInt(ID); });
	const FString Trimmed = Label.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return IDs.IsEmpty() ? TEXT("部件") : FString::Printf(TEXT("部件 %s"), *IDs);
	}
	return IDs.IsEmpty() ? FString::Printf(TEXT("部件 %s"), *Trimmed) : FString::Printf(TEXT("部件 %s (%s)"), *Trimmed, *IDs);
}

void UHTToggleGraphNode_Part::AllocateDefaultPins()
{
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("PartOutput", "物体");
}

FLinearColor UHTToggleGraphNode_Part::GetNodeTitleColor() const
{
	return FLinearColor(0.10f, 0.45f, 0.65f);
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

FString UHTToggleGraphNode_Switch::GetDisplayLabel() const
{
	const FString Trimmed = Label.TrimStartAndEnd();
	const FString Name = Trimmed.IsEmpty() ? VariableName.TrimStartAndEnd() : Trimmed;
	const FString Key = KeyName.TrimStartAndEnd();
	FString Title = TEXT("切换");
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
	OptionCount = FMath::Clamp(OptionCount, 2, 64);
	for (int32 Option = 0; Option < OptionCount; ++Option)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, PinCategory, OptionPinName(Option));
		const FString OptionLabel = OptionLabels.IsValidIndex(Option) ? OptionLabels[Option].TrimStartAndEnd() : FString();
		Pin->PinFriendlyName = FText::FromString(OptionLabel.IsEmpty()
			? FString::Printf(TEXT("选项 %d"), Option + 1)
			: FString::Printf(TEXT("选项 %d %s"), Option + 1, *OptionLabel));
	}
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("SwitchOutput", "输出");
}

FLinearColor UHTToggleGraphNode_Switch::GetNodeTitleColor() const
{
	return FLinearColor(0.62f, 0.32f, 0.08f);
}

// ---------------------------------------------------------------- Group

FString UHTToggleGraphNode_Group::GetDisplayLabel() const
{
	const FString Trimmed = Label.TrimStartAndEnd();
	return Trimmed.IsEmpty() ? TEXT("群组") : FString::Printf(TEXT("群组 %s"), *Trimmed);
}

void UHTToggleGraphNode_Group::AllocateDefaultPins()
{
	UEdGraphPin* In = CreatePin(EGPD_Input, PinCategory, InputPinName);
	In->PinFriendlyName = LOCTEXT("GroupInput", "输入");
	UEdGraphPin* Out = CreatePin(EGPD_Output, PinCategory, OutputPinName);
	Out->PinFriendlyName = LOCTEXT("GroupOutput", "输出");
}

FLinearColor UHTToggleGraphNode_Group::GetNodeTitleColor() const
{
	return FLinearColor(0.30f, 0.30f, 0.34f);
}

// ---------------------------------------------------------------- Generate

FString UHTToggleGraphNode_Generate::GetDisplayLabel() const
{
	return TEXT("生成");
}

void UHTToggleGraphNode_Generate::AllocateDefaultPins()
{
	UEdGraphPin* In = CreatePin(EGPD_Input, PinCategory, InputPinName);
	In->PinFriendlyName = LOCTEXT("GenerateInput", "输入");
}

FLinearColor UHTToggleGraphNode_Generate::GetNodeTitleColor() const
{
	return FLinearColor(0.12f, 0.50f, 0.20f);
}

#undef LOCTEXT_NAMESPACE
