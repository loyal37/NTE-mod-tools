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
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace HTNestedToggleTests
{
	struct FCondition
	{
		FName Variable;
		int32 Option = 0;

		bool operator==(const FCondition& Other) const
		{
			return Variable == Other.Variable && Option == Other.Option;
		}
	};

	static FHTNestedToggleSwitch MakeSwitch(const FString& Id, const FString& Key, const int32 OptionCount, const FString& ParentId = FString(), const int32 ParentOption = INDEX_NONE, const FString& Variable = FString())
	{
		FHTNestedToggleSwitch Switch;
		Switch.NodeId = Id;
		Switch.KeyName = Key;
		Switch.OptionCount = OptionCount;
		Switch.ParentNodeId = ParentId;
		Switch.ParentOption = ParentOption;
		Switch.VariableName = Variable;
		return Switch;
	}

	struct FStep
	{
		const TCHAR* Switch;
		int32 Option;
	};

	static FHTNestedTogglePart MakePart(const FString& Id, const TArray<int32>& MaterialIDs, const TArray<FStep>& Path)
	{
		FHTNestedTogglePart Part;
		Part.NodeId = Id;
		Part.MaterialIDs = MaterialIDs;
		for (const FStep& Step : Path)
		{
			FHTNestedTogglePathStep& PathStep = Part.Path.AddDefaulted_GetRef();
			PathStep.SwitchNodeId = Step.Switch;
			PathStep.Option = Step.Option;
		}
		return Part;
	}

	static TMap<FName, int32> MakeState(const int32 Style, const int32 A, const int32 B, const int32 Stocking)
	{
		TMap<FName, int32> State;
		State.Add(TEXT("Style"), Style);
		State.Add(TEXT("Typhoeus_A0"), A);
		State.Add(TEXT("Typhoeus_B0"), B);
		State.Add(TEXT("Stocking"), Stocking);
		return State;
	}

	// Typhoeus-like tree: one outer 3-option style switch, three 2-option part switches under
	// every style sharing Alt 6/7/8 by position, empty options at any position, one explicit
	// shared variable name, one always-visible part and one part directly under the outer switch.
	static FHTNestedToggleGraph MakeTyphoeusGraph()
	{
		FHTNestedToggleGraph Graph;
		Graph.Name = TEXT("Typhoeus");
		FHTNestedToggleSwitch Style = MakeSwitch(TEXT("Style"), TEXT("alt 5"), 3, FString(), INDEX_NONE, TEXT("Style"));
		Style.Label = TEXT("Outfit style");
		Style.InitialOption = 1;
		Style.OptionLabels = { TEXT("A"), TEXT("B"), TEXT("C") };
		Graph.Switches.Add(Style);
		Graph.Switches.Add(MakeSwitch(TEXT("A0"), TEXT("Alt 6"), 2, TEXT("Style"), 0));
		Graph.Switches.Add(MakeSwitch(TEXT("B0"), TEXT("alt+7"), 2, TEXT("Style"), 0));
		Graph.Switches.Add(MakeSwitch(TEXT("C0"), TEXT("alt 8"), 2, TEXT("Style"), 0));
		Graph.Switches.Add(MakeSwitch(TEXT("A1"), TEXT("ALT 6"), 2, TEXT("Style"), 1));
		Graph.Switches.Add(MakeSwitch(TEXT("B1"), TEXT("alt 7"), 2, TEXT("Style"), 1));
		Graph.Switches.Add(MakeSwitch(TEXT("C1"), TEXT("alt 8"), 2, TEXT("Style"), 1));
		Graph.Switches.Add(MakeSwitch(TEXT("A2"), TEXT("alt 6"), 2, TEXT("Style"), 2));
		Graph.Switches.Add(MakeSwitch(TEXT("B2"), TEXT("alt 7"), 2, TEXT("Style"), 2));
		Graph.Switches.Add(MakeSwitch(TEXT("C2"), TEXT("alt 8"), 2, TEXT("Style"), 2, TEXT("Stocking")));
		Graph.Parts.Add(MakePart(TEXT("P1"), { 1 }, { { TEXT("Style"), 0 }, { TEXT("A0"), 0 } }));
		Graph.Parts.Add(MakePart(TEXT("P2"), { 2, 12 }, { { TEXT("Style"), 0 }, { TEXT("B0"), 0 } }));
		Graph.Parts.Add(MakePart(TEXT("P3"), { 3 }, { { TEXT("Style"), 0 }, { TEXT("C0"), 1 } }));
		Graph.Parts.Add(MakePart(TEXT("P4"), { 4 }, { { TEXT("Style"), 1 }, { TEXT("A1"), 1 } }));
		Graph.Parts.Add(MakePart(TEXT("P5"), { 5 }, { { TEXT("Style"), 1 }, { TEXT("B1"), 0 } }));
		Graph.Parts.Add(MakePart(TEXT("P6"), { 6 }, { { TEXT("Style"), 1 }, { TEXT("C1"), 0 } }));
		Graph.Parts.Add(MakePart(TEXT("P7"), { 7 }, { { TEXT("Style"), 2 }, { TEXT("A2"), 0 } }));
		Graph.Parts.Add(MakePart(TEXT("P8"), { 8 }, { { TEXT("Style"), 2 }, { TEXT("A2"), 1 } }));
		Graph.Parts.Add(MakePart(TEXT("P9"), { 9 }, {}));
		Graph.Parts.Add(MakePart(TEXT("P10"), { 10 }, { { TEXT("Style"), 2 } }));
		Graph.Parts.Add(MakePart(TEXT("P11"), { 11 }, { { TEXT("Style"), 2 }, { TEXT("C2"), 0 } }));
		return Graph;
	}

	static TMap<int32, TArray<FCondition>> ExpectedTyphoeusConditions()
	{
		const FName Style(TEXT("Style"));
		const FName A(TEXT("Typhoeus_A0"));
		const FName B(TEXT("Typhoeus_B0"));
		const FName S(TEXT("Stocking"));
		TMap<int32, TArray<FCondition>> Expected;
		auto Add = [&Expected](const int32 MaterialID, std::initializer_list<FCondition> Conditions)
		{
			Expected.Add(MaterialID) = TArray<FCondition>(Conditions);
		};
		Add(1, { { Style, 0 }, { A, 0 } });
		Add(2, { { Style, 0 }, { B, 0 } });
		Add(12, { { Style, 0 }, { B, 0 } });
		Add(3, { { Style, 0 }, { S, 1 } });
		Add(4, { { Style, 1 }, { A, 1 } });
		Add(5, { { Style, 1 }, { B, 0 } });
		Add(6, { { Style, 1 }, { S, 0 } });
		Add(7, { { Style, 2 }, { A, 0 } });
		Add(8, { { Style, 2 }, { A, 1 } });
		Add(9, {});
		Add(10, { { Style, 2 } });
		Add(11, { { Style, 2 }, { S, 0 } });
		return Expected;
	}

	static bool ReadCondition(const UEdGraphPin* ResultPin, FCondition& Out)
	{
		const UK2Node_CallFunction* Equal = ResultPin ? Cast<UK2Node_CallFunction>(ResultPin->GetOwningNode()) : nullptr;
		if (!Equal || Equal->FunctionReference.GetMemberName() != TEXT("EqualEqual_IntInt"))
		{
			return false;
		}
		const UEdGraphPin* APin = Equal->FindPin(TEXT("A"));
		const UEdGraphPin* BPin = Equal->FindPin(TEXT("B"));
		const UK2Node_VariableGet* Get = APin && APin->LinkedTo.Num() == 1 ? Cast<UK2Node_VariableGet>(APin->LinkedTo[0]->GetOwningNode()) : nullptr;
		if (!Get || !BPin || !BPin->LinkedTo.IsEmpty())
		{
			return false;
		}
		Out.Variable = Get->VariableReference.GetMemberName();
		Out.Option = FCString::Atoi(*BPin->DefaultValue);
		return true;
	}

	// Reads the visibility formula wired into a ShowMaterialSection call: nothing linked and
	// a true default = always visible, one Equal = one condition, an AND = several conditions.
	static bool ExtractConditions(const UK2Node_CallFunction* Show, TArray<FCondition>& Out)
	{
		Out.Reset();
		const UEdGraphPin* ShowPin = Show ? Show->FindPin(TEXT("bShow")) : nullptr;
		if (!ShowPin)
		{
			return false;
		}
		if (ShowPin->LinkedTo.IsEmpty())
		{
			return ShowPin->DefaultValue == TEXT("true");
		}
		if (ShowPin->LinkedTo.Num() != 1)
		{
			return false;
		}
		const UEdGraphPin* Source = ShowPin->LinkedTo[0];
		if (const UK2Node_CommutativeAssociativeBinaryOperator* And = Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Source->GetOwningNode()))
		{
			if (And->FunctionReference.GetMemberName() != TEXT("BooleanAND"))
			{
				return false;
			}
			for (const UEdGraphPin* Pin : And->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean)
				{
					continue;
				}
				FCondition Condition;
				if (Pin->LinkedTo.Num() != 1 || !ReadCondition(Pin->LinkedTo[0], Condition))
				{
					return false;
				}
				Out.Add(Condition);
			}
			return Out.Num() >= 2;
		}
		FCondition Condition;
		if (!ReadCondition(Source, Condition))
		{
			return false;
		}
		Out.Add(Condition);
		return true;
	}

	static bool SameConditions(TArray<FCondition> A, TArray<FCondition> B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const FCondition& Condition : A)
		{
			if (!B.RemoveSingle(Condition))
			{
				return false;
			}
		}
		return true;
	}

	static bool Evaluate(const TArray<FCondition>& Conditions, const TMap<FName, int32>& State)
	{
		for (const FCondition& Condition : Conditions)
		{
			const int32* Value = State.Find(Condition.Variable);
			if (!Value || *Value != Condition.Option)
			{
				return false;
			}
		}
		return true;
	}

	struct FGraphCounts
	{
		int32 TreeComments = 0;
		int32 ApplyEvents = 0;
		int32 ApplyCalls = 0;
		int32 ShowCalls = 0;
		int32 DoesSaveExist = 0;
		int32 LoadGame = 0;
		int32 SaveSlot = 0;
		int32 Clamp = 0;
		int32 WasPressed = 0;
		int32 ModifierDown = 0;
		int32 Modulo = 0;
		int32 Switches = 0;
		int32 LegacyUpdateComments = 0;
		int32 LegacyInitComments = 0;
		int32 Nodes = 0;
		FString Metadata;
		TArray<UK2Node_CallFunction*> Shows;
		TSet<FString> SlotNames;
		TSet<FString> PressedKeys;
		TMap<FString, int32> ClampMaxByVariable;
		TSet<int32> ModuloDivisors;
	};

	static FGraphCounts CountGraph(UEdGraph* Graph, const FString& TreePrefix, const FName ApplyEvent)
	{
		FGraphCounts Counts;
		Counts.Nodes = Graph->Nodes.Num();
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
			{
				if (Comment->NodeComment.StartsWith(TreePrefix))
				{
					++Counts.TreeComments;
					Counts.Metadata = Comment->NodeComment;
				}
				Counts.LegacyUpdateComments += Comment->NodeComment == TEXT("HT Update - Outfit") ? 1 : 0;
				Counts.LegacyInitComments += Comment->NodeComment == TEXT("HT Init - Outfit") ? 1 : 0;
			}
			else if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
			{
				Counts.ApplyEvents += Event->CustomFunctionName == ApplyEvent ? 1 : 0;
			}
			else if (Cast<UK2Node_SwitchInteger>(Node))
			{
				++Counts.Switches;
			}
			else if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				const FName Function = Call->FunctionReference.GetMemberName();
				if (Function == ApplyEvent)
				{
					++Counts.ApplyCalls;
				}
				else if (Function == TEXT("ShowMaterialSection"))
				{
					++Counts.ShowCalls;
					Counts.Shows.Add(Call);
				}
				else if (Function == TEXT("DoesSaveGameExist"))
				{
					++Counts.DoesSaveExist;
					if (const UEdGraphPin* Slot = Call->FindPin(TEXT("SlotName")))
					{
						Counts.SlotNames.Add(Slot->DefaultValue);
					}
				}
				else if (Function == TEXT("LoadGameFromSlot"))
				{
					++Counts.LoadGame;
				}
				else if (Function == TEXT("SaveGameToSlot"))
				{
					++Counts.SaveSlot;
				}
				else if (Function == TEXT("Clamp"))
				{
					++Counts.Clamp;
					const UEdGraphPin* Value = Call->FindPin(TEXT("Value"));
					const UEdGraphPin* Max = Call->FindPin(TEXT("Max"));
					const UK2Node_VariableGet* Saved = Value && Value->LinkedTo.Num() == 1 ? Cast<UK2Node_VariableGet>(Value->LinkedTo[0]->GetOwningNode()) : nullptr;
					if (Saved && Max)
					{
						Counts.ClampMaxByVariable.Add(Saved->VariableReference.GetMemberName().ToString(), FCString::Atoi(*Max->DefaultValue));
					}
				}
				else if (Function == TEXT("WasInputKeyJustPressed"))
				{
					++Counts.WasPressed;
					if (const UEdGraphPin* Key = Call->FindPin(TEXT("Key")))
					{
						Counts.PressedKeys.Add(Key->DefaultValue);
					}
				}
				else if (Function == TEXT("IsInputKeyDown"))
				{
					++Counts.ModifierDown;
				}
				else if (Function == TEXT("Percent_IntInt"))
				{
					++Counts.Modulo;
					if (const UEdGraphPin* Divisor = Call->FindPin(TEXT("B")))
					{
						Counts.ModuloDivisors.Add(FCString::Atoi(*Divisor->DefaultValue));
					}
				}
			}
		}
		return Counts;
	}

	static bool IssueMentions(const TArray<FHTNestedToggleIssue>& Issues, const TArray<FString>& NodeIds)
	{
		for (const FHTNestedToggleIssue& Issue : Issues)
		{
			bool bAll = true;
			for (const FString& NodeId : NodeIds)
			{
				bAll &= Issue.NodeIds.Contains(NodeId);
			}
			if (bAll)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTNestedToggleTreeValidationTest,
	"HT.BlueprintToggleTool.NestedToggleTreeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTNestedToggleTreeValidationTest::RunTest(const FString& Parameters)
{
	using namespace HTNestedToggleTests;

	// The reference tree compiles and resolves shared keys / explicit names to four variables.
	{
		FHTNestedToggleCompiledGraph Compiled;
		TArray<FHTNestedToggleIssue> Errors;
		if (!TestTrue(TEXT("Typhoeus tree compiles"), FHTBlueprintToggleGenerator::CompileNestedToggleGraph(MakeTyphoeusGraph(), Compiled, Errors)))
		{
			for (const FHTNestedToggleIssue& Error : Errors)
			{
				AddError(Error.ToDisplayString());
			}
			return false;
		}
		TestEqual(TEXT("Shared keys collapse nine part switches and the style into four variables"), Compiled.Variables.Num(), 4);
		TArray<FString> Names;
		for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
		{
			Names.Add(Variable.VariableName.ToString());
			TestEqual(TEXT("Every shared variable has exactly one key"), Variable.KeyNames.Num(), 1);
		}
		TestTrue(TEXT("Explicit style name is kept"), Names.Contains(TEXT("Style")));
		TestTrue(TEXT("Derived names combine tree and first switch"), Names.Contains(TEXT("Typhoeus_A0")) && Names.Contains(TEXT("Typhoeus_B0")));
		TestTrue(TEXT("One explicit name renames the whole shared group"), Names.Contains(TEXT("Stocking")));
		TestEqual(TEXT("All switches map to a variable"), Compiled.SwitchVariableIndices.Num(), 10);
		TestEqual(TEXT("C0 and C2 share the stocking variable"), Compiled.SwitchVariableIndices[3], Compiled.SwitchVariableIndices[9]);
		TestEqual(TEXT("All parts compile"), Compiled.Parts.Num(), 11);
		TestEqual(TEXT("Metadata name is sanitized"), Compiled.Name, FString(TEXT("Typhoeus")));
	}

	auto ExpectFailure = [this](const FString& Label, const FHTNestedToggleGraph& Graph, const TArray<FString>& NodeIds)
	{
		FHTNestedToggleCompiledGraph Compiled;
		TArray<FHTNestedToggleIssue> Errors;
		const bool bCompiled = FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Graph, Compiled, Errors);
		TestFalse(*FString::Printf(TEXT("%s is rejected"), *Label), bCompiled);
		TestTrue(*FString::Printf(TEXT("%s reports the offending nodes %s"), *Label, *FString::Join(NodeIds, TEXT(", "))), IssueMentions(Errors, NodeIds));
		TestTrue(*FString::Printf(TEXT("%s leaves no compiled parts"), *Label), Compiled.Parts.IsEmpty());
	};

	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[4].OptionCount = 3; // A1 shares Alt 6 with A0/A2 but has three options.
		ExpectFailure(TEXT("Same key with different option counts"), Graph, { TEXT("A0"), TEXT("A1"), TEXT("A2") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[4].InitialOption = 1;
		ExpectFailure(TEXT("Same key with different initial options"), Graph, { TEXT("A0"), TEXT("A1") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Parts.Add(MakePart(TEXT("P12"), { 4 }, { { TEXT("Style"), 2 }, { TEXT("B2"), 0 } }));
		ExpectFailure(TEXT("A material connected in two parts"), Graph, { TEXT("P4"), TEXT("P12") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Parts[0].Path = { { TEXT("Style"), 1 }, { TEXT("A0"), 0 } }; // A0 hangs on style 0, not 1.
		ExpectFailure(TEXT("A path that does not follow the parent attachment"), Graph, { TEXT("P1") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Parts[0].Path = { { TEXT("A0"), 0 } }; // Missing the root step.
		ExpectFailure(TEXT("A path that skips the root switch"), Graph, { TEXT("P1") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[1].KeyName = TEXT("nosuchkey");
		ExpectFailure(TEXT("An unknown key"), Graph, { TEXT("A0") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[1].KeyName.Empty();
		ExpectFailure(TEXT("A missing key"), Graph, { TEXT("A0") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[1].ParentNodeId = TEXT("Missing");
		ExpectFailure(TEXT("A missing parent"), Graph, { TEXT("A0") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[1].ParentOption = 3;
		ExpectFailure(TEXT("A parent option out of range"), Graph, { TEXT("A0") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[0].ParentNodeId = TEXT("A0");
		Graph.Switches[0].ParentOption = 0;
		ExpectFailure(TEXT("A parent cycle"), Graph, { TEXT("Style"), TEXT("A0") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[1].KeyName = TEXT("alt 5"); // A0 would share the style variable with its parent.
		ExpectFailure(TEXT("A switch sharing its ancestor's variable"), Graph, { TEXT("A0"), TEXT("Style") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[1].VariableName = TEXT("Coat");
		Graph.Switches[4].VariableName = TEXT("Shirt"); // Same Alt 6 key, different explicit names.
		ExpectFailure(TEXT("Same key with conflicting variable names"), Graph, { TEXT("A0"), TEXT("A1") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Switches[2].VariableName = TEXT("Stocking"); // B0 (Alt 7) and C2 (Alt 8) both named Stocking.
		FHTNestedToggleCompiledGraph Compiled;
		TArray<FHTNestedToggleIssue> Errors;
		TestTrue(TEXT("The same explicit name on different keys is allowed"), FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Graph, Compiled, Errors));
		TestEqual(TEXT("It merges both key groups into one variable"), Compiled.Variables.Num(), 3);
		const int32 StockingIndex = Compiled.SwitchVariableIndices[2];
		TestEqual(TEXT("B and C switches now share one variable"), StockingIndex, Compiled.SwitchVariableIndices[9]);
		TestEqual(TEXT("The merged variable is advanced by both keys"), Compiled.Variables.IsValidIndex(StockingIndex) ? Compiled.Variables[StockingIndex].KeyNames.Num() : 0, 2);
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Parts[0].MaterialIDs = { 1, 1 };
		ExpectFailure(TEXT("A part listing one material twice"), Graph, { TEXT("P1") });
	}
	{
		FHTNestedToggleGraph Graph = MakeTyphoeusGraph();
		Graph.Parts[0].MaterialIDs.Empty();
		ExpectFailure(TEXT("A part without materials"), Graph, { TEXT("P1") });
	}
	{
		FHTNestedToggleGraph Graph;
		Graph.Name = TEXT("  my tree.01 ");
		Graph.Switches.Add(MakeSwitch(TEXT("S"), TEXT("9"), 2));
		Graph.Parts.Add(MakePart(TEXT("P"), { 3 }, { { TEXT("S"), 1 } }));
		FHTNestedToggleCompiledGraph Compiled;
		TArray<FHTNestedToggleIssue> Errors;
		TestTrue(TEXT("A single switch tree compiles"), FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Graph, Compiled, Errors));
		TestEqual(TEXT("Tree names become identifiers"), Compiled.Name, FString(TEXT("my_tree_01")));
		TestEqual(TEXT("Apply event name follows the identifier"), FHTBlueprintToggleGenerator::MakeNestedToggleApplyEventName(Graph.Name), FString(TEXT("HTN_Apply_my_tree_01")));
		TestEqual(TEXT("Metadata prefix follows the identifier"), FHTBlueprintToggleGenerator::MakeNestedToggleMetadataPrefix(Graph.Name), FString(TEXT("HT Toggle Tree - my_tree_01;")));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTNestedToggleTreeGenerationTest,
	"HT.BlueprintToggleTool.NestedToggleTreeGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTNestedToggleTreeGenerationTest::RunTest(const FString& Parameters)
{
	using namespace HTNestedToggleTests;
	// Template Animation Blueprints need no character skeleton or existing project assets.
	const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTNestedToggle_%s"), *RunID));
	Package->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = UAnimInstance::StaticClass();
	Factory->bTemplate = true;
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(), Package, TEXT("ABP_NestedTree"), RF_Transient | RF_Public, nullptr, GWarn));
	UBlueprint* SaveBlueprint = FKismetEditorUtilities::CreateBlueprint(
		USaveGame::StaticClass(), Package, TEXT("SG_NestedTree"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Isolated template Animation Blueprint"), AnimBlueprint)
		|| !TestNotNull(TEXT("Isolated SaveGame Blueprint"), SaveBlueprint))
	{
		return false;
	}
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
	if (!TestNotNull(TEXT("Template Event Graph"), Graph))
	{
		return false;
	}

	// An existing legacy toggle must survive untouched: the tree is a separate feature.
	{
		FHTBlueprintToggleGeneratorParams Legacy;
		Legacy.Mode = EHTBlueprintToggleMode::MaterialSection;
		Legacy.AnimBlueprintPath = AnimBlueprint->GetPathName();
		Legacy.SaveGameBlueprintPath = SaveBlueprint->GetPathName();
		Legacy.ToggleVariableName = TEXT("Outfit");
		Legacy.SaveVariableName = TEXT("OutfitSave");
		Legacy.SlotName = TEXT("Outfit Typhoeus");
		Legacy.KeyName = TEXT("K");
		Legacy.bSaveAssets = false;
		FString ParseError;
		if (!TestTrue(TEXT("Legacy groups parse"), HTMaterialVisibilityInput::Parse(TEXT("20+21;22"), Legacy.MaterialVisibilityGroups, ParseError)))
		{
			return false;
		}
		Legacy.bIncludeHiddenMaterialState = false;
		const FHTBlueprintToggleGeneratorResult LegacyResult = FHTBlueprintToggleGenerator::Generate(Legacy);
		if (!TestTrue(TEXT("Legacy material group generation succeeds first"), LegacyResult.bSuccess))
		{
			AddError(LegacyResult.ToDisplayString());
			return false;
		}
	}
	const FString TreePrefix = FHTBlueprintToggleGenerator::MakeNestedToggleMetadataPrefix(TEXT("Typhoeus"));
	const FName ApplyEvent(*FHTBlueprintToggleGenerator::MakeNestedToggleApplyEventName(TEXT("Typhoeus")));
	const FGraphCounts BeforeTree = CountGraph(Graph, TreePrefix, ApplyEvent);
	TestEqual(TEXT("Legacy toggle produced its two Switch on Int nodes"), BeforeTree.Switches, 2);
	TestEqual(TEXT("Legacy toggle produced its show calls"), BeforeTree.ShowCalls, 12);
	const int32 AnimVariablesBefore = AnimBlueprint->NewVariables.Num();
	const int32 SaveVariablesBefore = SaveBlueprint->NewVariables.Num();

	FHTNestedToggleGeneratorParams Params;
	Params.AnimBlueprintPath = AnimBlueprint->GetPathName();
	Params.SaveGameBlueprintPath = SaveBlueprint->GetPathName();
	Params.CharacterName = TEXT("Typhoeus");
	Params.Graph = MakeTyphoeusGraph();
	Params.bSaveAssets = false;

	// Invalid trees are rejected before any asset is touched.
	{
		FHTNestedToggleGeneratorParams Invalid = Params;
		Invalid.Graph.Switches[4].OptionCount = 3;
		const FHTNestedToggleGeneratorResult InvalidResult = FHTBlueprintToggleGenerator::GenerateNestedVisibility(Invalid);
		TestFalse(TEXT("A tree with mismatched shared option counts is rejected"), InvalidResult.bSuccess);
		TestTrue(TEXT("The rejection names the shared switches"), IssueMentions(InvalidResult.Errors, { TEXT("A0"), TEXT("A1"), TEXT("A2") }));
		TestEqual(TEXT("A rejected tree adds no graph nodes"), Graph->Nodes.Num(), BeforeTree.Nodes);
		TestEqual(TEXT("A rejected tree adds no Animation Blueprint variables"), AnimBlueprint->NewVariables.Num(), AnimVariablesBefore);
		TestEqual(TEXT("A rejected tree adds no SaveGame variables"), SaveBlueprint->NewVariables.Num(), SaveVariablesBefore);
	}

	const FHTNestedToggleGeneratorResult Result = FHTBlueprintToggleGenerator::GenerateNestedVisibility(Params);
	if (!TestTrue(TEXT("Typhoeus tree generation succeeds"), Result.bSuccess))
	{
		AddError(Result.ToDisplayString());
		return false;
	}
	TestTrue(TEXT("Generated Animation Blueprint compiles"), AnimBlueprint->Status != BS_Error);
	TestTrue(TEXT("Generated SaveGame Blueprint compiles"), SaveBlueprint->Status != BS_Error);
	TestEqual(TEXT("Result reports the four managed variables"), Result.Variables.Num(), 4);

	const TMap<FName, int32> ExpectedDefaults = MakeState(1, 0, 0, 0);
	for (const TPair<FName, int32>& Expected : ExpectedDefaults)
	{
		const FIntProperty* AnimProperty = FindFProperty<FIntProperty>(AnimBlueprint->GeneratedClass, Expected.Key);
		const FIntProperty* SaveProperty = FindFProperty<FIntProperty>(SaveBlueprint->GeneratedClass, FName(*(Expected.Key.ToString() + TEXT("Save"))));
		if (TestNotNull(*FString::Printf(TEXT("Animation Blueprint has int variable %s"), *Expected.Key.ToString()), AnimProperty))
		{
			TestEqual(*FString::Printf(TEXT("%s starts at its initial option"), *Expected.Key.ToString()),
				AnimProperty->GetPropertyValue_InContainer(AnimBlueprint->GeneratedClass->GetDefaultObject()), Expected.Value);
		}
		if (TestNotNull(*FString::Printf(TEXT("SaveGame Blueprint has int variable %sSave"), *Expected.Key.ToString()), SaveProperty))
		{
			TestEqual(*FString::Printf(TEXT("%sSave starts at its initial option"), *Expected.Key.ToString()),
				SaveProperty->GetPropertyValue_InContainer(SaveBlueprint->GeneratedClass->GetDefaultObject()), Expected.Value);
		}
	}

	auto CheckGeneratedGraph = [&](const TCHAR* Pass)
	{
		const FGraphCounts Counts = CountGraph(Graph, TreePrefix, ApplyEvent);
		TestEqual(*FString::Printf(TEXT("%s: one managed tree comment"), Pass), Counts.TreeComments, 1);
		TestEqual(*FString::Printf(TEXT("%s: one Apply custom event"), Pass), Counts.ApplyEvents, 1);
		TestEqual(*FString::Printf(TEXT("%s: Apply is called once from init and once per key handler"), Pass), Counts.ApplyCalls, 5);
		TestEqual(*FString::Printf(TEXT("%s: legacy and tree show calls"), Pass), Counts.ShowCalls, 12 + 12);
		TestEqual(*FString::Printf(TEXT("%s: every variable reads its save"), Pass), Counts.DoesSaveExist, 4 + 1);
		TestEqual(*FString::Printf(TEXT("%s: every variable loads its save"), Pass), Counts.LoadGame, 4 + 1);
		TestEqual(*FString::Printf(TEXT("%s: every variable writes its save on init and on key press"), Pass), Counts.SaveSlot, 8 + 2);
		TestEqual(*FString::Printf(TEXT("%s: every restored value is clamped"), Pass), Counts.Clamp, 4 + 1);
		TestEqual(*FString::Printf(TEXT("%s: one key handler per state variable"), Pass), Counts.WasPressed, 4 + 1);
		TestEqual(*FString::Printf(TEXT("%s: alt handlers test both modifier keys"), Pass), Counts.ModifierDown, 8);
		TestEqual(*FString::Printf(TEXT("%s: one cycle increment per handler"), Pass), Counts.Modulo, 4 + 1);
		TestEqual(*FString::Printf(TEXT("%s: the tree never expands Switch on Int"), Pass), Counts.Switches, 2);
		TestEqual(*FString::Printf(TEXT("%s: legacy update group preserved"), Pass), Counts.LegacyUpdateComments, 1);
		TestEqual(*FString::Printf(TEXT("%s: legacy init group preserved"), Pass), Counts.LegacyInitComments, 1);
		for (const TCHAR* Slot : { TEXT("Style Typhoeus"), TEXT("Typhoeus_A0 Typhoeus"), TEXT("Typhoeus_B0 Typhoeus"), TEXT("Stocking Typhoeus") })
		{
			TestTrue(*FString::Printf(TEXT("%s: save slot %s carries the character name"), Pass, Slot), Counts.SlotNames.Contains(Slot));
		}
		for (const TCHAR* Key : { TEXT("Five"), TEXT("Six"), TEXT("Seven"), TEXT("Eight") })
		{
			TestTrue(*FString::Printf(TEXT("%s: key %s is handled"), Pass, Key), Counts.PressedKeys.Contains(Key));
		}
		TestTrue(*FString::Printf(TEXT("%s: style wraps after three options"), Pass), Counts.ModuloDivisors.Contains(3));
		TestEqual(*FString::Printf(TEXT("%s: style clamps old saves to its last option"), Pass), Counts.ClampMaxByVariable.FindRef(TEXT("StyleSave")), 2);
		TestEqual(*FString::Printf(TEXT("%s: stocking clamps old saves to its last option"), Pass), Counts.ClampMaxByVariable.FindRef(TEXT("StockingSave")), 1);

		TestTrue(*FString::Printf(TEXT("%s: metadata names the Apply event"), Pass), Counts.Metadata.Contains(TEXT(";Apply=HTN_Apply_Typhoeus")));
		int32 SwitchEntries = 0;
		for (int32 Index = Counts.Metadata.Find(TEXT(";Switch=")); Index != INDEX_NONE; Index = Counts.Metadata.Find(TEXT(";Switch="), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1))
		{
			++SwitchEntries;
		}
		TestEqual(*FString::Printf(TEXT("%s: metadata lists every switch node"), Pass), SwitchEntries, 10);
		TestTrue(*FString::Printf(TEXT("%s: root switch entry has labels and no parent"), Pass), Counts.Metadata.Contains(TEXT(";Switch=Style,Alt Five,3,,-1,Outfit style,A|B|C")));
		TestTrue(*FString::Printf(TEXT("%s: shared stocking entry under style 2 marks its empty option"), Pass), Counts.Metadata.Contains(TEXT(";Switch=Stocking,Alt Eight,2,Style,2,Stocking,1|")));
		TestTrue(*FString::Printf(TEXT("%s: stocking entry under style 0 marks option 0 empty"), Pass), Counts.Metadata.Contains(TEXT(";Switch=Stocking,Alt Eight,2,Style,0,Stocking,|2")));
		TestTrue(*FString::Printf(TEXT("%s: unused switch marks both options empty"), Pass), Counts.Metadata.Contains(TEXT(";Switch=Typhoeus_B0,Alt Seven,2,Style,2,Typhoeus_B0,|")));

		// Every managed material gets exactly the AND of its path; nothing else is touched.
		const TMap<int32, TArray<FCondition>> Expected = ExpectedTyphoeusConditions();
		TMap<int32, TArray<FCondition>> Actual;
		for (const UK2Node_CallFunction* Show : Counts.Shows)
		{
			const UEdGraphPin* IDPin = Show->FindPin(TEXT("MaterialID"));
			const int32 MaterialID = IDPin ? FCString::Atoi(*IDPin->DefaultValue) : INDEX_NONE;
			if (!Expected.Contains(MaterialID))
			{
				TestTrue(*FString::Printf(TEXT("%s: material %d belongs to the legacy toggle"), Pass, MaterialID), MaterialID >= 20 && MaterialID <= 22);
				continue;
			}
			TArray<FCondition> Conditions;
			TestTrue(*FString::Printf(TEXT("%s: material %d has a readable visibility formula"), Pass, MaterialID), ExtractConditions(Show, Conditions));
			TestFalse(*FString::Printf(TEXT("%s: material %d is applied once"), Pass, MaterialID), Actual.Contains(MaterialID));
			Actual.Add(MaterialID, Conditions);
		}
		TestEqual(*FString::Printf(TEXT("%s: every tree material is applied"), Pass), Actual.Num(), Expected.Num());
		for (const TPair<int32, TArray<FCondition>>& Pair : Expected)
		{
			const TArray<FCondition>* Conditions = Actual.Find(Pair.Key);
			TestTrue(*FString::Printf(TEXT("%s: material %d visible = AND of its path"), Pass, Pair.Key), Conditions && SameConditions(*Conditions, Pair.Value));
		}
		// Simulated runtime states: only the parts on the active path show.
		auto VisibleSet = [&Actual](const TMap<FName, int32>& State)
		{
			TSet<int32> Visible;
			for (const TPair<int32, TArray<FCondition>>& Pair : Actual)
			{
				if (Evaluate(Pair.Value, State))
				{
					Visible.Add(Pair.Key);
				}
			}
			return Visible;
		};
		const TMap<FName, int32> StyleB = MakeState(1, 1, 0, 0);
		const TMap<FName, int32> StyleC = MakeState(2, 1, 0, 0);
		const TMap<FName, int32> StyleA = MakeState(0, 0, 0, 0);
		TestTrue(*FString::Printf(TEXT("%s: style B shows its parts and the constant part"), Pass), VisibleSet(StyleB).Difference(TSet<int32>({ 4, 5, 6, 9 })).IsEmpty() && VisibleSet(StyleB).Num() == 4);
		TestTrue(*FString::Printf(TEXT("%s: style C shows the direct part, A option 1 and stocking"), Pass), VisibleSet(StyleC).Difference(TSet<int32>({ 8, 9, 10, 11 })).IsEmpty() && VisibleSet(StyleC).Num() == 4);
		TestTrue(*FString::Printf(TEXT("%s: style A hides the empty stocking option"), Pass), VisibleSet(StyleA).Difference(TSet<int32>({ 1, 2, 12, 9 })).IsEmpty() && VisibleSet(StyleA).Num() == 4);
		return Counts;
	};

	const FGraphCounts FirstCounts = CheckGeneratedGraph(TEXT("first"));
	TestEqual(TEXT("Shared variables add exactly four Animation Blueprint variables"), AnimBlueprint->NewVariables.Num(), AnimVariablesBefore + 4);
	TestEqual(TEXT("Shared variables add exactly four SaveGame variables"), SaveBlueprint->NewVariables.Num(), SaveVariablesBefore + 4);

	// Regeneration replaces the managed region as a whole and never appends.
	const FHTNestedToggleGeneratorResult Second = FHTBlueprintToggleGenerator::GenerateNestedVisibility(Params);
	if (!TestTrue(TEXT("Repeated tree generation succeeds"), Second.bSuccess))
	{
		AddError(Second.ToDisplayString());
	}
	const FGraphCounts SecondCounts = CheckGeneratedGraph(TEXT("second"));
	TestEqual(TEXT("Regeneration leaves the same number of nodes"), SecondCounts.Nodes, FirstCounts.Nodes);
	TestEqual(TEXT("Regeneration adds no variables"), AnimBlueprint->NewVariables.Num(), AnimVariablesBefore + 4);

	// A reopened asset has no transient comment membership; the saved bounds must still recover the group.
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			Comment->ClearNodesUnderComment();
		}
	}
	FHTNestedToggleGeneratorParams Changed = Params;
	Changed.Graph.Parts.RemoveAll([](const FHTNestedTogglePart& Part) { return Part.NodeId == TEXT("P9"); });
	const FHTNestedToggleGeneratorResult Third = FHTBlueprintToggleGenerator::GenerateNestedVisibility(Changed);
	if (!TestTrue(TEXT("Regeneration without comment membership succeeds"), Third.bSuccess))
	{
		AddError(Third.ToDisplayString());
	}
	const FGraphCounts ThirdCounts = CountGraph(Graph, TreePrefix, ApplyEvent);
	TestEqual(TEXT("Bounds recovery leaves one tree comment"), ThirdCounts.TreeComments, 1);
	TestEqual(TEXT("Bounds recovery leaves one Apply event"), ThirdCounts.ApplyEvents, 1);
	TestEqual(TEXT("Bounds recovery removes the old part"), ThirdCounts.ShowCalls, 12 + 11);
	TestEqual(TEXT("Bounds recovery removes the old key handlers"), ThirdCounts.WasPressed, 4 + 1);
	TestEqual(TEXT("Bounds recovery keeps the legacy toggle"), ThirdCounts.Switches, 2);
	TestTrue(TEXT("Graph still compiles after bounds recovery"), AnimBlueprint->Status != BS_Error);
	return !HasAnyErrors();
}

#endif
