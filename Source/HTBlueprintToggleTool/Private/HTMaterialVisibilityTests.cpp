#include "HTBlueprintToggleGenerator.h"
#include "HTMaterialVisibilityInput.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/AnimBlueprintFactory.h"
#include "GameFramework/SaveGame.h"
#include "K2Node_CallFunction.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTMaterialVisibilityInputTest,
	"HT.BlueprintToggleTool.MaterialVisibilityInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTMaterialVisibilityInputTest::RunTest(const FString& Parameters)
{
	auto CheckGroups = [this](const FString& Text, const TArray<TArray<int32>>& Expected, const bool bCycle)
	{
		TArray<FHTMaterialVisibilityGroup> Groups;
		FString Error;
		if (!TestTrue(*FString::Printf(TEXT("Valid Material ID expression: %s"), *Text),
			HTMaterialVisibilityInput::Parse(Text, Groups, Error)))
		{
			AddError(Error);
			return;
		}
		TestTrue(TEXT("Valid expressions have no error"), Error.IsEmpty());
		TestEqual(TEXT("Group count"), Groups.Num(), Expected.Num());
		for (int32 Index = 0; Index < FMath::Min(Groups.Num(), Expected.Num()); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("Group %d preserves its material IDs and order"), Index),
				Groups[Index].MaterialIDs == Expected[Index]);
		}
		TestEqual(TEXT("Only a semicolon selects the group-only cycle"), HTMaterialVisibilityInput::IsGroupCycle(Text), bCycle);
	};
	CheckGroups(TEXT("5+6;8+10+12"), { { 5, 6 }, { 8, 10, 12 } }, true);
	CheckGroups(TEXT(" 5 + 6 ； 8 + 10 + 12 "), { { 5, 6 }, { 8, 10, 12 } }, true);
	CheckGroups(TEXT("5;8+10;12"), { { 5 }, { 8, 10 }, { 12 } }, true);
	CheckGroups(TEXT("16"), { { 16 } }, false);
	CheckGroups(TEXT("13+20"), { { 13, 20 } }, false);
	CheckGroups(TEXT("13,20"), { { 13 }, { 20 } }, false);
	CheckGroups(TEXT("13，20"), { { 13 }, { 20 } }, false);
	CheckGroups(TEXT("13 20"), { { 13 }, { 20 } }, false);
	CheckGroups(TEXT("5＋6；8＋10＋12"), { { 5, 6 }, { 8, 10, 12 } }, true);
	CheckGroups(TEXT("1+2,3+4"), { { 1, 2 }, { 3, 4 } }, false);
	CheckGroups(TEXT("0;2147483647"), { { 0 }, { MAX_int32 } }, true);

	for (const FString& Text : {
		FString(TEXT("")), FString(TEXT(" ")), FString(TEXT(";5")), FString(TEXT("5;")),
		FString(TEXT("5;;8")), FString(TEXT("5；；8")), FString(TEXT("5;；8")),
		FString(TEXT("5,,8")), FString(TEXT("5, ")), FString(TEXT("5+;8")),
		FString(TEXT("+5;8")), FString(TEXT("5++6;8")), FString(TEXT("-1;8")),
		FString(TEXT("5;cloth")), FString(TEXT("5;1.5")), FString(TEXT("5;2147483648")),
		FString(TEXT("5;999999999999999999999999999")), FString(TEXT("5+5;8")),
		FString(TEXT("5+6;6+8")), FString(TEXT("5+6;8,10")), FString(TEXT("5+6；8，10")) })
	{
		TArray<FHTMaterialVisibilityGroup> Groups;
		FString Error;
		TestFalse(*FString::Printf(TEXT("Reject malformed Material ID expression: %s"), *Text),
			HTMaterialVisibilityInput::Parse(Text, Groups, Error));
		TestFalse(TEXT("Invalid expressions explain the error"), Error.IsEmpty());
		TestTrue(TEXT("Invalid expressions do not expose partially parsed groups"), Groups.IsEmpty());
	}
	return !HasAnyErrors();
}

