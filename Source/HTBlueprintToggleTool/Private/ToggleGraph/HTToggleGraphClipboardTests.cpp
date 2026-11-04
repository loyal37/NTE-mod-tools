#include "HTToggleGraph.h"
#include "HTToggleGraphEditor.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// Copy, cut, paste, duplicate and undo go through the same helpers as the editor commands
// (FHTToggleGraphEditor::ExportNodesToText / PasteNodesFromText / DeleteNodesFromGraph); only
// the system clipboard and the selection are left out, so the tests never touch the user's clipboard.
namespace HTToggleGraphClipboardTests
{
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

	static int32 CountLinks(const UEdGraphNode* Node)
	{
		int32 Count = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			Count += Pin ? Pin->LinkedTo.Num() : 0;
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

	template <typename NodeType>
	static NodeType* FindPasted(const TArray<UEdGraphNode*>& Pasted)
	{
		for (UEdGraphNode* Node : Pasted)
		{
			if (NodeType* Typed = Cast<NodeType>(Node))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	static UHTToggleGraphNode_Part* FindPastedPart(const TArray<UEdGraphNode*>& Pasted, const int32 MaterialID)
	{
		for (UEdGraphNode* Node : Pasted)
		{
			UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(Node);
			if (Part && Part->MaterialIDs == TArray<int32>{ MaterialID })
			{
				return Part;
			}
		}
		return nullptr;
	}

	// Every link is non-null, points at a live pin of a node in the same graph, and is reciprocal.
	static void CheckLinks(FAutomationTestBase& Test, const FString& Context, const UEdGraph* Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s has a null pin"), *Context, *Node->GetName()));
					continue;
				}
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					const UEdGraphNode* Owner = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
					if (!Linked || Linked->WasTrashed() || !Owner || !Graph->Nodes.Contains(Owner))
					{
						Test.AddError(FString::Printf(TEXT("%s: %s.%s has a dangling link"), *Context, *Node->GetName(), *Pin->PinName.ToString()));
					}
					else if (!Linked->LinkedTo.Contains(Pin))
					{
						Test.AddError(FString::Printf(TEXT("%s: %s.%s -> %s.%s is one-sided"), *Context, *Node->GetName(), *Pin->PinName.ToString(), *Owner->GetName(), *Linked->PinName.ToString()));
					}
				}
			}
		}
	}

	// PartA, PartB -> Group (In 1, In 2) -> Switch option 1; PartC -> Switch option 2; Switch -> Generate.
	struct FTree
	{
		TStrongObjectPtr<UHTToggleGraph> Asset;
		UEdGraph* Graph = nullptr;
		UHTToggleGraphNode_Generate* Generate = nullptr;
		UHTToggleGraphNode_Switch* Switch = nullptr;
		UHTToggleGraphNode_Group* Group = nullptr;
		UHTToggleGraphNode_Part* PartA = nullptr;
		UHTToggleGraphNode_Part* PartB = nullptr;
		UHTToggleGraphNode_Part* PartC = nullptr;

		bool IsIntact() const
		{
			return IsLinked(PartA->GetOutputPin(), Group->GetGroupInputPin(0))
				&& IsLinked(PartB->GetOutputPin(), Group->GetGroupInputPin(1))
				&& IsLinked(Group->GetOutputPin(), Switch->GetOptionPin(0))
				&& IsLinked(PartC->GetOutputPin(), Switch->GetOptionPin(1))
				&& IsLinked(Switch->GetOutputPin(), Generate->GetInputPin())
				&& CountInputPins(Group) == 3
				&& CountInputPins(Generate) == 2;
		}
	};

	static UPackage* MakePackage(const TCHAR* Prefix)
	{
		// Undo needs transactional, non-transient objects; /Temp packages are never saved.
		return CreatePackage(*FString::Printf(TEXT("/Temp/%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	static bool BuildTree(FAutomationTestBase& Test, UPackage* Package, const TCHAR* Name, FTree& Tree)
	{
		Tree.Asset.Reset(NewObject<UHTToggleGraph>(Package, Name, RF_Public | RF_Transactional));
		Tree.Graph = Tree.Asset->EnsureGraph();
		Tree.Generate = FindGenerate(Tree.Graph);
		Tree.Switch = Spawn<UHTToggleGraphNode_Switch>(Tree.Graph, 0, 0);
		Tree.Group = Spawn<UHTToggleGraphNode_Group>(Tree.Graph, -400, -100);
		Tree.PartA = Spawn<UHTToggleGraphNode_Part>(Tree.Graph, -800, -200);
		Tree.PartB = Spawn<UHTToggleGraphNode_Part>(Tree.Graph, -800, 0);
		Tree.PartC = Spawn<UHTToggleGraphNode_Part>(Tree.Graph, -400, 200);
		if (!Test.TestTrue(TEXT("Tree nodes spawn"), Tree.Generate && Tree.Switch && Tree.Group && Tree.PartA && Tree.PartB && Tree.PartC))
		{
			return false;
		}
		Tree.Switch->KeyName = TEXT("7");
		Tree.Group->Label = TEXT("Body");
		Tree.PartA->MaterialIDs = { 1 };
		Tree.PartB->MaterialIDs = { 2 };
		Tree.PartC->MaterialIDs = { 3 };
		Link(Tree.Graph, Tree.PartA->GetOutputPin(), Tree.Group->GetGroupInputPin(0));
		Link(Tree.Graph, Tree.PartB->GetOutputPin(), Tree.Group->GetGroupInputPin(1));
		Link(Tree.Graph, Tree.Group->GetOutputPin(), Tree.Switch->GetOptionPin(0));
		Link(Tree.Graph, Tree.PartC->GetOutputPin(), Tree.Switch->GetOptionPin(1));
		Link(Tree.Graph, Tree.Switch->GetOutputPin(), Tree.Generate->GetInputPin());
		return Test.TestTrue(TEXT("Tree is wired"), Tree.IsIntact());
	}

	static FString Copy(std::initializer_list<UEdGraphNode*> Nodes)
	{
		FGraphPanelSelectionSet Selection;
		for (UEdGraphNode* Node : Nodes)
		{
			Selection.Add(Node);
		}
		return FHTToggleGraphEditor::ExportNodesToText(Selection);
	}

	// Checks a pasted copy of PartA, PartB, Group and Switch: inner links kept, outer links gone.
	static void CheckPastedSet(FAutomationTestBase& Test, const FString& Context, const UEdGraph* Graph, const TArray<UEdGraphNode*>& Pasted)
	{
		UHTToggleGraphNode_Switch* Switch = FindPasted<UHTToggleGraphNode_Switch>(Pasted);
		UHTToggleGraphNode_Group* Group = FindPasted<UHTToggleGraphNode_Group>(Pasted);
		UHTToggleGraphNode_Part* PartA = FindPastedPart(Pasted, 1);
		UHTToggleGraphNode_Part* PartB = FindPastedPart(Pasted, 2);
		if (!Test.TestTrue(*(Context + TEXT(": all four nodes pasted")), Pasted.Num() == 4 && Switch && Group && PartA && PartB))
		{
			return;
		}
		for (const UEdGraphNode* Node : Pasted)
		{
			Test.TestTrue(*FString::Printf(TEXT("%s: %s is in the graph"), *Context, *Node->GetName()), Graph->Nodes.Contains(Node));
		}
		Test.TestTrue(*(Context + TEXT(": PartA -> Group In 1 kept")), IsLinked(PartA->GetOutputPin(), Group->GetGroupInputPin(0)));
		Test.TestTrue(*(Context + TEXT(": PartB -> Group In 2 kept")), IsLinked(PartB->GetOutputPin(), Group->GetGroupInputPin(1)));
		Test.TestTrue(*(Context + TEXT(": Group -> Switch option 1 kept")), IsLinked(Group->GetOutputPin(), Switch->GetOptionPin(0)));
		Test.TestTrue(*(Context + TEXT(": link from the uncopied PartC dropped")), Switch->GetOptionPin(1) && Switch->GetOptionPin(1)->LinkedTo.IsEmpty());
		Test.TestTrue(*(Context + TEXT(": link to the uncopied Generate dropped")), Switch->GetOutputPin()->LinkedTo.IsEmpty());
		Test.TestEqual(*(Context + TEXT(": Group keeps two links plus one empty input")), CountInputPins(Group), 3);
		Test.TestEqual(*(Context + TEXT(": Switch keeps its two options")), CountInputPins(Switch), 2);
		Test.TestEqual(*(Context + TEXT(": Switch keeps its key")), Switch->KeyName, FString(TEXT("7")));
		CheckLinks(Test, Context, Graph);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphPasteSingleTest,
	"HT.BlueprintToggleTool.ToggleGraphPasteSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphPasteSingleTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphClipboardTests;
	if (!TestTrue(TEXT("Editor transactions are available"), GEditor && GEditor->Trans))
	{
		return false;
	}
	UPackage* Package = MakePackage(TEXT("HTToggleGraphPasteSingle"));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	FTree Tree;
	if (!BuildTree(*this, Package, TEXT("TG_PasteSingle"), Tree))
	{
		return false;
	}

	// One linked node on its own: every link leads outside the clipboard.
	struct FCase
	{
		const TCHAR* Name;
		UEdGraphNode* Node;
		int32 Inputs;
	};
	const FCase Cases[] = {
		{ TEXT("Switch"), Tree.Switch, 2 },
		{ TEXT("Part"), Tree.PartA, 0 },
		{ TEXT("Group"), Tree.Group, 1 },
	};
	float Y = 0.0f;
	for (const FCase& Case : Cases)
	{
		const int32 NodesBefore = Tree.Graph->Nodes.Num();
		const TArray<UEdGraphNode*> Pasted = FHTToggleGraphEditor::PasteNodesFromText(Tree.Graph, Copy({ Case.Node }), FVector2f(1200.0f, Y += 300.0f));
		const FString Context = FString::Printf(TEXT("Paste a linked %s alone"), Case.Name);
		if (!TestEqual(*(Context + TEXT(": one node pasted")), Pasted.Num(), 1))
		{
			continue;
		}
		UEdGraphNode* PastedNode = Pasted[0];
		TestTrue(*(Context + TEXT(": same class")), PastedNode->GetClass() == Case.Node->GetClass());
		TestEqual(*(Context + TEXT(": graph grew by one")), Tree.Graph->Nodes.Num(), NodesBefore + 1);
		TestTrue(*(Context + TEXT(": new node GUID")), PastedNode->NodeGuid != Case.Node->NodeGuid);
		TestEqual(*(Context + TEXT(": no links survive")), CountLinks(PastedNode), 0);
		TestEqual(*(Context + TEXT(": input pins")), CountInputPins(PastedNode), Case.Inputs);
		TestNotNull(*(Context + TEXT(": output pin")), Cast<UHTToggleGraphNode>(PastedNode)->GetOutputPin());
		TestTrue(*(Context + TEXT(": the original keeps every link")), Tree.IsIntact());
		CheckLinks(*this, Context, Tree.Graph);
	}

	// Ctrl+D is Copy + Paste through the same helpers.
	const TArray<UEdGraphNode*> Duplicated = FHTToggleGraphEditor::PasteNodesFromText(Tree.Graph, Copy({ Tree.Switch }), FVector2f(1600.0f, 0.0f));
	UHTToggleGraphNode_Switch* Duplicate = FindPasted<UHTToggleGraphNode_Switch>(Duplicated);
	if (TestNotNull(TEXT("Duplicate a linked Switch"), Duplicate))
	{
		TestEqual(TEXT("Duplicated Switch has no links"), CountLinks(Duplicate), 0);
		TestEqual(TEXT("Duplicated Switch keeps its options"), CountInputPins(Duplicate), Tree.Switch->OptionCount);
		TestEqual(TEXT("Duplicated Switch keeps its key"), Duplicate->KeyName, Tree.Switch->KeyName);
		TestTrue(TEXT("Duplicate leaves the original wired"), Tree.IsIntact());
		CheckLinks(*this, TEXT("Duplicate a linked Switch"), Tree.Graph);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphPasteConnectedTest,
	"HT.BlueprintToggleTool.ToggleGraphPasteConnected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphPasteConnectedTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphClipboardTests;
	if (!TestTrue(TEXT("Editor transactions are available"), GEditor && GEditor->Trans))
	{
		return false;
	}
	UPackage* Package = MakePackage(TEXT("HTToggleGraphPasteConnected"));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	FTree Tree;
	if (!BuildTree(*this, Package, TEXT("TG_PasteConnected"), Tree))
	{
		return false;
	}
	const int32 OriginalCount = Tree.Graph->Nodes.Num();

	// A connected set, without PartC and Generate: inner links stay, links to the rest are dropped.
	const FString Text = Copy({ Tree.PartA, Tree.PartB, Tree.Group, Tree.Switch });
	const TArray<UEdGraphNode*> Pasted = FHTToggleGraphEditor::PasteNodesFromText(Tree.Graph, Text, FVector2f(1200.0f, 0.0f));
	CheckPastedSet(*this, TEXT("Paste a connected set"), Tree.Graph, Pasted);
	TestTrue(TEXT("Paste leaves the originals wired"), Tree.IsIntact());
	TestEqual(TEXT("Paste adds four nodes"), Tree.Graph->Nodes.Num(), OriginalCount + 4);

	TestTrue(TEXT("Undo the paste"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo removes the pasted nodes"), Tree.Graph->Nodes.Num(), OriginalCount);
	for (const UEdGraphNode* Node : Pasted)
	{
		TestFalse(TEXT("Pasted node left the graph"), Tree.Graph->Nodes.Contains(Node));
	}
	TestTrue(TEXT("Undo leaves the originals wired"), Tree.IsIntact());
	CheckLinks(*this, TEXT("Undo the paste"), Tree.Graph);

	TestTrue(TEXT("Redo the paste"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo brings the pasted nodes back"), Tree.Graph->Nodes.Num(), OriginalCount + 4);
	CheckPastedSet(*this, TEXT("Redo the paste"), Tree.Graph, Pasted);
	TestTrue(TEXT("Redo leaves the originals wired"), Tree.IsIntact());

	// Ctrl+D on the pasted copy.
	const TArray<UEdGraphNode*> Duplicated = FHTToggleGraphEditor::PasteNodesFromText(Tree.Graph, Copy({ Pasted[0], Pasted[1], Pasted[2], Pasted[3] }), FVector2f(2000.0f, 0.0f));
	CheckPastedSet(*this, TEXT("Duplicate the pasted set"), Tree.Graph, Duplicated);
	CheckPastedSet(*this, TEXT("Duplicate keeps the first copy"), Tree.Graph, Pasted);
	TestTrue(TEXT("Undo the duplicate"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo removes the duplicate only"), Tree.Graph->Nodes.Num(), OriginalCount + 4);
	CheckPastedSet(*this, TEXT("Undo the duplicate"), Tree.Graph, Pasted);
	TestTrue(TEXT("Redo the duplicate"), GEditor->RedoTransaction());
	CheckPastedSet(*this, TEXT("Redo the duplicate"), Tree.Graph, Duplicated);

	// Pasting the same text again (second Ctrl+V) gives an independent copy.
	const TArray<UEdGraphNode*> Again = FHTToggleGraphEditor::PasteNodesFromText(Tree.Graph, Text, FVector2f(1200.0f, 800.0f));
	CheckPastedSet(*this, TEXT("Paste the same text again"), Tree.Graph, Again);
	TestTrue(TEXT("Originals still wired"), Tree.IsIntact());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphCutAndCrossAssetPasteTest,
	"HT.BlueprintToggleTool.ToggleGraphCutAndCrossAssetPaste",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphCutAndCrossAssetPasteTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphClipboardTests;
	if (!TestTrue(TEXT("Editor transactions are available"), GEditor && GEditor->Trans))
	{
		return false;
	}
	UPackage* Package = MakePackage(TEXT("HTToggleGraphCut"));
	UPackage* OtherPackage = MakePackage(TEXT("HTToggleGraphCrossAsset"));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
		OtherPackage->SetDirtyFlag(false);
	};
	FTree Tree;
	if (!BuildTree(*this, Package, TEXT("TG_Cut"), Tree))
	{
		return false;
	}
	const int32 OriginalCount = Tree.Graph->Nodes.Num();

	// Cut = Copy + Delete.
	const FString CutText = Copy({ Tree.PartA, Tree.Group });
	FGraphPanelSelectionSet CutSelection;
	CutSelection.Add(Tree.PartA);
	CutSelection.Add(Tree.Group);
	FHTToggleGraphEditor::DeleteNodesFromGraph(Tree.Graph, CutSelection);
	TestFalse(TEXT("Cut removes the Group"), Tree.Graph->Nodes.Contains(Tree.Group));
	TestFalse(TEXT("Cut removes PartA"), Tree.Graph->Nodes.Contains(Tree.PartA));
	TestTrue(TEXT("Cut unlinks Switch option 1"), Tree.Switch->GetOptionPin(0)->LinkedTo.IsEmpty());
	TestTrue(TEXT("Cut unlinks PartB"), Tree.PartB->GetOutputPin()->LinkedTo.IsEmpty());
	CheckLinks(*this, TEXT("Cut"), Tree.Graph);

	const TArray<UEdGraphNode*> Pasted = FHTToggleGraphEditor::PasteNodesFromText(Tree.Graph, CutText, FVector2f(1200.0f, 0.0f));
	UHTToggleGraphNode_Group* PastedGroup = FindPasted<UHTToggleGraphNode_Group>(Pasted);
	UHTToggleGraphNode_Part* PastedPart = FindPastedPart(Pasted, 1);
	if (TestTrue(TEXT("Paste after cut gives Group and PartA"), Pasted.Num() == 2 && PastedGroup && PastedPart))
	{
		TestTrue(TEXT("PartA -> Group In 1 kept"), IsLinked(PastedPart->GetOutputPin(), PastedGroup->GetGroupInputPin(0)));
		TestTrue(TEXT("Group output not linked to the Switch"), PastedGroup->GetOutputPin()->LinkedTo.IsEmpty());
		TestEqual(TEXT("PartB link dropped, Group keeps one link plus one empty input"), CountInputPins(PastedGroup), 2);
		TestEqual(TEXT("PartB stays unlinked"), CountLinks(Tree.PartB), 0);
	}
	CheckLinks(*this, TEXT("Paste after cut"), Tree.Graph);

	TestTrue(TEXT("Undo the paste after cut"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo the cut"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores the node count"), Tree.Graph->Nodes.Num(), OriginalCount);
	TestTrue(TEXT("Undo the cut restores every link"), Tree.IsIntact());
	CheckLinks(*this, TEXT("Undo the cut"), Tree.Graph);
	TestTrue(TEXT("Redo the cut"), GEditor->RedoTransaction());
	TestFalse(TEXT("Redo removes the Group again"), Tree.Graph->Nodes.Contains(Tree.Group));
	CheckLinks(*this, TEXT("Redo the cut"), Tree.Graph);
	TestTrue(TEXT("Undo the cut again"), GEditor->UndoTransaction());
	TestTrue(TEXT("Second undo restores every link"), Tree.IsIntact());

	// Copy from one toggle graph asset, paste into another one.
	TStrongObjectPtr<UHTToggleGraph> Other(NewObject<UHTToggleGraph>(OtherPackage, TEXT("TG_Other"), RF_Public | RF_Transactional));
	UEdGraph* OtherGraph = Other->EnsureGraph();
	UHTToggleGraphNode_Generate* OtherGenerate = FindGenerate(OtherGraph);
	if (!TestNotNull(TEXT("Other graph has its Generate node"), OtherGenerate))
	{
		return false;
	}
	const TArray<UEdGraphNode*> Crossed = FHTToggleGraphEditor::PasteNodesFromText(OtherGraph, Copy({ Tree.PartA, Tree.PartB, Tree.Group, Tree.Switch }), FVector2f(0.0f, 0.0f));
	CheckPastedSet(*this, TEXT("Paste into another asset"), OtherGraph, Crossed);
	for (const UEdGraphNode* Node : Crossed)
	{
		TestTrue(TEXT("Pasted node belongs to the other graph"), Node->GetGraph() == OtherGraph);
	}
	TestEqual(TEXT("Other Generate stays unlinked"), CountLinks(OtherGenerate), 0);
	TestEqual(TEXT("Other Generate keeps one input"), CountInputPins(OtherGenerate), 1);
	TestTrue(TEXT("Source asset stays wired"), Tree.IsIntact());
	CheckLinks(*this, TEXT("Source after cross-asset paste"), Tree.Graph);
	TestTrue(TEXT("Undo the cross-asset paste"), GEditor->UndoTransaction());
	TestEqual(TEXT("Other graph is back to its Generate node"), OtherGraph->Nodes.Num(), 1);
	CheckLinks(*this, TEXT("Undo the cross-asset paste"), OtherGraph);
	TestTrue(TEXT("Redo the cross-asset paste"), GEditor->RedoTransaction());
	CheckPastedSet(*this, TEXT("Redo the cross-asset paste"), OtherGraph, Crossed);

	// A single linked Switch into the other asset: its links name nodes that do not exist there.
	const TArray<UEdGraphNode*> Single = FHTToggleGraphEditor::PasteNodesFromText(OtherGraph, Copy({ Tree.Switch }), FVector2f(0.0f, 800.0f));
	UHTToggleGraphNode_Switch* SingleSwitch = FindPasted<UHTToggleGraphNode_Switch>(Single);
	if (TestTrue(TEXT("Paste a single linked Switch into another asset"), Single.Num() == 1 && SingleSwitch))
	{
		TestEqual(TEXT("Cross-asset Switch has no links"), CountLinks(SingleSwitch), 0);
		TestEqual(TEXT("Cross-asset Switch keeps its options"), CountInputPins(SingleSwitch), 2);
	}
	CheckLinks(*this, TEXT("Single cross-asset paste"), OtherGraph);
	TestTrue(TEXT("Source asset stays wired after the single paste"), Tree.IsIntact());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphNodeRebuildTest,
	"HT.BlueprintToggleTool.ToggleGraphNodeRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphNodeRebuildTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphClipboardTests;
	UPackage* Package = MakePackage(TEXT("HTToggleGraphRebuild"));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	FTree Tree;
	if (!BuildTree(*this, Package, TEXT("TG_Rebuild"), Tree))
	{
		return false;
	}
	UHTToggleGraphNode_Switch* Switch = Tree.Switch;
	const TArray<UEdGraphPin*> PinsBefore = Switch->Pins;

	// The engine calls PostEditChange() without a property while importing pasted text and while
	// undoing; the node must not rebuild its pins then.
	Switch->PostEditChange();
	TestTrue(TEXT("PostEditChange without a property keeps the pins"), Switch->Pins == PinsBefore);

	// Initial option does not change pins or the title.
	FProperty* InitialOption = FindFProperty<FProperty>(UHTToggleGraphNode_Switch::StaticClass(), GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Switch, InitialOption));
	Switch->InitialOption = 1;
	FPropertyChangedEvent InitialEvent(InitialOption);
	Switch->PostEditChangeProperty(InitialEvent);
	TestTrue(TEXT("Editing Initial Option keeps the pins"), Switch->Pins == PinsBefore);

	// A Details panel edit of the option count still rebuilds the pins and keeps the links.
	FProperty* OptionCount = FindFProperty<FProperty>(UHTToggleGraphNode_Switch::StaticClass(), GET_MEMBER_NAME_CHECKED(UHTToggleGraphNode_Switch, OptionCount));
	Switch->Modify();
	Switch->OptionCount = 3;
	FPropertyChangedEvent CountEvent(OptionCount);
	Switch->PostEditChangeProperty(CountEvent);
	TestEqual(TEXT("Option count edit adds a pin"), CountInputPins(Switch), 3);
	TestTrue(TEXT("Option count edit keeps the links"), Tree.IsIntact());
	CheckLinks(*this, TEXT("Option count edit"), Tree.Graph);

	// A rebuild never trips over a null link or a link to a node that left the graph.
	UHTToggleGraphNode_Part* Stray = Spawn<UHTToggleGraphNode_Part>(Tree.Graph, -400, 400);
	Link(Tree.Graph, Stray->GetOutputPin(), Switch->GetOptionPin(2));
	Tree.Graph->RemoveNode(Stray, /*bBreakAllLinks*/ false);
	Switch->GetOptionPin(2)->LinkedTo.Add(nullptr);
	Switch->ReconstructNode();
	TestEqual(TEXT("Rebuild keeps three options"), CountInputPins(Switch), 3);
	TestTrue(TEXT("Rebuild drops the null and the stray link"), Switch->GetOptionPin(2) && Switch->GetOptionPin(2)->LinkedTo.IsEmpty());
	TestTrue(TEXT("The stray node forgets the rebuilt pin too"), Stray->GetOutputPin()->LinkedTo.IsEmpty());
	TestTrue(TEXT("Rebuild keeps the live links"), Tree.IsIntact());
	CheckLinks(*this, TEXT("Rebuild with bad links"), Tree.Graph);
	return !HasAnyErrors();
}

#endif
