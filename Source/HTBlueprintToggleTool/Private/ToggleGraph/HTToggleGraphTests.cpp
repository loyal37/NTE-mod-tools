#include "HTToggleGraph.h"
#include "HTToggleGraphCompiler.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/AnimBlueprintFactory.h"
#include "GameFramework/SaveGame.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace HTToggleGraphTests
{
	template <typename NodeType>
	static NodeType* Spawn(UEdGraph* Graph, const float X, const float Y)
	{
		return Cast<NodeType>(FHTToggleGraphSchemaAction_NewNode::SpawnNode(Graph, NodeType::StaticClass(), FVector2f(X, Y), nullptr, false));
	}

	static UHTToggleGraphNode_Switch* SpawnSwitch(UEdGraph* Graph, const float X, const float Y, const FString& Label, const FString& Key, const int32 Options, const FString& Variable = FString())
	{
		UHTToggleGraphNode_Switch* Switch = Spawn<UHTToggleGraphNode_Switch>(Graph, X, Y);
		Switch->Label = Label;
		Switch->KeyName = Key;
		Switch->OptionCount = Options;
		if (!Variable.IsEmpty())
		{
			Switch->VariableName = Variable;
		}
		Switch->ReconstructNode();
		return Switch;
	}

	static UHTToggleGraphNode_Part* SpawnPart(UEdGraph* Graph, const float X, const float Y, const TArray<int32>& MaterialIDs)
	{
		UHTToggleGraphNode_Part* Part = Spawn<UHTToggleGraphNode_Part>(Graph, X, Y);
		Part->MaterialIDs = MaterialIDs;
		return Part;
	}

	static bool Link(UEdGraph* Graph, UEdGraphPin* Output, UEdGraphPin* Input)
	{
		return Output && Input && Graph->GetSchema()->TryCreateConnection(Output, Input);
	}

	static UHTToggleGraphNode_Generate* FindGenerate(const UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UHTToggleGraphNode_Generate* Generate = Cast<UHTToggleGraphNode_Generate>(Node))
			{
				return Generate;
			}
		}
		return nullptr;
	}

	static const FHTNestedTogglePart* FindPartByMaterial(const FHTNestedToggleGraph& Model, const int32 MaterialID)
	{
		return Model.Parts.FindByPredicate([MaterialID](const FHTNestedTogglePart& Part) { return Part.MaterialIDs.Contains(MaterialID); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphCompileTest,
	"HT.BlueprintToggleTool.ToggleGraphEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphCompileTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphTests;
	const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraph_%s"), *RunID));
	Package->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};

	UHTToggleGraph* Asset = NewObject<UHTToggleGraph>(Package, TEXT("TG_Typhoeus"), RF_Transient | RF_Public);
	UEdGraph* Graph = Asset->EnsureGraph();
	if (!TestNotNull(TEXT("Asset graph"), Graph))
	{
		return false;
	}
	UHTToggleGraphNode_Generate* Generate = FindGenerate(Graph);
	if (!TestNotNull(TEXT("A new graph starts with a generate node"), Generate))
	{
		return false;
	}
	TestEqual(TEXT("Default tree name is the asset name"), Asset->GetEffectiveTreeName(), FString(TEXT("TG_Typhoeus")));

	// Typhoeus-like tree: style (3 options) under generate; per style, part switches sharing Alt 6/7;
	// a group collecting two parts; a constant part; a switch with an explicit shared variable.
	UHTToggleGraphNode_Switch* Style = SpawnSwitch(Graph, 0, 0, TEXT("Outfit style"), TEXT("alt 5"), 3, TEXT("Style"));
	Style->OptionLabels = { TEXT("A"), TEXT("B"), TEXT("C") };
	Style->InitialOption = 1;
	UHTToggleGraphNode_Switch* A0 = SpawnSwitch(Graph, -400, -300, TEXT("Coat"), TEXT("alt 6"), 2);
	UHTToggleGraphNode_Switch* B0 = SpawnSwitch(Graph, -400, 0, TEXT("Skirt"), TEXT("alt 7"), 2);
	UHTToggleGraphNode_Switch* A1 = SpawnSwitch(Graph, -400, 300, TEXT("Coat"), TEXT("ALT 6"), 2);
	UHTToggleGraphNode_Switch* C2 = SpawnSwitch(Graph, -400, 600, TEXT("Stocking"), TEXT("alt 8"), 2, TEXT("Stocking"));
	UHTToggleGraphNode_Group* Group = Spawn<UHTToggleGraphNode_Group>(Graph, -700, 0);
	UHTToggleGraphNode_Part* P1 = SpawnPart(Graph, -800, -300, { 1 });
	UHTToggleGraphNode_Part* P2 = SpawnPart(Graph, -1000, -50, { 2 });
	UHTToggleGraphNode_Part* P12 = SpawnPart(Graph, -1000, 50, { 12 });
	UHTToggleGraphNode_Part* P4 = SpawnPart(Graph, -800, 300, { 4 });
	UHTToggleGraphNode_Part* P9 = SpawnPart(Graph, -800, 900, { 9 });
	UHTToggleGraphNode_Part* P10 = SpawnPart(Graph, -800, 1200, { 10 });
	UHTToggleGraphNode_Part* P11 = SpawnPart(Graph, -800, 600, { 11 });
	UHTToggleGraphNode_Part* Orphan = SpawnPart(Graph, -800, 1500, { 99 });
	TestTrue(TEXT("Switch option pins follow the option count"), Style->GetOptionPin(2) != nullptr && Style->GetOptionPin(3) == nullptr);
	TestTrue(TEXT("New switches stay unnamed so same-key switches can share a variable"), A0->VariableName.IsEmpty() && B0->VariableName.IsEmpty() && A1->VariableName.IsEmpty());

	TestTrue(TEXT("Style -> generate"), Link(Graph, Style->GetOutputPin(), Generate->GetInputPin()));
	TestTrue(TEXT("A0 -> style option 0"), Link(Graph, A0->GetOutputPin(), Style->GetOptionPin(0)));
	TestTrue(TEXT("B0 -> style option 0"), Link(Graph, B0->GetOutputPin(), Style->GetOptionPin(0)));
	TestTrue(TEXT("A1 -> style option 1"), Link(Graph, A1->GetOutputPin(), Style->GetOptionPin(1)));
	TestTrue(TEXT("C2 -> style option 2"), Link(Graph, C2->GetOutputPin(), Style->GetOptionPin(2)));
	TestTrue(TEXT("P1 -> A0 option 0"), Link(Graph, P1->GetOutputPin(), A0->GetOptionPin(0)));
	TestTrue(TEXT("P2 -> group"), Link(Graph, P2->GetOutputPin(), Group->GetInputPin()));
	TestTrue(TEXT("P12 -> group"), Link(Graph, P12->GetOutputPin(), Group->GetInputPin()));
	TestTrue(TEXT("group -> B0 option 0"), Link(Graph, Group->GetOutputPin(), B0->GetOptionPin(0)));
	TestTrue(TEXT("P4 -> A1 option 1"), Link(Graph, P4->GetOutputPin(), A1->GetOptionPin(1)));
	TestTrue(TEXT("P9 -> generate"), Link(Graph, P9->GetOutputPin(), Generate->GetInputPin()));
	TestTrue(TEXT("P10 -> style option 2"), Link(Graph, P10->GetOutputPin(), Style->GetOptionPin(2)));
	TestTrue(TEXT("P11 -> C2 option 0"), Link(Graph, P11->GetOutputPin(), C2->GetOptionPin(0)));

	// Schema rules: one output link, no loops, no foreign/self links.
	TestTrue(TEXT("Reconnecting an output replaces the old link"), Link(Graph, P4->GetOutputPin(), A1->GetOptionPin(0)));
	TestEqual(TEXT("A part hangs on exactly one option"), P4->GetOutputPin()->LinkedTo.Num(), 1);
	TestTrue(TEXT("The old option lost its link"), A1->GetOptionPin(1)->LinkedTo.IsEmpty());
	TestFalse(TEXT("A loop is refused"), Link(Graph, Style->GetOutputPin(), A0->GetOptionPin(1)));
	TestEqual(TEXT("Refused loop keeps the generate link"), Style->GetOutputPin()->LinkedTo.Num() == 1 && Style->GetOutputPin()->LinkedTo[0]->GetOwningNode() == Generate, true);
	TestFalse(TEXT("Input to input is refused"), Link(Graph, A0->GetOptionPin(0), B0->GetOptionPin(1)));

	FHTNestedToggleGraph Model;
	TArray<FHTNestedToggleIssue> Errors;
	TArray<FString> Warnings;
	if (!TestTrue(TEXT("Graph compiles to the condition model"), FHTToggleGraphCompiler::CompileToModel(Asset, Model, Errors, Warnings)))
	{
		for (const FHTNestedToggleIssue& Error : Errors)
		{
			AddError(Error.ToDisplayString());
		}
		return false;
	}
	TestEqual(TEXT("Attached switches are compiled"), Model.Switches.Num(), 5);
	TestEqual(TEXT("Attached parts are compiled"), Model.Parts.Num(), 7);
	TestEqual(TEXT("The orphan part is reported as ignored"), Warnings.Num(), 1);
	TestTrue(TEXT("The orphan warning names the node"), Warnings.Num() == 1 && Warnings[0].Contains(Orphan->GetNodeId()));
	const FHTNestedToggleSwitch* StyleModel = Model.Switches.FindByPredicate([Style](const FHTNestedToggleSwitch& Switch) { return Switch.NodeId == Style->GetNodeId(); });
	const FHTNestedToggleSwitch* B0Model = Model.Switches.FindByPredicate([B0](const FHTNestedToggleSwitch& Switch) { return Switch.NodeId == B0->GetNodeId(); });
	TestTrue(TEXT("Root switch has no parent"), StyleModel && StyleModel->ParentNodeId.IsEmpty() && StyleModel->VariableName == TEXT("Style") && StyleModel->OptionLabels.Num() == 3 && StyleModel->InitialOption == 1);
	TestTrue(TEXT("Child switch records its parent option"), B0Model && B0Model->ParentNodeId == Style->GetNodeId() && B0Model->ParentOption == 0);
	const FHTNestedTogglePart* P12Model = FindPartByMaterial(Model, 12);
	TestTrue(TEXT("A part behind a group takes the group's attachment"),
		P12Model && P12Model->Path.Num() == 2
		&& P12Model->Path[0].SwitchNodeId == Style->GetNodeId() && P12Model->Path[0].Option == 0
		&& P12Model->Path[1].SwitchNodeId == B0->GetNodeId() && P12Model->Path[1].Option == 0);
	const FHTNestedTogglePart* P4Model = FindPartByMaterial(Model, 4);
	TestTrue(TEXT("Rewired part reports its new option"), P4Model && P4Model->Path.Num() == 2 && P4Model->Path[1].Option == 0);
	const FHTNestedTogglePart* P9Model = FindPartByMaterial(Model, 9);
	TestTrue(TEXT("A part on the generate node is always visible"), P9Model && P9Model->Path.IsEmpty());
	const FHTNestedTogglePart* P10Model = FindPartByMaterial(Model, 10);
	TestTrue(TEXT("A part directly on the root switch has one condition"), P10Model && P10Model->Path.Num() == 1 && P10Model->Path[0].Option == 2);

	// The compiled model passes the generator validation and resolves shared keys.
	FHTNestedToggleCompiledGraph Compiled;
	Errors.Reset();
	if (!TestTrue(TEXT("Model passes nested toggle validation"), FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Model, Compiled, Errors)))
	{
		for (const FHTNestedToggleIssue& Error : Errors)
		{
			AddError(Error.ToDisplayString());
		}
	}
	TestEqual(TEXT("Shared Alt 6 switches and the rest resolve to four variables"), Compiled.Variables.Num(), 4);
	TArray<FString> VariableNames;
	for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
	{
		VariableNames.Add(Variable.VariableName.ToString());
	}
	TestTrue(TEXT("Unnamed switches derive their shared variable from the key"), VariableNames.Contains(TEXT("TG_Typhoeus_Alt_Six")) && VariableNames.Contains(TEXT("TG_Typhoeus_Alt_Seven")));
	TestTrue(TEXT("Named switches keep their names"), VariableNames.Contains(TEXT("Style")) && VariableNames.Contains(TEXT("Stocking")));

	// Errors come back with ids the editor can resolve to nodes.
	P11->MaterialIDs = { 4 };
	Model = FHTNestedToggleGraph();
	Errors.Reset();
	Warnings.Reset();
	TestTrue(TEXT("Graph still compiles to a model with a duplicated material"), FHTToggleGraphCompiler::CompileToModel(Asset, Model, Errors, Warnings));
	TestFalse(TEXT("Duplicated material ID is rejected by validation"), FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Model, Compiled, Errors));
	bool bResolved = false;
	for (const FHTNestedToggleIssue& Error : Errors)
	{
		if (Error.NodeIds.Contains(P4->GetNodeId()) && Error.NodeIds.Contains(P11->GetNodeId()))
		{
			bResolved = FHTToggleGraphCompiler::FindNodeById(Asset, P4->GetNodeId()) == P4
				&& FHTToggleGraphCompiler::FindNodeById(Asset, P11->GetNodeId()) == P11;
		}
	}
	TestTrue(TEXT("Error node ids resolve to the graph nodes"), bResolved);
	P11->MaterialIDs = { 11 };

	// Full generation through the asset into isolated template Blueprints.
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = UAnimInstance::StaticClass();
	Factory->bTemplate = true;
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(), Package, TEXT("ABP_ToggleGraph"), RF_Transient | RF_Public, nullptr, GWarn));
	UBlueprint* SaveBlueprint = FKismetEditorUtilities::CreateBlueprint(
		USaveGame::StaticClass(), Package, TEXT("SG_ToggleGraph"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Isolated Animation Blueprint"), AnimBlueprint) || !TestNotNull(TEXT("Isolated SaveGame Blueprint"), SaveBlueprint))
	{
		return false;
	}
	const FHTNestedToggleGeneratorResult Unbound = FHTToggleGraphCompiler::Generate(Asset, false);
	TestFalse(TEXT("Generation without bound Blueprints is refused"), Unbound.bSuccess);

	Asset->AnimBlueprint = AnimBlueprint;
	Asset->SaveGameBlueprint = SaveBlueprint;
	Asset->CharacterName = TEXT("Typhoeus");
	Asset->TreeName = TEXT("Typhoeus");
	const FHTNestedToggleGeneratorResult Result = FHTToggleGraphCompiler::Generate(Asset, false);
	if (!TestTrue(TEXT("Generation from the graph asset succeeds"), Result.bSuccess))
	{
		AddError(Result.ToDisplayString());
		return false;
	}
	TestTrue(TEXT("Animation Blueprint compiles"), AnimBlueprint->Status != BS_Error);
	TestTrue(TEXT("Orphan warning is reported with the result"), Result.Messages.ContainsByPredicate([](const FString& Message) { return Message.Contains(TEXT("ignored")); }));
	int32 TreeComments = 0;
	int32 ApplyEvents = 0;
	if (UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint))
	{
		for (const UEdGraphNode* Node : EventGraph->Nodes)
		{
			if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
			{
				TreeComments += Comment->NodeComment.StartsWith(TEXT("HT Toggle Tree - Typhoeus;")) ? 1 : 0;
			}
			else if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
			{
				ApplyEvents += Event->CustomFunctionName == TEXT("HTN_Apply_Typhoeus") ? 1 : 0;
			}
		}
	}
	TestEqual(TEXT("The tree region is written once"), TreeComments, 1);
	TestEqual(TEXT("The Apply event is written once"), ApplyEvents, 1);
	TestTrue(TEXT("Shared variables exist on the Animation Blueprint"),
		FindFProperty<FIntProperty>(AnimBlueprint->GeneratedClass, TEXT("Stocking")) != nullptr
		&& FindFProperty<FIntProperty>(AnimBlueprint->GeneratedClass, TEXT("Style")) != nullptr
		&& FindFProperty<FIntProperty>(AnimBlueprint->GeneratedClass, TEXT("Typhoeus_Alt_Six")) != nullptr);
	return !HasAnyErrors();
}

#endif