namespace HTMaterialVisibilityTests
{
	static TArray<UK2Node_CallFunction*> FindShowCalls(UEdGraphPin* StatePin)
	{
		TArray<UK2Node_CallFunction*> Calls;
		TArray<UEdGraphNode*> Pending;
		TSet<UEdGraphNode*> Visited;
		if (StatePin)
		{
			for (UEdGraphPin* Link : StatePin->LinkedTo)
			{
				Pending.Add(Link->GetOwningNode());
			}
		}
		while (!Pending.IsEmpty())
		{
			UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
			if (!Node || Visited.Contains(Node))
			{
				continue;
			}
			Visited.Add(Node);
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				if (Call->FunctionReference.GetMemberName() == TEXT("ShowMaterialSection"))
				{
					Calls.Add(Call);
				}
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					for (UEdGraphPin* Link : Pin->LinkedTo)
					{
						Pending.Add(Link->GetOwningNode());
					}
				}
			}
		}
		return Calls;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTMaterialGroupSwitchGenerationTest,
	"HT.BlueprintToggleTool.MaterialGroupSwitchGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTMaterialGroupSwitchGenerationTest::RunTest(const FString& Parameters)
{
	using namespace HTMaterialVisibilityTests;
	// Template Animation Blueprints need no character skeleton or existing project assets.
	const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTMaterialVisibility_%s"), *RunID));
	Package->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};

	for (const bool bIncludeHiddenState : { false, true })
	{
		const FString Suffix = bIncludeHiddenState ? TEXT("Legacy") : TEXT("GroupCycle");
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UAnimInstance::StaticClass();
		Factory->bTemplate = true;
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
			UAnimBlueprint::StaticClass(), Package, FName(*(TEXT("ABP_") + Suffix)), RF_Transient | RF_Public,
			nullptr, GWarn));
		UBlueprint* SaveBlueprint = FKismetEditorUtilities::CreateBlueprint(
			USaveGame::StaticClass(), Package, FName(*(TEXT("SG_") + Suffix)), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!TestNotNull(TEXT("Isolated template Animation Blueprint"), AnimBlueprint)
			|| !TestNotNull(TEXT("Isolated SaveGame Blueprint"), SaveBlueprint))
		{
			return false;
		}

		FHTBlueprintToggleGeneratorParams Params;
		Params.Mode = EHTBlueprintToggleMode::MaterialSection;
		Params.AnimBlueprintPath = AnimBlueprint->GetPathName();
		Params.SaveGameBlueprintPath = SaveBlueprint->GetPathName();
		Params.ToggleVariableName = TEXT("Outfit");
		Params.SaveVariableName = TEXT("OutfitSave");
		Params.SlotName = TEXT("HTVisibilityAutomation_") + RunID + Suffix;
		Params.KeyName = TEXT("K");
		Params.bSaveAssets = false;
		Params.bIncludeHiddenMaterialState = bIncludeHiddenState;
		Params.InitialState = bIncludeHiddenState ? 2 : 1;
		FString ParseError;
		if (!TestTrue(TEXT("Unequal groups parse for generation"), HTMaterialVisibilityInput::Parse(
			TEXT("5+6;8+10+12"), Params.MaterialVisibilityGroups, ParseError)))
		{
			return false;
		}
		const FHTBlueprintToggleGeneratorResult Result = FHTBlueprintToggleGenerator::Generate(Params);
		if (!TestTrue(*FString::Printf(TEXT("%s generation succeeds"), *Suffix), Result.bSuccess))
		{
			AddError(Result.ToDisplayString());
			continue;
		}
		TestTrue(TEXT("Generated Animation Blueprint compiles"), AnimBlueprint->Status != BS_Error);
		TestTrue(TEXT("Generated SaveGame Blueprint compiles"), SaveBlueprint->Status != BS_Error);
		for (UBlueprint* Blueprint : { static_cast<UBlueprint*>(AnimBlueprint), SaveBlueprint })
		{
			const FName Variable = Blueprint == AnimBlueprint ? TEXT("Outfit") : TEXT("OutfitSave");
			const FIntProperty* Property = FindFProperty<FIntProperty>(Blueprint->GeneratedClass, Variable);
			if (TestNotNull(TEXT("Shared cycle state is an integer variable"), Property))
			{
				TestEqual(TEXT("Requested initial state is preserved in the generated class"),
					Property->GetPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject()), Params.InitialState);
			}
		}

		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
		if (!TestNotNull(TEXT("Generated Event Graph"), Graph))
		{
			return false;
		}
		const int32 StateCount = bIncludeHiddenState ? 3 : 2;
		const TArray<int32> AllMaterialIDs = { 5, 6, 8, 10, 12 };
		int32 SwitchCount = 0;
		int32 ModuloCount = 0;
		int32 ClampCount = 0;
		int32 UpdateCommentCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_SwitchInteger* Switch = Cast<UK2Node_SwitchInteger>(Node))
			{
				++SwitchCount;
				int32 ActualStateCount = 0;
				for (const UEdGraphPin* Pin : Switch->Pins)
				{
					ActualStateCount += Pin && Pin->Direction == EGPD_Output
						&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
						&& Pin != Switch->GetDefaultPin() ? 1 : 0;
				}
				TestEqual(TEXT("Init and update expose exactly the requested cycle states"), ActualStateCount, StateCount);
				for (int32 State = 0; State < StateCount; ++State)
				{
					const TArray<UK2Node_CallFunction*> Calls = FindShowCalls(Switch->FindPin(FName(*FString::FromInt(State)), EGPD_Output));
					TestEqual(TEXT("Every state updates every selected material section"), Calls.Num(), AllMaterialIDs.Num());
					TSet<int32> SeenIDs;
					for (const UK2Node_CallFunction* Call : Calls)
					{
						const UEdGraphPin* IDPin = Call->FindPin(TEXT("MaterialID"));
						const UEdGraphPin* ShowPin = Call->FindPin(TEXT("bShow"));
						if (!TestTrue(TEXT("Visibility calls have material and show pins"), IDPin && ShowPin))
						{
							continue;
						}
						const int32 ID = FCString::Atoi(*IDPin->DefaultValue);
						TestTrue(TEXT("Visibility calls only affect the requested IDs"), AllMaterialIDs.Contains(ID));
						TestFalse(TEXT("Every material is updated once per state"), SeenIDs.Contains(ID));
						SeenIDs.Add(ID);
						const bool bExpectedVisible = State < Params.MaterialVisibilityGroups.Num()
							&& Params.MaterialVisibilityGroups[State].MaterialIDs.Contains(ID);
						TestEqual(*FString::Printf(TEXT("State %d material %d visibility"), State, ID),
							ShowPin->DefaultValue, bExpectedVisible ? FString(TEXT("true")) : FString(TEXT("false")));
					}
				}
			}
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				if (Call->FunctionReference.GetMemberName() == TEXT("Percent_IntInt"))
				{
					++ModuloCount;
					const UEdGraphPin* Divisor = Call->FindPin(TEXT("B"));
					TestTrue(TEXT("Cycle wraps after the last requested state"), Divisor && Divisor->DefaultValue == FString::FromInt(StateCount));
				}
				if (Call->FunctionReference.GetMemberName() == TEXT("Clamp"))
				{
					++ClampCount;
					const UEdGraphPin* Minimum = Call->FindPin(TEXT("Min"));
					const UEdGraphPin* Maximum = Call->FindPin(TEXT("Max"));
					const UEdGraphPin* Value = Call->FindPin(TEXT("Value"));
					const UEdGraphPin* ReturnValue = Call->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
					TestTrue(TEXT("Migrated saves clamp to the first group"), Minimum && Minimum->DefaultValue == TEXT("0"));
					TestTrue(TEXT("Migrated saves clamp to the last group"), Maximum && Maximum->DefaultValue == FString::FromInt(StateCount - 1));
					const UK2Node_VariableGet* GetSaved = Value && Value->LinkedTo.Num() == 1
						? Cast<UK2Node_VariableGet>(Value->LinkedTo[0]->GetOwningNode()) : nullptr;
					TestTrue(TEXT("Migration reads the saved cycle value"), GetSaved && GetSaved->VariableReference.GetMemberName() == TEXT("OutfitSave"));
					const UK2Node_VariableSet* SetCurrent = ReturnValue && ReturnValue->LinkedTo.Num() == 1
						? Cast<UK2Node_VariableSet>(ReturnValue->LinkedTo[0]->GetOwningNode()) : nullptr;
					TestTrue(TEXT("Migration applies the clamped current state"), SetCurrent && SetCurrent->VariableReference.GetMemberName() == TEXT("Outfit"));
				}
			}
			if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
			{
				if (Comment->NodeComment == TEXT("HT Update - Outfit"))
				{
					++UpdateCommentCount;
					TestFalse(TEXT("Update metadata retains nodes for NTE panel scanning"), Comment->GetNodesUnderComment().IsEmpty());
				}
			}
		}
		TestEqual(TEXT("Initialize and update each apply the selected group"), SwitchCount, 2);
		TestEqual(TEXT("All groups share one cycle increment"), ModuloCount, 1);
		TestEqual(TEXT("Only group-only cycles clamp legacy saved states"), ClampCount, bIncludeHiddenState ? 0 : 1);
		TestEqual(TEXT("NTE panel can identify the generated update group"), UpdateCommentCount, 1);

		// A hidden-state index must not be accepted in the group-only cycle.
		FHTBlueprintToggleGeneratorParams Invalid = Params;
		Invalid.InitialState = StateCount;
		const int32 NodeCount = Graph->Nodes.Num();
		const FHTBlueprintToggleGeneratorResult InvalidResult = FHTBlueprintToggleGenerator::Generate(Invalid);
		TestFalse(TEXT("An initial state beyond this cycle is rejected"), InvalidResult.bSuccess);
		TestEqual(TEXT("Rejected initial states do not append graph nodes"), Graph->Nodes.Num(), NodeCount);
		if (!bIncludeHiddenState)
		{
			Invalid = Params;
			Invalid.InitialState = 0;
			Invalid.MaterialVisibilityGroups.SetNum(1);
			const int32 AnimVariableCount = AnimBlueprint->NewVariables.Num();
			const int32 SaveVariableCount = SaveBlueprint->NewVariables.Num();
			const FHTBlueprintToggleGeneratorResult SingleGroupResult = FHTBlueprintToggleGenerator::Generate(Invalid);
			TestFalse(TEXT("Group-only cycle requires at least two groups"), SingleGroupResult.bSuccess);
			TestEqual(TEXT("Rejected groups do not append graph nodes"), Graph->Nodes.Num(), NodeCount);
			TestEqual(TEXT("Rejected groups do not add Animation Blueprint variables"), AnimBlueprint->NewVariables.Num(), AnimVariableCount);
			TestEqual(TEXT("Rejected groups do not add SaveGame variables"), SaveBlueprint->NewVariables.Num(), SaveVariableCount);
		}
	}
	return !HasAnyErrors();
}

#endif
