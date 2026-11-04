#include "HTToggleGraph.h"
#include "HTToggleGraphEditor.h"
#include "HTToggleGraphNodeWidgets.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GraphEditor.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "NodeFactory.h"
#include "RenderingThread.h"
#include "SGraphPanel.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/AppStyle.h"
#include "TextureResource.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Zoom range and collapsible nodes of the toggle graph editor.
namespace HTToggleGraphViewTests
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

	static int32 CountPins(const UEdGraphNode* Node, const EEdGraphPinDirection Direction)
	{
		int32 Count = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			Count += Pin && Pin->Direction == Direction ? 1 : 0;
		}
		return Count;
	}

	static TSharedPtr<SHTToggleGraphNode> MakeWidget(UEdGraphNode* Node)
	{
		return StaticCastSharedPtr<SHTToggleGraphNode>(FNodeFactory::CreateNodeWidget(Node));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphZoomAndCollapseTest,
	"HT.BlueprintToggleTool.ToggleGraphZoomAndCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphZoomAndCollapseTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphViewTests;
	if (!TestTrue(TEXT("Editor transactions are available"), GEditor && GEditor->Trans))
	{
		return false;
	}

	// The zoom steps: 1:1 stays the default, the range goes to 4x, Ctrl is never needed.
	{
		const FHTToggleGraphZoomLevels Zoom;
		TestTrue(TEXT("Zoom past 1:1 without Ctrl"), Zoom.GetZoomLimitHandling() == EGraphZoomLimitHandling::AllowLimitBreak);
		TestEqual(TEXT("Default zoom is 1:1"), Zoom.GetZoomAmount(Zoom.GetDefaultZoomLevel()), 1.0f);
		TestEqual(TEXT("Default zoom text"), Zoom.GetZoomText(Zoom.GetDefaultZoomLevel()).ToString(), FString(TEXT("1:1")));
		TestTrue(TEXT("Maximum zoom is at least 4x"), Zoom.GetZoomAmount(Zoom.GetNumZoomLevels() - 1) >= 4.0f);
		TestEqual(TEXT("Smallest zoom matches the engine"), Zoom.GetZoomAmount(0), 0.1f);
		TestTrue(TEXT("Deep zoom draws full detail"), Zoom.GetLOD(Zoom.GetNumZoomLevels() - 1) == EGraphRenderingLOD::FullyZoomedIn);
		TestEqual(TEXT("1.0 maps to the default step"), Zoom.GetNearestZoomLevel(1.0f), Zoom.GetDefaultZoomLevel());
		TestEqual(TEXT("Large amounts map to the last step"), Zoom.GetNearestZoomLevel(10.0f), Zoom.GetNumZoomLevels() - 1);
		for (int32 Level = 1; Level < Zoom.GetNumZoomLevels(); ++Level)
		{
			TestTrue(*FString::Printf(TEXT("Zoom step %d grows"), Level), Zoom.GetZoomAmount(Level) > Zoom.GetZoomAmount(Level - 1));
		}
	}

	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraphView_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	TStrongObjectPtr<UHTToggleGraph> AssetRef(NewObject<UHTToggleGraph>(Package, TEXT("TG_View"), RF_Public | RF_Transactional));
	UEdGraph* Graph = AssetRef->EnsureGraph();

	// The plain mouse wheel zooms past 1:1 and up to 4x on the editor's panel.
	{
		TSharedRef<SGraphEditor> Editor = SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(true);
		FHTToggleGraphEditor::ApplyZoomLevels(*Editor);
		SGraphPanel* Panel = Editor->GetGraphPanel();
		if (TestNotNull(TEXT("Graph panel"), Panel))
		{
			TestEqual(TEXT("Opens at 1:1"), Panel->GetZoomAmount(), 1.0f);
			const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(800.0f, 600.0f), FSlateLayoutTransform());
			auto Wheel = [&](const float Delta)
			{
				const FPointerEvent Event(0, FVector2D(400.0, 300.0), FVector2D(400.0, 300.0), TSet<FKey>(), EKeys::Invalid, Delta, FModifierKeysState());
				Panel->OnMouseWheel(Geometry, Event);
			};
			Wheel(1.0f);
			TestEqual(TEXT("One wheel step without Ctrl goes past 1:1"), Panel->GetZoomAmount(), 1.25f);
			for (int32 Step = 0; Step < 20; ++Step)
			{
				Wheel(1.0f);
			}
			TestEqual(TEXT("The wheel reaches 4x"), Panel->GetZoomAmount(), 4.0f);
			for (int32 Step = 0; Step < 40; ++Step)
			{
				Wheel(-1.0f);
			}
			TestEqual(TEXT("And back down to the smallest step"), Panel->GetZoomAmount(), 0.1f);
		}
	}

	// Collapsible nodes.
	UHTToggleGraphNode_Generate* Generate = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		Generate = Generate ? Generate : Cast<UHTToggleGraphNode_Generate>(Node);
	}
	UHTToggleGraphNode_Switch* Switch = Spawn<UHTToggleGraphNode_Switch>(Graph, 400, 0);
	UHTToggleGraphNode_Part* Part = Spawn<UHTToggleGraphNode_Part>(Graph, 0, 0);
	UHTToggleGraphNode_TextureSwitch* Texture = Spawn<UHTToggleGraphNode_TextureSwitch>(Graph, 400, 400);
	UHTToggleGraphNode_MaterialSwitch* Material = Spawn<UHTToggleGraphNode_MaterialSwitch>(Graph, 400, 800);
	UHTToggleGraphNode_Group* Group = Spawn<UHTToggleGraphNode_Group>(Graph, 0, 400);
	if (!TestTrue(TEXT("Nodes spawn"), Generate && Switch && Part && Texture && Material && Group))
	{
		return false;
	}
	Switch->KeyName = TEXT("alt 6");
	Switch->OptionLabels = { TEXT("Coat"), FString() };
	Part->MaterialIDs = { 10, 11 };
	Texture->KeyName = TEXT("alt 9");
	Texture->ParameterName = TEXT("BaseColor");
	Material->KeyName = TEXT("alt 8");
	Link(Graph, Switch->GetOutputPin(), Generate->GetInputPin());
	Link(Graph, Texture->GetOutputPin(), Switch->GetOptionPin(1));
	Link(Graph, Part->GetOutputPin(), Texture->GetDynamicInputPin(0));

	TestEqual(TEXT("Switch summary"), Switch->GetCollapsedSummary(), FString(TEXT("Key alt 6 · 2 options")));
	TestEqual(TEXT("Part summary"), Part->GetCollapsedSummary(), FString(TEXT("IDs 10, 11")));
	TestEqual(TEXT("Texture Switch summary"), Texture->GetCollapsedSummary(), FString(TEXT("Key alt 9 · 2 options · BaseColor")));
	TestEqual(TEXT("Material Switch summary"), Material->GetCollapsedSummary(), FString(TEXT("Key alt 8 · 2 options")));
	TestFalse(TEXT("Groups have nothing to collapse"), Group->SetCollapsedInline(true));
	TestFalse(TEXT("The generate node has nothing to collapse"), Generate->SetCollapsedInline(true));
	TestNotNull(TEXT("Collapse arrow brush exists"), FAppStyle::Get().GetOptionalBrush(TEXT("TreeArrow_Collapsed"), nullptr, nullptr));
	TestNotNull(TEXT("Expand arrow brush exists"), FAppStyle::Get().GetOptionalBrush(TEXT("TreeArrow_Expanded"), nullptr, nullptr));

	// Saved per node, undoable.
	TestTrue(TEXT("Collapse the part"), Part->SetCollapsedInline(true));
	TestTrue(TEXT("The part is collapsed"), Part->IsCollapsed());
	TestTrue(TEXT("Undo the collapse"), GEditor->UndoTransaction());
	TestFalse(TEXT("Undo expands it"), Part->IsCollapsed());
	TestTrue(TEXT("Redo the collapse"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo collapses it"), Part->IsCollapsed());

	// The widget keeps every pin and hides the fields.
	TestTrue(TEXT("Collapse the switch"), Switch->SetCollapsedInline(true));
	for (UEdGraphNode* Node : TArray<UEdGraphNode*>{ Switch, Part, Texture })
	{
		const TSharedPtr<SHTToggleGraphNode> Widget = MakeWidget(Node);
		const UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node);
		if (TestTrue(*FString::Printf(TEXT("%s widget"), *Node->GetName()), Widget.IsValid()))
		{
			TestEqual(*FString::Printf(TEXT("%s shows its fields only when expanded"), *Node->GetName()), Widget->IsShowingFields(), !ToggleNode->IsCollapsed());
			TestEqual(*FString::Printf(TEXT("%s keeps every input pin"), *Node->GetName()), Widget->GetInputPinWidgetCount(), CountPins(Node, EGPD_Input));
			TestEqual(*FString::Printf(TEXT("%s keeps its output pin"), *Node->GetName()), Widget->GetOutputPinWidgetCount(), CountPins(Node, EGPD_Output));
		}
	}
	// Collapsed nodes still connect, and links survive collapsing.
	TestTrue(TEXT("A collapsed part connects to a collapsed switch"), Link(Graph, Part->GetOutputPin(), Switch->GetOptionPin(0)));
	TestTrue(TEXT("The part keeps its texture target link"), Part->GetOutputPin()->LinkedTo.Contains(Texture->GetDynamicInputPin(0)));
	TestTrue(TEXT("Expand the switch"), Switch->SetCollapsedInline(false));
	TestTrue(TEXT("Links are unchanged after expanding"), Switch->GetOptionPin(0)->LinkedTo.Contains(Part->GetOutputPin()) && Switch->GetOptionPin(1)->LinkedTo.Contains(Texture->GetOutputPin()));
	TestTrue(TEXT("Collapse the switch again"), Switch->SetCollapsedInline(true));

	// Collapse all / Expand all: one undoable step each.
	TestEqual(TEXT("Collapse all changes the two expanded switches"), UHTToggleGraphNode::SetAllCollapsedInline(Graph, true), 2);
	TestTrue(TEXT("Everything collapsible is collapsed"), Switch->IsCollapsed() && Part->IsCollapsed() && Texture->IsCollapsed() && Material->IsCollapsed());
	TestFalse(TEXT("Groups stay as they are"), Group->IsCollapsed());
	TestTrue(TEXT("Undo collapse all"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores each node"), Switch->IsCollapsed() && Part->IsCollapsed() && !Texture->IsCollapsed() && !Material->IsCollapsed());
	{
		FGraphContextMenuBuilder MenuBuilder(Graph);
		Graph->GetSchema()->GetGraphContextActions(MenuBuilder);
		TSharedPtr<FEdGraphSchemaAction> ExpandAll;
		TSharedPtr<FEdGraphSchemaAction> CollapseAll;
		for (int32 Index = 0; Index < MenuBuilder.GetNumActions(); ++Index)
		{
			const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
			ExpandAll = Action.IsValid() && Action->GetMenuDescription().ToString() == TEXT("Expand all") ? Action : ExpandAll;
			CollapseAll = Action.IsValid() && Action->GetMenuDescription().ToString() == TEXT("Collapse all") ? Action : CollapseAll;
		}
		if (TestTrue(TEXT("Graph menu has Collapse all and Expand all"), ExpandAll.IsValid() && CollapseAll.IsValid()))
		{
			TestNull(TEXT("Expand all places no node"), ExpandAll->PerformAction(Graph, nullptr, FVector2f::ZeroVector));
			TestTrue(TEXT("Expand all from the graph menu"), !Switch->IsCollapsed() && !Part->IsCollapsed() && !Texture->IsCollapsed() && !Material->IsCollapsed());
			CollapseAll->PerformAction(Graph, nullptr, FVector2f::ZeroVector);
			TestTrue(TEXT("Collapse all from the graph menu"), Switch->IsCollapsed() && Material->IsCollapsed());
		}
		FGraphContextMenuBuilder PinMenu(Graph);
		PinMenu.FromPin = Part->GetOutputPin();
		Graph->GetSchema()->GetGraphContextActions(PinMenu);
		TestEqual(TEXT("Dragging from a pin only offers nodes"), PinMenu.GetNumActions(), 6);
	}
	// Node right-click menu.
	{
		UToolMenu* Menu = NewObject<UToolMenu>();
		UGraphNodeContextMenuContext* Context = NewObject<UGraphNodeContextMenuContext>();
		Context->Init(Graph, Part, nullptr, false);
		Part->GetNodeContextMenuActions(Menu, Context);
		const FToolMenuSection* Section = Menu->FindSection(TEXT("HTToggleGraphNodeView"));
		TestTrue(TEXT("Part menu: Expand, Collapse all, Expand all"), Section && Section->Blocks.Num() == 3);
		UToolMenu* GroupMenu = NewObject<UToolMenu>();
		Group->GetNodeContextMenuActions(GroupMenu, Context);
		const FToolMenuSection* GroupSection = GroupMenu->FindSection(TEXT("HTToggleGraphNodeView"));
		TestTrue(TEXT("Group menu: Collapse all, Expand all"), GroupSection && GroupSection->Blocks.Num() == 2);
	}

	// Copy and paste keeps the state.
	{
		FGraphPanelSelectionSet Selection;
		Selection.Add(Switch);
		Selection.Add(Part);
		Selection.Add(Material);
		Material->SetCollapsedInline(false);
		const TArray<UEdGraphNode*> Pasted = FHTToggleGraphEditor::PasteNodesFromText(Graph, FHTToggleGraphEditor::ExportNodesToText(Selection), FVector2f(0.0f, 1200.0f));
		const UHTToggleGraphNode_Switch* PastedSwitch = nullptr;
		const UHTToggleGraphNode_Part* PastedPart = nullptr;
		const UHTToggleGraphNode_MaterialSwitch* PastedMaterial = nullptr;
		for (UEdGraphNode* Node : Pasted)
		{
			PastedSwitch = PastedSwitch ? PastedSwitch : Cast<UHTToggleGraphNode_Switch>(Node);
			PastedPart = PastedPart ? PastedPart : Cast<UHTToggleGraphNode_Part>(Node);
			PastedMaterial = PastedMaterial ? PastedMaterial : Cast<UHTToggleGraphNode_MaterialSwitch>(Node);
		}
		if (TestTrue(TEXT("Three nodes pasted"), PastedSwitch && PastedPart && PastedMaterial))
		{
			TestTrue(TEXT("Pasted switch stays collapsed"), PastedSwitch->IsCollapsed());
			TestTrue(TEXT("Pasted part stays collapsed"), PastedPart->IsCollapsed());
			TestFalse(TEXT("Pasted material switch stays expanded"), PastedMaterial->IsCollapsed());
			TestTrue(TEXT("Pasted link between collapsed nodes"), PastedSwitch->GetOptionPin(0)->LinkedTo.Contains(PastedPart->GetOutputPin()));
		}
		TestTrue(TEXT("Undo the paste"), GEditor->UndoTransaction());
	}

	// Render collapsed and expanded nodes side by side for review.
	if (FApp::CanEverRender())
	{
		Texture->SetCollapsedInline(false);
		TSharedRef<SGraphEditor> Editor = SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(true);
		FHTToggleGraphEditor::ApplyZoomLevels(*Editor);
		if (SGraphPanel* Panel = Editor->GetGraphPanel())
		{
			Panel->Update();
		}
		const FVector2D DrawSize(1400.0, 1100.0);
		FWidgetRenderer* Renderer = new FWidgetRenderer(true, true);
		UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, true);
		if (Target)
		{
			Target->AddToRoot();
			for (int32 Pass = 0; Pass < 6; ++Pass)
			{
				Editor->SetViewLocation(FVector2f(-60.0f, -60.0f), 1.0f);
				Renderer->DrawWidget(Target, Editor, DrawSize, 0.05f, false);
				FlushRenderingCommands();
			}
			TArray<FColor> Pixels;
			FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
			if (Resource && Resource->ReadPixels(Pixels) && Pixels.Num() == static_cast<int32>(DrawSize.X * DrawSize.Y))
			{
				for (FColor& Pixel : Pixels)
				{
					Pixel.A = 255;
				}
				const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HTToggleGraphCollapsed.png"));
				FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), static_cast<int32>(DrawSize.X), static_cast<int32>(DrawSize.Y)));
				AddInfo(FString::Printf(TEXT("Collapsed node screenshot: %s"), *FPaths::ConvertRelativePathToFull(Path)));
			}
			Target->RemoveFromRoot();
		}
		BeginCleanup(Renderer);
	}
	return !HasAnyErrors();
}

#endif
