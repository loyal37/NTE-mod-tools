#include "HTToggleGraph.h"
#include "HTToggleGraphCompiler.h"
#include "HTToggleGraphNodeWidgets.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/AnimBlueprintFactory.h"
#include "GameFramework/SaveGame.h"
#include "GraphEditor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "NodeFactory.h"
#include "RenderingThread.h"
#include "SGraphPanel.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace HTToggleGraphNodeWidgetTests
{
	static bool ContainsCJK(const FString& Text)
	{
		for (const TCHAR Character : Text)
		{
			if ((Character >= 0x2E80 && Character <= 0x9FFF) || (Character >= 0xF900 && Character <= 0xFAFF) || (Character >= 0xFF00 && Character <= 0xFFEF))
			{
				return true;
			}
		}
		return false;
	}

	template <typename NodeType>
	static NodeType* Spawn(UEdGraph* Graph, const float X, const float Y)
	{
		return Cast<NodeType>(FHTToggleGraphSchemaAction_NewNode::SpawnNode(Graph, NodeType::StaticClass(), FVector2f(X, Y), nullptr, false));
	}

	static bool Link(UEdGraph* Graph, UEdGraphPin* Output, UEdGraphPin* Input)
	{
		return Output && Input && Graph->GetSchema()->TryCreateConnection(Output, Input);
	}

	static bool IsLinked(const UEdGraphPin* Output, const UEdGraphPin* Input)
	{
		return Output && Input && Output->LinkedTo.Contains(Input) && Input->LinkedTo.Contains(Output);
	}

	static int32 CountInputPins(const UEdGraphNode* Node)
	{
		int32 Count = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			Count += Pin && Pin->Direction == EGPD_Input ? 1 : 0;
		}
		return Count;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphInlineEditTest,
	"HT.BlueprintToggleTool.ToggleGraphInlineEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphInlineEditTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphNodeWidgetTests;
	if (!TestTrue(TEXT("Editor transactions are available"), GEditor && GEditor->Trans))
	{
		return false;
	}
	// Undo needs transactional, non-transient objects; /Temp packages are never saved.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraphInline_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	// Blueprint compiles inside the editor can collect garbage; keep the test objects alive.
	TStrongObjectPtr<UHTToggleGraph> AssetRef(NewObject<UHTToggleGraph>(Package, TEXT("TG_Inline"), RF_Public | RF_Transactional));
	UHTToggleGraph* Asset = AssetRef.Get();
	UEdGraph* Graph = Asset->EnsureGraph();
	UHTToggleGraphNode_Generate* Generate = FindGenerate(Graph);
	UHTToggleGraphNode_Switch* Switch = Spawn<UHTToggleGraphNode_Switch>(Graph, 0, 0);
	UHTToggleGraphNode_Part* Parts[4] = {
		Spawn<UHTToggleGraphNode_Part>(Graph, -400, -200),
		Spawn<UHTToggleGraphNode_Part>(Graph, -400, 0),
		Spawn<UHTToggleGraphNode_Part>(Graph, -400, 200),
		Spawn<UHTToggleGraphNode_Part>(Graph, -400, 400) };
	if (!TestNotNull(TEXT("Generate node"), Generate) || !TestNotNull(TEXT("Switch node"), Switch))
	{
		return false;
	}
	TestEqual(TEXT("A new generate node has one empty input"), CountInputPins(Generate), 1);
	TestTrue(TEXT("Switch -> generate"), Link(Graph, Switch->GetOutputPin(), Generate->GetInputPin()));
	TestEqual(TEXT("Connecting the generate node's last input adds an empty one"), CountInputPins(Generate), 2);

	// Option count +: new pins appear and existing links stay.
	TestEqual(TEXT("A new switch has two option pins"), CountInputPins(Switch), 2);
	TestTrue(TEXT("Part 0 -> option 1"), Link(Graph, Parts[0]->GetOutputPin(), Switch->GetOptionPin(0)));
	TestTrue(TEXT("Part 1 -> option 2"), Link(Graph, Parts[1]->GetOutputPin(), Switch->GetOptionPin(1)));
	TestTrue(TEXT("Option count can be raised inline"), Switch->SetOptionCountInline(4));
	TestEqual(TEXT("Raising the option count adds pins"), CountInputPins(Switch), 4);
	TestTrue(TEXT("Option 1 keeps its link"), IsLinked(Parts[0]->GetOutputPin(), Switch->GetOptionPin(0)));
	TestTrue(TEXT("Option 2 keeps its link"), IsLinked(Parts[1]->GetOutputPin(), Switch->GetOptionPin(1)));
	TestTrue(TEXT("The output keeps its link"), IsLinked(Switch->GetOutputPin(), Generate->GetInputPin()));
	TestTrue(TEXT("Part 2 -> option 3"), Link(Graph, Parts[2]->GetOutputPin(), Switch->GetOptionPin(2)));
	TestTrue(TEXT("Part 3 -> option 4"), Link(Graph, Parts[3]->GetOutputPin(), Switch->GetOptionPin(3)));
	TestTrue(TEXT("Initial option can be set inline"), Switch->SetInitialOptionInline(3));
	TestTrue(TEXT("Option labels can be set inline"), Switch->SetOptionLabelInline(3, TEXT(" Skirt ")));
	TestEqual(TEXT("Option labels are trimmed"), Switch->GetOptionLabel(3), FString(TEXT("Skirt")));

	// Option count -: trailing pins and their links go, the rest stays.
	TestTrue(TEXT("Option count can be lowered inline"), Switch->SetOptionCountInline(2));
	TestEqual(TEXT("Lowering the option count removes trailing pins"), CountInputPins(Switch), 2);
	TestTrue(TEXT("Option 1 survives lowering"), IsLinked(Parts[0]->GetOutputPin(), Switch->GetOptionPin(0)));
	TestTrue(TEXT("Option 2 survives lowering"), IsLinked(Parts[1]->GetOutputPin(), Switch->GetOptionPin(1)));
	TestTrue(TEXT("Removed options release their parts"), Parts[2]->GetOutputPin()->LinkedTo.IsEmpty() && Parts[3]->GetOutputPin()->LinkedTo.IsEmpty());
	TestEqual(TEXT("Initial option is clamped to the remaining options"), Switch->InitialOption, 1);
	TestEqual(TEXT("Labels of removed options are dropped"), Switch->OptionLabels.Num(), 0);
	TestFalse(TEXT("Option count cannot go below two"), Switch->SetOptionCountInline(1));

	// Undo restores the removed pins and both ends of their links; redo removes them again.
	TestTrue(TEXT("Undo the option count change"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores four option pins"), CountInputPins(Switch), 4);
	TestEqual(TEXT("Undo restores the option count"), Switch->OptionCount, 4);
	TestTrue(TEXT("Undo restores option 3 link"), IsLinked(Parts[2]->GetOutputPin(), Switch->GetOptionPin(2)));
	TestTrue(TEXT("Undo restores option 4 link"), IsLinked(Parts[3]->GetOutputPin(), Switch->GetOptionPin(3)));
	TestTrue(TEXT("Undo keeps option 1 link"), IsLinked(Parts[0]->GetOutputPin(), Switch->GetOptionPin(0)));
	TestEqual(TEXT("Undo restores the initial option"), Switch->InitialOption, 3);
	TestEqual(TEXT("Undo restores the option label"), Switch->GetOptionLabel(3), FString(TEXT("Skirt")));
	TestTrue(TEXT("Redo the option count change"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo removes the trailing pins again"), CountInputPins(Switch), 2);
	TestTrue(TEXT("Redo releases part 3"), Parts[2]->GetOutputPin()->LinkedTo.IsEmpty());
	TestTrue(TEXT("Undo again for the following checks"), GEditor->UndoTransaction());

	// Text fields.
	TestTrue(TEXT("Comment can be set inline"), Switch->SetLabelInline(TEXT("Coat")));
	TestTrue(TEXT("Variable can be set inline"), Switch->SetVariableNameInline(TEXT("CoatState")));
	TestTrue(TEXT("Key can be set inline"), Switch->SetKeyNameInline(TEXT("alt 6")));
	TestFalse(TEXT("Unchanged text creates no edit"), Switch->SetKeyNameInline(TEXT("alt 6")));
	TestEqual(TEXT("Title reflects the inline fields"), Switch->GetDisplayLabel(), FString(TEXT("Switch Coat [alt 6]")));
	TestTrue(TEXT("Undo the key edit"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores the previous key"), Switch->KeyName.IsEmpty());
	TestEqual(TEXT("Earlier edits stay"), Switch->VariableName, FString(TEXT("CoatState")));

	// Part material IDs.
	TArray<int32> IDs;
	TestTrue(TEXT("Material ID lists accept commas and spaces"), UHTToggleGraphNode_Part::ParseMaterialIDs(TEXT("12, 2 1,,"), IDs));
	TestTrue(TEXT("Parsed IDs keep their values"), IDs == TArray<int32>({ 12, 2, 1 }));
	TestFalse(TEXT("Invalid tokens are rejected"), UHTToggleGraphNode_Part::ParseMaterialIDs(TEXT("1, x"), IDs));
	TestFalse(TEXT("Negative IDs are rejected"), UHTToggleGraphNode_Part::ParseMaterialIDs(TEXT("-1"), IDs));
	TestTrue(TEXT("Material IDs can be set inline"), Parts[0]->SetMaterialIDsInline({ 12, 2, 1, 2 }));
	TestTrue(TEXT("Material IDs are sorted and unique"), Parts[0]->MaterialIDs == TArray<int32>({ 1, 2, 12 }));
	TestTrue(TEXT("A slot can be ticked"), Parts[0]->SetMaterialIDSelectedInline(5, true));
	TestTrue(TEXT("A slot can be unticked"), Parts[0]->SetMaterialIDSelectedInline(2, false));
	TestEqual(TEXT("Material IDs text round-trips"), Parts[0]->GetMaterialIDsText(), FString(TEXT("1, 5, 12")));
	TestTrue(TEXT("Undo the untick"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores the unticked slot"), Parts[0]->MaterialIDs == TArray<int32>({ 1, 2, 5, 12 }));

	// Group inputs grow and shrink by themselves: always exactly one empty input at the end.
	// Each connect/disconnect runs in a transaction, as the graph editor does for drags and breaks.
	const UEdGraphSchema* Schema = Graph->GetSchema();
	UHTToggleGraphNode_Group* Group = Spawn<UHTToggleGraphNode_Group>(Graph, -800, 0);
	UHTToggleGraphNode_Part* Members[3] = {
		Spawn<UHTToggleGraphNode_Part>(Graph, -1200, -200),
		Spawn<UHTToggleGraphNode_Part>(Graph, -1200, 0),
		Spawn<UHTToggleGraphNode_Part>(Graph, -1200, 200) };
	auto Connect = [&](UHTToggleGraphNode_Part* Part, const int32 Input)
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Connect")));
		return Link(Graph, Part->GetOutputPin(), Group->GetGroupInputPin(Input));
	};
	auto Disconnect = [&](UHTToggleGraphNode_Part* Part, const int32 Input)
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Disconnect")));
		Schema->BreakSinglePinLink(Part->GetOutputPin(), Group->GetGroupInputPin(Input));
	};
	TestEqual(TEXT("A new group has one empty input"), CountInputPins(Group), 1);
	TestTrue(TEXT("Member 1 -> group input 1"), Connect(Members[0], 0));
	TestEqual(TEXT("Connecting the last input adds an empty one"), CountInputPins(Group), 2);
	TestTrue(TEXT("The new input is empty"), Group->GetGroupInputPin(1) && Group->GetGroupInputPin(1)->LinkedTo.IsEmpty());
	TestTrue(TEXT("Member 2 -> group input 2"), Connect(Members[1], 1));
	TestTrue(TEXT("Member 3 -> group input 3"), Connect(Members[2], 2));
	TestEqual(TEXT("Every filled last input adds one more"), CountInputPins(Group), 4);
	TestEqual(TEXT("New inputs are labeled in order"), Group->GetGroupInputPin(3)->PinFriendlyName.ToString(), FString(TEXT("In 4")));

	Disconnect(Members[1], 1);
	TestEqual(TEXT("A middle input stays when it is disconnected"), CountInputPins(Group), 4);
	TestTrue(TEXT("Later links keep their inputs"), IsLinked(Members[2]->GetOutputPin(), Group->GetGroupInputPin(2)));
	Disconnect(Members[2], 2);
	TestEqual(TEXT("Disconnecting the last link shrinks to one empty input after the last link"), CountInputPins(Group), 2);
	TestTrue(TEXT("The first link stays"), IsLinked(Members[0]->GetOutputPin(), Group->GetGroupInputPin(0)));

	TestTrue(TEXT("Undo the last disconnect"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores the inputs"), CountInputPins(Group), 4);
	TestTrue(TEXT("Undo restores the link"), IsLinked(Members[2]->GetOutputPin(), Group->GetGroupInputPin(2)));
	TestTrue(TEXT("Undo the middle disconnect"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores the middle link"), IsLinked(Members[1]->GetOutputPin(), Group->GetGroupInputPin(1)));
	TestTrue(TEXT("Undo the third connect"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undoing a connect removes the input it added"), CountInputPins(Group), 3);
	TestTrue(TEXT("Redo the third connect"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo adds it again"), CountInputPins(Group), 4);

	// Moving a member elsewhere (the output can only hang on one place) also shrinks the group.
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Move")));
		TestTrue(TEXT("Member 3 moves to the generate node"), Link(Graph, Members[2]->GetOutputPin(), Generate->GetDynamicInputPin(1)));
	}
	TestEqual(TEXT("Taking the last member away shrinks the group"), CountInputPins(Group), 3);
	TestEqual(TEXT("The generate node grew for its new link"), CountInputPins(Generate), 3);

	// Deleting a member node notifies the group through NodeConnectionListChanged.
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Delete")));
		Graph->Modify();
		Members[1]->Modify();
		Members[1]->DestroyNode();
	}
	TestEqual(TEXT("Deleting the last member shrinks the group"), CountInputPins(Group), 2);
	TestTrue(TEXT("Exactly one empty input remains at the end"), Group->GetGroupInputPin(1) && Group->GetGroupInputPin(1)->LinkedTo.IsEmpty() && !Group->GetGroupInputPin(0)->LinkedTo.IsEmpty());
	TestEqual(TEXT("The first group input keeps the v1.5.30 pin name"), Group->GetGroupInputPin(0) ? Group->GetGroupInputPin(0)->PinName : NAME_None, UHTToggleGraphNode::InputPinName);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphLegacyPinNamesTest,
	"HT.BlueprintToggleTool.ToggleGraphLegacyPinNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphLegacyPinNamesTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphNodeWidgetTests;
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraphLegacyPins_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Package->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	TStrongObjectPtr<UHTToggleGraph> SourceRef(NewObject<UHTToggleGraph>(Package, TEXT("TG_Legacy"), RF_Public | RF_Transient));
	UEdGraph* Graph = SourceRef->EnsureGraph();
	UHTToggleGraphNode_Generate* Generate = FindGenerate(Graph);
	UHTToggleGraphNode_Switch* Switch = Spawn<UHTToggleGraphNode_Switch>(Graph, 0, 0);
	UHTToggleGraphNode_Group* Group = Spawn<UHTToggleGraphNode_Group>(Graph, -400, 0);
	UHTToggleGraphNode_Part* Part = Spawn<UHTToggleGraphNode_Part>(Graph, -800, 0);
	Part->MaterialIDs = { 3 };
	Link(Graph, Switch->GetOutputPin(), Generate->GetInputPin());
	Link(Graph, Group->GetOutputPin(), Switch->GetOptionPin(0));
	Link(Graph, Part->GetOutputPin(), Group->GetGroupInputPin(0));

	// The pin display names v1.5.30 serialized into its assets.
	const FText LegacyIn = FText::FromString(TEXT("\u8F93\u5165"));
	const FText LegacyOut = FText::FromString(TEXT("\u8F93\u51FA"));
	const FText LegacyObject = FText::FromString(TEXT("\u7269\u4F53"));
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				Pin->PinFriendlyName = Node == Part ? LegacyObject : LegacyOut;
			}
			else if (Pin->PinName.ToString().StartsWith(TEXT("Option_")))
			{
				Pin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("\u9009\u9879 %d"), Switch->GetOptionIndex(Pin) + 1));
			}
			else
			{
				Pin->PinFriendlyName = LegacyIn;
			}
		}
	}

	// Duplicating serializes the graph and runs PostLoad on every copied node, like opening the asset.
	TStrongObjectPtr<UHTToggleGraph> LoadedRef(DuplicateObject<UHTToggleGraph>(SourceRef.Get(), Package, TEXT("TG_Legacy_Loaded")));
	UHTToggleGraph* Loaded = LoadedRef.Get();
	if (!TestTrue(TEXT("The legacy graph round-trips"), Loaded && Loaded->Graph && Loaded->Graph != Graph))
	{
		return false;
	}
	UHTToggleGraphNode_Generate* LoadedGenerate = nullptr;
	UHTToggleGraphNode_Switch* LoadedSwitch = nullptr;
	UHTToggleGraphNode_Group* LoadedGroup = nullptr;
	UHTToggleGraphNode_Part* LoadedPart = nullptr;
	for (UEdGraphNode* Node : Loaded->Graph->Nodes)
	{
		LoadedGenerate = LoadedGenerate ? LoadedGenerate : Cast<UHTToggleGraphNode_Generate>(Node);
		LoadedSwitch = LoadedSwitch ? LoadedSwitch : Cast<UHTToggleGraphNode_Switch>(Node);
		LoadedGroup = LoadedGroup ? LoadedGroup : Cast<UHTToggleGraphNode_Group>(Node);
		LoadedPart = LoadedPart ? LoadedPart : Cast<UHTToggleGraphNode_Part>(Node);
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			TestFalse(*FString::Printf(TEXT("Loaded pin %s.%s is English: %s"), *Node->GetName(), *Pin->PinName.ToString(), *Pin->PinFriendlyName.ToString()), ContainsCJK(Pin->PinFriendlyName.ToString()));
		}
	}
	if (!TestTrue(TEXT("All four node types were loaded"), LoadedGenerate && LoadedSwitch && LoadedGroup && LoadedPart))
	{
		return false;
	}
	TestEqual(TEXT("Generate input reads In 1"), LoadedGenerate->GetInputPin()->PinFriendlyName.ToString(), FString(TEXT("In 1")));
	TestEqual(TEXT("Part output reads Out"), LoadedPart->GetOutputPin()->PinFriendlyName.ToString(), FString(TEXT("Out")));
	TestEqual(TEXT("Switch option reads Option 2"), LoadedSwitch->GetOptionPin(1)->PinFriendlyName.ToString(), FString(TEXT("Option 2")));
	TestEqual(TEXT("Group input reads In 1"), LoadedGroup->GetGroupInputPin(0)->PinFriendlyName.ToString(), FString(TEXT("In 1")));
	TestTrue(TEXT("Switch keeps its generate link"), IsLinked(LoadedSwitch->GetOutputPin(), LoadedGenerate->GetInputPin()));
	TestTrue(TEXT("Group keeps its option link"), IsLinked(LoadedGroup->GetOutputPin(), LoadedSwitch->GetOptionPin(0)));
	TestTrue(TEXT("Part keeps its group link"), IsLinked(LoadedPart->GetOutputPin(), LoadedGroup->GetGroupInputPin(0)));
	TestEqual(TEXT("A second refresh finds nothing left to change"), LoadedSwitch->RefreshPinDisplayNames(), 0);
	// The label the graph editor draws comes from GetPinDisplayName, even for a stale stored name.
	LoadedPart->GetOutputPin()->PinFriendlyName = LegacyObject;
	TestEqual(TEXT("Drawn pin labels ignore stale stored names"), LoadedPart->GetPinDisplayName(LoadedPart->GetOutputPin()).ToString(), FString(TEXT("Out")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphNodeWidgetsTest,
	"HT.BlueprintToggleTool.ToggleGraphNodeWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphNodeWidgetsTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphNodeWidgetTests;
	const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraphWidgets_%s"), *RunID));
	Package->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	// Blueprint compiles inside the editor can collect garbage; keep the test objects alive.
	TStrongObjectPtr<UHTToggleGraph> AssetRef(NewObject<UHTToggleGraph>(Package, TEXT("TG_Widgets"), RF_Transient | RF_Public));
	UHTToggleGraph* Asset = AssetRef.Get();
	UEdGraph* Graph = Asset->EnsureGraph();
	UHTToggleGraphNode_Generate* Generate = FindGenerate(Graph);
	Generate->NodePosX = 1180;
	Generate->NodePosY = 120;

	// A two-level tree like the user's outfits: style switch, part switches sharing keys, a group.
	UHTToggleGraphNode_Switch* Style = Spawn<UHTToggleGraphNode_Switch>(Graph, 820, 0);
	Style->Label = TEXT("Outfit style");
	Style->KeyName = TEXT("alt 5");
	Style->VariableName = TEXT("Style");
	Style->OptionCount = 3;
	Style->OptionLabels = { TEXT("A"), TEXT("B"), TEXT("C") };
	Style->ReconstructNode();
	UHTToggleGraphNode_Switch* CoatA = Spawn<UHTToggleGraphNode_Switch>(Graph, 420, -40);
	CoatA->Label = TEXT("Coat");
	CoatA->KeyName = TEXT("alt 6");
	UHTToggleGraphNode_Switch* CoatB = Spawn<UHTToggleGraphNode_Switch>(Graph, 420, 400);
	CoatB->Label = TEXT("Coat");
	CoatB->KeyName = TEXT("alt 6");
	UHTToggleGraphNode_Group* Group = Spawn<UHTToggleGraphNode_Group>(Graph, 60, 380);
	Group->Label = TEXT("Coat B");
	UHTToggleGraphNode_Part* PartA = Spawn<UHTToggleGraphNode_Part>(Graph, 60, 0);
	PartA->Label = TEXT("Coat A");
	PartA->MaterialIDs = { 3 };
	UHTToggleGraphNode_Part* PartB1 = Spawn<UHTToggleGraphNode_Part>(Graph, -280, 320);
	PartB1->Label = TEXT("Coat B body");
	PartB1->MaterialIDs = { 4, 5 };
	UHTToggleGraphNode_Part* PartB2 = Spawn<UHTToggleGraphNode_Part>(Graph, -280, 520);
	PartB2->Label = TEXT("Coat B belt");
	PartB2->MaterialIDs = { 6 };
	Link(Graph, Style->GetOutputPin(), Generate->GetInputPin());
	Link(Graph, CoatA->GetOutputPin(), Style->GetOptionPin(0));
	Link(Graph, CoatB->GetOutputPin(), Style->GetOptionPin(1));
	Link(Graph, PartA->GetOutputPin(), CoatA->GetOptionPin(0));
	Link(Graph, Group->GetOutputPin(), CoatB->GetOptionPin(0));
	Link(Graph, PartB1->GetOutputPin(), Group->GetGroupInputPin(0));
	Link(Graph, PartB2->GetOutputPin(), Group->GetGroupInputPin(1));

	// English titles, pins, properties and menu entries.
	TestEqual(TEXT("Switch title"), Style->GetDisplayLabel(), FString(TEXT("Switch Outfit style [alt 5]")));
	TestEqual(TEXT("Part title"), PartB1->GetDisplayLabel(), FString(TEXT("Part Coat B body (4,5)")));
	TestEqual(TEXT("Group title"), Group->GetDisplayLabel(), FString(TEXT("Group Coat B")));
	TestEqual(TEXT("Generate title"), Generate->GetDisplayLabel(), FString(TEXT("Generate")));
	TestEqual(TEXT("Option pin label"), Style->GetOptionPin(2)->PinFriendlyName.ToString(), FString(TEXT("Option 3")));
	TestEqual(TEXT("Output pin label"), Style->GetOutputPin()->PinFriendlyName.ToString(), FString(TEXT("Out")));
	TestEqual(TEXT("Group input label"), Group->GetGroupInputPin(1)->PinFriendlyName.ToString(), FString(TEXT("In 2")));
	for (UClass* Class : { UHTToggleGraph::StaticClass(), UHTToggleGraphNode_Part::StaticClass(), UHTToggleGraphNode_SwitchBase::StaticClass(), UHTToggleGraphNode_Switch::StaticClass(), UHTToggleGraphNode_TextureSwitch::StaticClass(), UHTToggleGraphNode_MaterialSwitch::StaticClass(), UHTToggleGraphNode_Group::StaticClass() })
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			TestFalse(*FString::Printf(TEXT("%s.%s display name is English"), *Class->GetName(), *It->GetName()), ContainsCJK(It->GetDisplayNameText().ToString()));
		}
	}
	FGraphContextMenuBuilder MenuBuilder(Graph);
	Graph->GetSchema()->GetGraphContextActions(MenuBuilder);
	TestEqual(TEXT("Right-click menu lists the six node types plus Collapse all and Expand all"), MenuBuilder.GetNumActions(), 8);
	for (int32 Index = 0; Index < MenuBuilder.GetNumActions(); ++Index)
	{
		const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
		TestFalse(TEXT("Menu entries are English"), !Action.IsValid() || ContainsCJK(Action->GetMenuDescription().ToString()) || ContainsCJK(Action->GetTooltipDescription().ToString()));
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		TestFalse(*FString::Printf(TEXT("%s title is English"), *Node->GetName()), ContainsCJK(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			TestFalse(TEXT("Pin labels are English"), ContainsCJK(Pin->PinFriendlyName.ToString()));
		}
	}

	// The registered factory builds the inline-editing widgets.
	struct FExpectedWidget
	{
		UEdGraphNode* Node;
		const TCHAR* Type;
		int32 Inputs;
		int32 Outputs;
	};
	const FExpectedWidget Expected[] = {
		{ Style, TEXT("SHTToggleGraphNode_Switch"), 3, 1 },
		{ PartA, TEXT("SHTToggleGraphNode_Part"), 0, 1 },
		{ Group, TEXT("SHTToggleGraphNode_Group"), 3, 1 },
		{ Generate, TEXT("SHTToggleGraphNode"), 2, 0 } };
	for (const FExpectedWidget& Entry : Expected)
	{
		TSharedPtr<SGraphNode> Widget = FNodeFactory::CreateNodeWidget(Entry.Node);
		if (!TestTrue(*FString::Printf(TEXT("%s uses %s"), *Entry.Node->GetName(), Entry.Type), Widget.IsValid() && Widget->GetTypeAsString() == Entry.Type))
		{
			continue;
		}
		const TSharedPtr<SHTToggleGraphNode> ToggleWidget = StaticCastSharedPtr<SHTToggleGraphNode>(Widget);
		TestEqual(*FString::Printf(TEXT("%s input pin widgets"), Entry.Type), ToggleWidget->GetInputPinWidgetCount(), Entry.Inputs);
		TestEqual(*FString::Printf(TEXT("%s output pin widgets"), Entry.Type), ToggleWidget->GetOutputPinWidgetCount(), Entry.Outputs);
	}
	{
		TSharedPtr<SGraphNode> Widget = FNodeFactory::CreateNodeWidget(CoatB);
		const TSharedPtr<SHTToggleGraphNode> ToggleWidget = StaticCastSharedPtr<SHTToggleGraphNode>(Widget);
		CoatB->OptionCount = 4;
		CoatB->ReconstructNode();
		Widget->UpdateGraphNode();
		TestEqual(TEXT("Rebuilding the widget follows the new option pins"), ToggleWidget->GetInputPinWidgetCount(), 4);
		TestTrue(TEXT("The rebuilt switch keeps its group link"), CoatB->GetOptionPin(0)->LinkedTo.Contains(Group->GetOutputPin()));
		CoatB->OptionCount = 2;
		CoatB->ReconstructNode();
	}

	// Compile output is English end to end.
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = UAnimInstance::StaticClass();
	Factory->bTemplate = true;
	TStrongObjectPtr<UAnimBlueprint> AnimRef(Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, TEXT("ABP_Widgets"), RF_Transient | RF_Public, nullptr, GWarn)));
	TStrongObjectPtr<UBlueprint> SaveRef(FKismetEditorUtilities::CreateBlueprint(USaveGame::StaticClass(), Package, TEXT("SG_Widgets"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	Asset->AnimBlueprint = AnimRef.Get();
	Asset->SaveGameBlueprint = SaveRef.Get();
	Asset->TreeName = TEXT("Widgets");
	Asset->CharacterName = TEXT("Typhoeus");
	const FHTNestedToggleGeneratorResult Compiled = FHTToggleGraphCompiler::Generate(Asset, false);
	TestTrue(TEXT("The sample tree compiles"), Compiled.bSuccess);
	TestFalse(*FString::Printf(TEXT("Compile output is English: %s"), *Compiled.ToDisplayString()), ContainsCJK(Compiled.ToDisplayString()));
	CoatB->KeyName = TEXT("nosuchkey");
	const FHTNestedToggleGeneratorResult Failed = FHTToggleGraphCompiler::Generate(Asset, false);
	TestFalse(TEXT("A bad key fails"), Failed.bSuccess);
	TestFalse(TEXT("Compile errors are English"), ContainsCJK(Failed.ToDisplayString()));
	CoatB->KeyName = TEXT("alt 6");

	// Render the graph editor offscreen so the node layout can be reviewed as an image.
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Node screenshot skipped: rendering is unavailable."));
		return !HasAnyErrors();
	}
	TSharedRef<SGraphEditor> Editor = SNew(SGraphEditor)
		.GraphToEdit(Graph)
		.IsEditable(true);
	if (SGraphPanel* Panel = Editor->GetGraphPanel())
	{
		Panel->Update();
	}
	const FVector2D DrawSize(1840.0, 820.0);
	FWidgetRenderer* Renderer = new FWidgetRenderer(true, true);
	UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, true);
	if (!TestNotNull(TEXT("Screenshot render target"), Target))
	{
		BeginCleanup(Renderer);
		return false;
	}
	Target->AddToRoot();
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		Editor->SetViewLocation(FVector2f(-320.0f, -70.0f), 1.0f);
		Renderer->DrawWidget(Target, Editor, DrawSize, 0.05f, false);
		FlushRenderingCommands();
	}
	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	const bool bRead = Resource && Resource->ReadPixels(Pixels);
	Target->RemoveFromRoot();
	BeginCleanup(Renderer);
	if (!TestTrue(TEXT("Screenshot pixels can be read"), bRead && Pixels.Num() == static_cast<int32>(DrawSize.X * DrawSize.Y)))
	{
		return false;
	}
	TSet<uint32> Colors;
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
		Colors.Add(Pixel.ToPackedARGB());
	}
	TestTrue(*FString::Printf(TEXT("Screenshot shows drawn nodes (%d colors)"), Colors.Num()), Colors.Num() > 200);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HTToggleGraphNodes.png"));
	const FImageView Image(Pixels.GetData(), static_cast<int32>(DrawSize.X), static_cast<int32>(DrawSize.Y));
	TestTrue(TEXT("Screenshot PNG is written"), FImageUtils::SaveImageByExtension(*Path, Image));
	AddInfo(FString::Printf(TEXT("Toggle graph node screenshot: %s"), *FPaths::ConvertRelativePathToFull(Path)));
	return !HasAnyErrors();
}

#endif
