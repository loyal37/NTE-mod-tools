#include "HTToggleGraph.h"
#include "HTToggleGraphCompiler.h"
#include "HTToggleGraphEditor.h"
#include "HTToggleGraphNodeWidgets.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/AnimBlueprintFactory.h"
#include "GameFramework/SaveGame.h"
#include "GraphEditor.h"
#include "ImageUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MaterialTypes.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "NodeFactory.h"
#include "RenderingThread.h"
#include "SGraphPanel.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// Texture Switch and Material Switch nodes, on the lacrimosa fixture of the test project.
namespace HTToggleGraphEffectTests
{
	struct FFixture
	{
		USkeletalMesh* Mesh = nullptr;
		// Two materials of the mesh that share a texture parameter, and a third one.
		UMaterialInterface* M1 = nullptr;
		UMaterialInterface* M2 = nullptr;
		UMaterialInterface* M3 = nullptr;
		TArray<int32> Slots1;
		TArray<int32> Slots2;
		FName Parameter;
		UTexture* Textures[3] = {};
	};

	static TArray<FName> TextureParameters(UMaterialInterface* Material)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material->GetAllTextureParameterInfo(Infos, Ids);
		TArray<FName> Names;
		for (const FMaterialParameterInfo& Info : Infos)
		{
			Names.AddUnique(Info.Name);
		}
		Names.Sort(FNameLexicalLess());
		return Names;
	}

	static UTexture* ReadTexture(UMaterialInterface* Material, const FName Parameter)
	{
		UTexture* Value = nullptr;
		if (Material)
		{
			Material->GetTextureParameterValue(FHashedMaterialParameterInfo(FMaterialParameterInfo(Parameter)), Value);
		}
		return Value;
	}

	static bool LoadFixture(FAutomationTestBase& Test, FFixture& Out)
	{
		const FString Root(TEXT("/Game/Characters/Player/004_lacrimosa/"));
		Out.Mesh = LoadObject<USkeletalMesh>(nullptr, *(Root + TEXT("player_004_lacrimosa_skin.player_004_lacrimosa_skin")));
		if (!Test.TestNotNull(TEXT("Lacrimosa mesh fixture"), Out.Mesh) || !Test.TestNotNull(TEXT("Fixture skeleton"), Out.Mesh->GetSkeleton()))
		{
			return false;
		}
		TArray<UMaterialInterface*> Distinct;
		for (const FSkeletalMaterial& Material : Out.Mesh->GetMaterials())
		{
			if (Material.MaterialInterface)
			{
				Distinct.AddUnique(Material.MaterialInterface);
			}
		}
		for (int32 First = 0; First < Distinct.Num() && !Out.M1; ++First)
		{
			const TArray<FName> FirstNames = TextureParameters(Distinct[First]);
			for (int32 Second = First + 1; Second < Distinct.Num(); ++Second)
			{
				for (const FName Name : TextureParameters(Distinct[Second]))
				{
					if (FirstNames.Contains(Name))
					{
						Out.M1 = Distinct[First];
						Out.M2 = Distinct[Second];
						Out.Parameter = Name;
						break;
					}
				}
				if (Out.M1)
				{
					break;
				}
			}
		}
		for (UMaterialInterface* Material : Distinct)
		{
			if (Material != Out.M1 && Material != Out.M2)
			{
				Out.M3 = Material;
				break;
			}
		}
		if (!Test.TestTrue(TEXT("Fixture has two materials sharing a texture parameter and a third material"), Out.M1 && Out.M2 && Out.M3))
		{
			return false;
		}
		Out.Slots1 = UHTToggleGraphNode_Part::FindSlotsUsingMaterial(Out.Mesh, Out.M1);
		Out.Slots2 = UHTToggleGraphNode_Part::FindSlotsUsingMaterial(Out.Mesh, Out.M2);
		const TCHAR* const TextureNames[] = { TEXT("T_player_004_lacrimosa_01_d"), TEXT("T_player_004_lacrimosa_01_d2"), TEXT("T_player_004_lacrimosa_01_d3") };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Out.Textures[Index] = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("%ster_new/cloth_ter/%s.%s"), *Root, TextureNames[Index], TextureNames[Index]));
			if (!Test.TestNotNull(*FString::Printf(TEXT("Fixture texture %s"), TextureNames[Index]), Out.Textures[Index]))
			{
				return false;
			}
		}
		return true;
	}

	struct FAssets
	{
		TStrongObjectPtr<UHTToggleGraph> Asset;
		TStrongObjectPtr<UAnimBlueprint> Anim;
		TStrongObjectPtr<UBlueprint> Save;
		UEdGraph* Graph = nullptr;
		UHTToggleGraphNode_Generate* Generate = nullptr;
	};

	static bool MakeAssets(FAutomationTestBase& Test, UPackage* Package, const FString& Name, const FFixture& Fixture, FAssets& Out)
	{
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UAnimInstance::StaticClass();
		Factory->TargetSkeleton = Fixture.Mesh->GetSkeleton();
		Out.Anim.Reset(Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, *(TEXT("ABP_") + Name), RF_Public | RF_Transient, nullptr, GWarn)));
		Out.Save.Reset(FKismetEditorUtilities::CreateBlueprint(USaveGame::StaticClass(), Package, *(TEXT("SG_") + Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
		if (!Test.TestTrue(TEXT("Test Blueprints"), Out.Anim.IsValid() && Out.Save.IsValid()))
		{
			return false;
		}
		Out.Anim->SetPreviewMesh(Fixture.Mesh, false);
		Out.Asset.Reset(NewObject<UHTToggleGraph>(Package, *(TEXT("TG_") + Name), RF_Public | RF_Transactional));
		Out.Asset->AnimBlueprint = Out.Anim.Get();
		Out.Asset->SaveGameBlueprint = Out.Save.Get();
		Out.Asset->TreeName = Name;
		Out.Graph = Out.Asset->EnsureGraph();
		for (UEdGraphNode* Node : Out.Graph->Nodes)
		{
			Out.Generate = Out.Generate ? Out.Generate : Cast<UHTToggleGraphNode_Generate>(Node);
		}
		return Test.TestTrue(TEXT("Graph has a preview mesh and a generate node"), Out.Generate && Out.Asset->ResolvePreviewMesh() == Fixture.Mesh);
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

	static void CheckLinks(FAutomationTestBase& Test, const FString& Context, const UEdGraph* Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				for (const UEdGraphPin* Linked : Pin ? Pin->LinkedTo : TArray<UEdGraphPin*>())
				{
					const UEdGraphNode* Owner = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
					if (!Linked || Linked->WasTrashed() || !Owner || !Graph->Nodes.Contains(Owner) || !Linked->LinkedTo.Contains(Pin))
					{
						Test.AddError(FString::Printf(TEXT("%s: %s.%s has a broken link"), *Context, *Node->GetName(), *Pin->PinName.ToString()));
					}
				}
			}
		}
	}

	static const FHTNestedToggleSwitch* FindModelSwitch(const FHTNestedToggleGraph& Model, const UHTToggleGraphNode* Node)
	{
		return Model.Switches.FindByPredicate([Node](const FHTNestedToggleSwitch& Switch) { return Switch.NodeId == Node->GetNodeId(); });
	}

	static FString PathOf(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphEffectNodesTest,
	"HT.BlueprintToggleTool.ToggleGraphEffectNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphEffectNodesTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphEffectTests;
	FFixture Fixture;
	if (!TestTrue(TEXT("Editor transactions are available"), GEditor && GEditor->Trans) || !LoadFixture(*this, Fixture))
	{
		return false;
	}
	// Undo needs transactional, non-transient objects; /Temp packages are never saved.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraphEffects_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	FAssets Assets;
	if (!MakeAssets(*this, Package, TEXT("FxNodes"), Fixture, Assets))
	{
		return false;
	}
	UEdGraph* Graph = Assets.Graph;
	UHTToggleGraphNode_Switch* Show = Spawn<UHTToggleGraphNode_Switch>(Graph, 800, 0);
	UHTToggleGraphNode_TextureSwitch* Texture = Spawn<UHTToggleGraphNode_TextureSwitch>(Graph, 400, -400);
	UHTToggleGraphNode_MaterialSwitch* Material = Spawn<UHTToggleGraphNode_MaterialSwitch>(Graph, 400, 500);
	UHTToggleGraphNode_Part* PartA = Spawn<UHTToggleGraphNode_Part>(Graph, 0, 0);
	UHTToggleGraphNode_Part* PartB = Spawn<UHTToggleGraphNode_Part>(Graph, 0, 300);
	UHTToggleGraphNode_Group* Group = Spawn<UHTToggleGraphNode_Group>(Graph, 400, 900);
	if (!TestTrue(TEXT("Nodes spawn"), Show && Texture && Material && PartA && PartB && Group))
	{
		return false;
	}
	Show->KeyName = TEXT("alt 7");
	Texture->KeyName = TEXT("alt 9");
	Material->KeyName = TEXT("alt 8");
	TestEqual(TEXT("Texture Switch title"), Texture->GetDisplayLabel(), FString(TEXT("Texture Switch [alt 9]")));
	TestEqual(TEXT("Material Switch title"), Material->GetDisplayLabel(), FString(TEXT("Material Switch [alt 8]")));
	TestEqual(TEXT("Texture Switch input reads Target 1"), Texture->GetDynamicInputPin(0)->PinFriendlyName.ToString(), FString(TEXT("Target 1")));
	TestEqual(TEXT("Material Switch input reads Slots 1"), Material->GetDynamicInputPin(0)->PinFriendlyName.ToString(), FString(TEXT("Slots 1")));
	TestEqual(TEXT("Texture Switch starts with one input"), CountInputPins(Texture), 1);

	// Fan-out: one visibility link plus any number of Target / Slots links.
	TestTrue(TEXT("Part -> switch option"), Link(Graph, PartA->GetOutputPin(), Show->GetOptionPin(0)));
	TestTrue(TEXT("Part -> Target 1"), Link(Graph, PartA->GetOutputPin(), Texture->GetDynamicInputPin(0)));
	TestTrue(TEXT("Part -> Slots 1"), Link(Graph, PartA->GetOutputPin(), Material->GetDynamicInputPin(0)));
	TestEqual(TEXT("The part keeps all three links"), PartA->GetOutputPin()->LinkedTo.Num(), 3);
	TestEqual(TEXT("Target inputs grow like a group"), CountInputPins(Texture), 2);
	TestEqual(TEXT("Slots inputs grow like a group"), CountInputPins(Material), 2);
	TestEqual(TEXT("The new input reads Target 2"), Texture->GetDynamicInputPin(1)->PinFriendlyName.ToString(), FString(TEXT("Target 2")));
	TestTrue(TEXT("Part -> another option"), Link(Graph, PartA->GetOutputPin(), Show->GetOptionPin(1)));
	TestTrue(TEXT("The visibility link moved"), IsLinked(PartA->GetOutputPin(), Show->GetOptionPin(1)) && Show->GetOptionPin(0)->LinkedTo.IsEmpty());
	TestTrue(TEXT("Target and Slots links stay when the visibility link moves"), IsLinked(PartA->GetOutputPin(), Texture->GetDynamicInputPin(0)) && IsLinked(PartA->GetOutputPin(), Material->GetDynamicInputPin(0)));
	TestFalse(TEXT("A switch output cannot be a target"), Link(Graph, Show->GetOutputPin(), Texture->GetDynamicInputPin(1)));
	TestFalse(TEXT("A group output cannot feed slots"), Link(Graph, Group->GetOutputPin(), Material->GetDynamicInputPin(1)));
	TestTrue(TEXT("Another part replaces the part on a target"), Link(Graph, PartB->GetOutputPin(), Texture->GetDynamicInputPin(0)));
	TestTrue(TEXT("Target 1 now holds part B only"), Texture->GetTargetPart(0) == PartB && Texture->GetDynamicInputPin(0)->LinkedTo.Num() == 1);
	TestFalse(TEXT("Part A lost only that link"), IsLinked(PartA->GetOutputPin(), Texture->GetDynamicInputPin(0)));
	TestTrue(TEXT("Part A -> Target 2"), Link(Graph, PartA->GetOutputPin(), Texture->GetDynamicInputPin(1)));
	TestTrue(TEXT("A Texture Switch hangs on a switch option"), Link(Graph, Texture->GetOutputPin(), Show->GetOptionPin(0)));
	TestTrue(TEXT("Switch -> generate"), Link(Graph, Show->GetOutputPin(), Assets.Generate->GetInputPin()));
	TestTrue(TEXT("Material Switch -> generate"), Link(Graph, Material->GetOutputPin(), Assets.Generate->GetDynamicInputPin(1)));
	CheckLinks(*this, TEXT("After wiring"), Graph);

	// By material: picks every slot of the material; picking by hand leaves the mode; undoable.
	TestTrue(TEXT("Pick part A by material"), PartA->SetSourceMaterialInline(Fixture.M1));
	TestTrue(TEXT("By material ticks every slot of the material"), PartA->MaterialIDs == Fixture.Slots1 && !Fixture.Slots1.IsEmpty());
	TestTrue(TEXT("By material remembers the material"), PartA->SourceMaterial.Get() == Fixture.M1);
	int32 Extra = 0;
	while (Fixture.Slots1.Contains(Extra))
	{
		++Extra;
	}
	TestTrue(TEXT("Tick an extra slot by hand"), PartA->SetMaterialIDSelectedInline(Extra, true));
	TestTrue(TEXT("Manual picks leave By material mode"), PartA->SourceMaterial.IsNull() && PartA->MaterialIDs.Contains(Extra));
	TestTrue(TEXT("Undo the manual pick"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores By material mode"), PartA->SourceMaterial.Get() == Fixture.M1 && PartA->MaterialIDs == Fixture.Slots1);
	TestTrue(TEXT("Pick part B by material"), PartB->SetSourceMaterialInline(Fixture.M2));
	PartB->MaterialIDs = { 999 };
	TestTrue(TEXT("By material looks the slots up again"), PartB->GetEffectiveMaterialIDs() == Fixture.Slots2);

	// Inline edits are undoable and option count changes keep per-option data.
	TestTrue(TEXT("Set the parameter"), Texture->SetParameterNameInline(Fixture.Parameter.ToString()));
	TestTrue(TEXT("Candidate parameters include it"), Texture->GetCandidateParameters().Contains(Fixture.Parameter));
	TestTrue(TEXT("Set a target texture"), Texture->SetTargetTextureInline(1, 0, Fixture.Textures[0]));
	TestTrue(TEXT("Undo the texture"), GEditor->UndoTransaction());
	TestTrue(TEXT("Texture undone"), Texture->GetTargetTexture(1, 0).IsNull());
	TestTrue(TEXT("Redo the texture"), GEditor->RedoTransaction());
	TestTrue(TEXT("Texture redone"), Texture->GetTargetTexture(1, 0).Get() == Fixture.Textures[0]);
	TestTrue(TEXT("Three options"), Texture->SetOptionCountInline(3));
	TestTrue(TEXT("Texture on option 3"), Texture->SetTargetTextureInline(1, 2, Fixture.Textures[2]));
	TestTrue(TEXT("Back to two options"), Texture->SetOptionCountInline(2));
	TestTrue(TEXT("Option 3 texture is dropped"), Texture->GetTargetTexture(1, 2).IsNull() && Texture->Targets[1].OptionTextures.Num() <= 2);
	TestTrue(TEXT("Undo the option count"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores option 3 and its texture"), Texture->OptionCount == 3 && Texture->GetTargetTexture(1, 2).Get() == Fixture.Textures[2]);
	TestEqual(TEXT("Option changes keep the target inputs"), CountInputPins(Texture), 3);
	TestTrue(TEXT("Set a material"), Material->SetOptionMaterialInline(1, Fixture.M3));
	TestTrue(TEXT("Undo the material"), GEditor->UndoTransaction());
	TestTrue(TEXT("Material undone"), Material->GetOptionMaterial(1).IsNull());
	TestTrue(TEXT("Redo the material"), GEditor->RedoTransaction());
	TestTrue(TEXT("Material redone"), Material->GetOptionMaterial(1).Get() == Fixture.M3);
	CheckLinks(*this, TEXT("After undo and redo"), Graph);

	// The model: kinds, targets with resolved slots and materials, union of slots, nesting.
	{
		FHTNestedToggleGraph Model;
		TArray<FHTNestedToggleIssue> Errors;
		TArray<FString> Warnings;
		if (TestTrue(TEXT("The effect graph converts"), FHTToggleGraphCompiler::CompileToModel(Assets.Asset.Get(), Model, Errors, Warnings)))
		{
			const FHTNestedToggleSwitch* TextureModel = FindModelSwitch(Model, Texture);
			const FHTNestedToggleSwitch* MaterialModel = FindModelSwitch(Model, Material);
			if (TestTrue(TEXT("Both effect switches are in the model"), TextureModel && MaterialModel))
			{
				TestTrue(TEXT("Texture kind"), TextureModel->Kind == EHTNestedToggleSwitchKind::Texture);
				TestEqual(TEXT("Texture switch hangs on option 1 of the switch"), TextureModel->ParentNodeId, Show->GetNodeId());
				TestEqual(TEXT("Parent option"), TextureModel->ParentOption, 0);
				TestEqual(TEXT("Parameter"), TextureModel->TextureParameterName, Fixture.Parameter.ToString());
				if (TestEqual(TEXT("Two targets"), TextureModel->TextureTargets.Num(), 2))
				{
					TestEqual(TEXT("Target 1 is part B"), TextureModel->TextureTargets[0].PartNodeId, PartB->GetNodeId());
					TestTrue(TEXT("Target 1 slots are looked up again"), TextureModel->TextureTargets[0].MaterialIDs == Fixture.Slots2);
					TestEqual(TEXT("Target 1 belongs to M2"), TextureModel->TextureTargets[0].ParentMaterialPath, PathOf(Fixture.M2));
					TestEqual(TEXT("Target 2 belongs to M1"), TextureModel->TextureTargets[1].ParentMaterialPath, PathOf(Fixture.M1));
					TestTrue(TEXT("Target 2 textures"), TextureModel->TextureTargets[1].TexturePaths.Num() == 3 && TextureModel->TextureTargets[1].TexturePaths[0] == PathOf(Fixture.Textures[0]) && TextureModel->TextureTargets[1].TexturePaths[1].IsEmpty());
				}
				TestTrue(TEXT("Material kind"), MaterialModel->Kind == EHTNestedToggleSwitchKind::Material);
				TestTrue(TEXT("Material switch slots"), MaterialModel->MaterialIDs == Fixture.Slots1);
				TestTrue(TEXT("Material switch options"), MaterialModel->OptionMaterialPaths.Num() == 2 && MaterialModel->OptionMaterialPaths[0].IsEmpty() && MaterialModel->OptionMaterialPaths[1] == PathOf(Fixture.M3));
			}
			TestEqual(TEXT("Only part A shows through a path"), Model.Parts.Num(), 1);
			TestEqual(TEXT("Every mesh slot has its original material"), Model.SlotMaterialPaths.Num(), Fixture.Mesh->GetMaterials().Num());
		}

		// Two visibility paths are refused (links made around the schema, e.g. by older tools).
		PartA->GetOutputPin()->MakeLinkTo(Group->GetGroupInputPin(0));
		Errors.Reset();
		TestFalse(TEXT("Two visibility paths fail"), FHTToggleGraphCompiler::CompileToModel(Assets.Asset.Get(), Model, Errors, Warnings));
		TestTrue(TEXT("The error names the part"), Errors.ContainsByPredicate([PartA](const FHTNestedToggleIssue& Issue) { return Issue.NodeIds.Contains(PartA->GetNodeId()); }));
		PartA->GetOutputPin()->BreakLinkTo(Group->GetGroupInputPin(0));
	}

	// Copy and paste: inner Target links and per-target textures come along, outer links do not.
	{
		FGraphPanelSelectionSet Selection;
		Selection.Add(PartA);
		Selection.Add(PartB);
		Selection.Add(Texture);
		const int32 Before = Graph->Nodes.Num();
		const TArray<UEdGraphNode*> Pasted = FHTToggleGraphEditor::PasteNodesFromText(Graph, FHTToggleGraphEditor::ExportNodesToText(Selection), FVector2f(0.0f, 1400.0f));
		UHTToggleGraphNode_TextureSwitch* PastedTexture = nullptr;
		for (UEdGraphNode* Node : Pasted)
		{
			PastedTexture = PastedTexture ? PastedTexture : Cast<UHTToggleGraphNode_TextureSwitch>(Node);
		}
		if (TestTrue(TEXT("Three nodes pasted"), Pasted.Num() == 3 && PastedTexture))
		{
			TestEqual(TEXT("Pasted parameter"), PastedTexture->ParameterName, Fixture.Parameter.ToString());
			TestTrue(TEXT("Pasted textures"), PastedTexture->GetTargetTexture(1, 0).Get() == Fixture.Textures[0] && PastedTexture->GetTargetTexture(1, 2).Get() == Fixture.Textures[2]);
			const UHTToggleGraphNode_Part* PastedB = PastedTexture->GetTargetPart(0);
			const UHTToggleGraphNode_Part* PastedA = PastedTexture->GetTargetPart(1);
			TestTrue(TEXT("Pasted targets are the pasted parts"), PastedA && PastedB && PastedA != PartA && PastedB != PartB && Pasted.Contains(PastedA) && Pasted.Contains(PastedB));
			TestTrue(TEXT("Pasted texture switch output is free"), PastedTexture->GetOutputPin()->LinkedTo.IsEmpty());
			TestEqual(TEXT("Pasted part A keeps only its Target link"), PastedA ? PastedA->GetOutputPin()->LinkedTo.Num() : -1, 1);
			TestTrue(TEXT("Pasted part A keeps By material"), PastedA && PastedA->SourceMaterial.Get() == Fixture.M1);
			TestEqual(TEXT("Pasted texture switch keeps its inputs"), CountInputPins(PastedTexture), 3);
		}
		CheckLinks(*this, TEXT("After paste"), Graph);
		TestTrue(TEXT("Undo the paste"), GEditor->UndoTransaction());
		TestEqual(TEXT("Undo removes the pasted nodes"), Graph->Nodes.Num(), Before);
		TestTrue(TEXT("Redo the paste"), GEditor->RedoTransaction());
		TestEqual(TEXT("Redo brings them back"), Graph->Nodes.Num(), Before + 3);
		CheckLinks(*this, TEXT("After redo"), Graph);
	}

	// Saved assets keep every setting (the Switch properties moved to a shared base class).
	{
		Show->OptionLabels = { TEXT("On"), TEXT("Off") };
		TStrongObjectPtr<UHTToggleGraph> Copy(DuplicateObject<UHTToggleGraph>(Assets.Asset.Get(), Package, TEXT("TG_FxNodes_Copy")));
		const UHTToggleGraphNode_Switch* CopyShow = nullptr;
		const UHTToggleGraphNode_TextureSwitch* CopyTexture = nullptr;
		const UHTToggleGraphNode_MaterialSwitch* CopyMaterial = nullptr;
		for (UEdGraphNode* Node : Copy->Graph->Nodes)
		{
			CopyShow = CopyShow ? CopyShow : Cast<UHTToggleGraphNode_Switch>(Node);
			CopyTexture = CopyTexture ? CopyTexture : Cast<UHTToggleGraphNode_TextureSwitch>(Node);
			CopyMaterial = CopyMaterial ? CopyMaterial : Cast<UHTToggleGraphNode_MaterialSwitch>(Node);
		}
		if (TestTrue(TEXT("Duplicated graph has every switch kind"), CopyShow && CopyTexture && CopyMaterial))
		{
			TestEqual(TEXT("Switch key survives"), CopyShow->KeyName, FString(TEXT("alt 7")));
			TestTrue(TEXT("Switch labels survive"), CopyShow->OptionLabels == Show->OptionLabels);
			TestEqual(TEXT("Texture switch options survive"), CopyTexture->OptionCount, 3);
			TestTrue(TEXT("Texture switch textures survive"), CopyTexture->GetTargetTexture(1, 2).Get() == Fixture.Textures[2]);
			TestTrue(TEXT("Material switch materials survive"), CopyMaterial->GetOptionMaterial(1).Get() == Fixture.M3);
			TestEqual(TEXT("Duplicated target pin name"), CopyTexture->GetDynamicInputPin(1)->PinFriendlyName.ToString(), FString(TEXT("Target 2")));
		}
	}

	// Widgets of the new nodes.
	struct FExpectedWidget
	{
		UEdGraphNode* Node;
		const TCHAR* Type;
	};
	for (const FExpectedWidget& Entry : { FExpectedWidget{ Texture, TEXT("SHTToggleGraphNode_TextureSwitch") }, FExpectedWidget{ Material, TEXT("SHTToggleGraphNode_MaterialSwitch") }, FExpectedWidget{ PartA, TEXT("SHTToggleGraphNode_Part") } })
	{
		TSharedPtr<SGraphNode> Widget = FNodeFactory::CreateNodeWidget(Entry.Node);
		if (TestTrue(*FString::Printf(TEXT("%s uses %s"), *Entry.Node->GetName(), Entry.Type), Widget.IsValid() && Widget->GetTypeAsString() == Entry.Type))
		{
			const TSharedPtr<SHTToggleGraphNode> ToggleWidget = StaticCastSharedPtr<SHTToggleGraphNode>(Widget);
			TestEqual(*FString::Printf(TEXT("%s input pin widgets"), Entry.Type), ToggleWidget->GetInputPinWidgetCount(), CountInputPins(Entry.Node));
		}
	}

	// Render the graph offscreen so the node layout can be reviewed as an image.
	if (FApp::CanEverRender())
	{
		TSharedRef<SGraphEditor> Editor = SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(true);
		if (SGraphPanel* Panel = Editor->GetGraphPanel())
		{
			Panel->Update();
		}
		const FVector2D DrawSize(1900.0, 1500.0);
		FWidgetRenderer* Renderer = new FWidgetRenderer(true, true);
		UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, true);
		if (Target)
		{
			Target->AddToRoot();
			for (int32 Pass = 0; Pass < 6; ++Pass)
			{
				Editor->SetViewLocation(FVector2f(-120.0f, -480.0f), 0.9f);
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
				const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HTToggleGraphEffectNodes.png"));
				FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), static_cast<int32>(DrawSize.X), static_cast<int32>(DrawSize.Y)));
				AddInfo(FString::Printf(TEXT("Effect node screenshot: %s"), *FPaths::ConvertRelativePathToFull(Path)));
			}
			Target->RemoveFromRoot();
		}
		BeginCleanup(Renderer);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphEffectsRuntimeTest,
	"HT.BlueprintToggleTool.ToggleGraphEffectsRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphEffectsRuntimeTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphEffectTests;
	FFixture Fixture;
	if (!LoadFixture(*this, Fixture))
	{
		return false;
	}
	const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTToggleGraphEffectsRuntime_%s"), *RunID));
	Package->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	FAssets Assets;
	if (!MakeAssets(*this, Package, TEXT("FxRun"), Fixture, Assets))
	{
		return false;
	}
	const FString Character = TEXT("FxTest") + RunID;
	Assets.Asset->CharacterName = Character;
	UEdGraph* Graph = Assets.Graph;

	// The old Texture mode in graph form: two by-material targets, three options; plus a show/hide
	// switch and a material switch acting on the same slots as the first target.
	UHTToggleGraphNode_Part* PartA = Spawn<UHTToggleGraphNode_Part>(Graph, 0, 0);
	UHTToggleGraphNode_Part* PartB = Spawn<UHTToggleGraphNode_Part>(Graph, 0, 300);
	UHTToggleGraphNode_Switch* Show = Spawn<UHTToggleGraphNode_Switch>(Graph, 400, 0);
	UHTToggleGraphNode_TextureSwitch* Texture = Spawn<UHTToggleGraphNode_TextureSwitch>(Graph, 400, 400);
	UHTToggleGraphNode_MaterialSwitch* Material = Spawn<UHTToggleGraphNode_MaterialSwitch>(Graph, 400, 900);
	PartA->SetSourceMaterialInline(Fixture.M1);
	PartB->SetSourceMaterialInline(Fixture.M2);
	Show->KeyName = TEXT("alt 7");
	Texture->KeyName = TEXT("alt 9");
	Texture->SetOptionCountInline(3);
	Texture->SetParameterNameInline(Fixture.Parameter.ToString());
	Material->KeyName = TEXT("alt 8");
	Material->SetOptionMaterialInline(1, Fixture.M3);
	Link(Graph, PartA->GetOutputPin(), Show->GetOptionPin(0));
	Link(Graph, PartA->GetOutputPin(), Texture->GetDynamicInputPin(0));
	Link(Graph, PartB->GetOutputPin(), Texture->GetDynamicInputPin(1));
	Link(Graph, PartA->GetOutputPin(), Material->GetDynamicInputPin(0));
	Link(Graph, Show->GetOutputPin(), Assets.Generate->GetDynamicInputPin(0));
	Link(Graph, Texture->GetOutputPin(), Assets.Generate->GetDynamicInputPin(1));
	Link(Graph, Material->GetOutputPin(), Assets.Generate->GetDynamicInputPin(2));
	UTexture* const* T = Fixture.Textures;
	Texture->SetTargetTextureInline(0, 0, T[0]);
	Texture->SetTargetTextureInline(0, 1, T[1]);
	Texture->SetTargetTextureInline(0, 2, T[2]);
	Texture->SetTargetTextureInline(1, 0, T[2]);
	Texture->SetTargetTextureInline(1, 2, T[0]);

	const FHTNestedToggleGeneratorResult Result = FHTToggleGraphCompiler::Generate(Assets.Asset.Get(), false);
	if (!TestTrue(TEXT("The effect tree generates"), Result.bSuccess))
	{
		AddError(Result.ToDisplayString());
		return false;
	}
	TestTrue(TEXT("Generated Animation Blueprint compiles"), Assets.Anim->Status != BS_Error);
	const FName ShowVar(TEXT("FxRun_Alt_Seven"));
	const FName TextureVar(TEXT("FxRun_Alt_Nine"));
	const FName MaterialVar(TEXT("FxRun_Alt_Eight"));
	const FName ApplyEvent(*FHTBlueprintToggleGenerator::MakeNestedToggleApplyEventName(TEXT("FxRun")));

	// Panel metadata: effect switches never look like show/hide rows.
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Assets.Anim.Get());
	FString Metadata;
	UK2Node_CustomEvent* Apply = nullptr;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node); Comment && Comment->NodeComment.StartsWith(FHTBlueprintToggleGenerator::MakeNestedToggleMetadataPrefix(TEXT("FxRun"))))
		{
			Metadata = Comment->NodeComment;
		}
		if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node); Event && Event->CustomFunctionName == ApplyEvent)
		{
			Apply = Event;
		}
	}
	TestTrue(*(TEXT("Texture switch row: ") + Metadata), Metadata.Contains(TEXT(";Switch=FxRun_Alt_Nine,Alt Nine,3,,-1,FxRun_Alt_Nine,1|2|3,000")));
	TestTrue(TEXT("Material switch row"), Metadata.Contains(TEXT(";Switch=FxRun_Alt_Eight,Alt Eight,2,,-1,FxRun_Alt_Eight,1|2,00")));
	TestTrue(TEXT("Show/hide row keeps its empty option"), Metadata.Contains(TEXT(";Switch=FxRun_Alt_Seven,Alt Seven,2,,-1,FxRun_Alt_Seven,1|,01")));

	// Apply order: no material or texture is set after any visibility call.
	if (TestNotNull(TEXT("Apply event"), Apply))
	{
		int32 SetMaterials = 0;
		int32 Shows = 0;
		bool bLateMaterial = false;
		TArray<const UEdGraphNode*> Pending = { Apply };
		TSet<const UEdGraphNode*> Visited;
		while (!Pending.IsEmpty())
		{
			const UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
			if (Visited.Contains(Node))
			{
				continue;
			}
			Visited.Add(Node);
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			const FName Function = Call ? Call->FunctionReference.GetMemberName() : NAME_None;
			SetMaterials += Function == TEXT("SetMaterial") ? 1 : 0;
			Shows += Function == TEXT("ShowMaterialSection") ? 1 : 0;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					for (const UEdGraphPin* Linked : Pin->LinkedTo)
					{
						Pending.Add(Linked->GetOwningNode());
					}
				}
			}
		}
		// Everything reachable from a visibility call must be visibility too.
		for (const UEdGraphNode* Node : Visited)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call || Call->FunctionReference.GetMemberName() != TEXT("ShowMaterialSection"))
			{
				continue;
			}
			TArray<const UEdGraphNode*> Downstream = { Node };
			TSet<const UEdGraphNode*> Seen;
			while (!Downstream.IsEmpty())
			{
				const UEdGraphNode* Next = Downstream.Pop(EAllowShrinking::No);
				if (Seen.Contains(Next))
				{
					continue;
				}
				Seen.Add(Next);
				const UK2Node_CallFunction* NextCall = Cast<UK2Node_CallFunction>(Next);
				const FName NextFunction = NextCall ? NextCall->FunctionReference.GetMemberName() : NAME_None;
				bLateMaterial |= NextFunction == TEXT("SetMaterial") || NextFunction == TEXT("SetTextureParameterValue");
				for (const UEdGraphPin* Pin : Next->Pins)
				{
					if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						for (const UEdGraphPin* Linked : Pin->LinkedTo)
						{
							Downstream.Add(Linked->GetOwningNode());
						}
					}
				}
			}
		}
		const TArray<int32> Managed = [&Fixture]() { TArray<int32> Slots = Fixture.Slots1; Slots.Append(Fixture.Slots2); return Slots; }();
		TestEqual(TEXT("One SetMaterial per managed slot"), SetMaterials, Managed.Num());
		TestEqual(TEXT("One visibility call per shown slot"), Shows, Fixture.Slots1.Num());
		TestFalse(TEXT("Materials and textures are applied before visibility"), bLateMaterial);
	}

	// Run the generated logic on a live component.
	UClass* AnimClass = Assets.Anim->GeneratedClass;
	UClass* SaveClass = Assets.Save->GeneratedClass;
	auto SlotName = [&Character](const FName Variable) { return Variable.ToString() + TEXT(" ") + Character; };
	auto ClearSaves = [&]()
	{
		for (const FName Variable : { ShowVar, TextureVar, MaterialVar })
		{
			UGameplayStatics::DeleteGameInSlot(SlotName(Variable), 0);
		}
	};
	ON_SCOPE_EXIT
	{
		ClearSaves();
	};
	ClearSaves();
	struct FLive
	{
		TStrongObjectPtr<USkeletalMeshComponent> Component;
		TStrongObjectPtr<UAnimInstance> Instance;
	};
	auto StartInstance = [&]()
	{
		FLive Live;
		Live.Component.Reset(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
		Live.Component->SetSkeletalMeshAsset(Fixture.Mesh);
		Live.Instance.Reset(NewObject<UAnimInstance>(Live.Component.Get(), AnimClass));
		if (UFunction* Init = Live.Instance->FindFunction(TEXT("BlueprintInitializeAnimation")))
		{
			Live.Instance->ProcessEvent(Init, nullptr);
		}
		return Live;
	};
	auto SetState = [&](const FLive& Live, const FName Variable, const int32 Value)
	{
		if (const FIntProperty* Property = FindFProperty<FIntProperty>(AnimClass, Variable))
		{
			Property->SetPropertyValue_InContainer(Live.Instance.Get(), Value);
		}
		if (UFunction* Function = Live.Instance->FindFunction(ApplyEvent))
		{
			Live.Instance->ProcessEvent(Function, nullptr);
		}
	};
	auto GetState = [&](const FLive& Live, const FName Variable)
	{
		const FIntProperty* Property = FindFProperty<FIntProperty>(AnimClass, Variable);
		return Property ? Property->GetPropertyValue_InContainer(Live.Instance.Get()) : INDEX_NONE;
	};
	UTexture* const DefaultB = ReadTexture(Fixture.M2, Fixture.Parameter);
	AddInfo(FString::Printf(TEXT("Fixture: %s on slots %s, %s on slots %s, %s for the material switch, parameter %s (M2 default %s)."),
		*Fixture.M1->GetName(), *FString::JoinBy(Fixture.Slots1, TEXT(","), [](const int32 Slot) { return FString::FromInt(Slot); }),
		*Fixture.M2->GetName(), *FString::JoinBy(Fixture.Slots2, TEXT(","), [](const int32 Slot) { return FString::FromInt(Slot); }),
		*Fixture.M3->GetName(), *Fixture.Parameter.ToString(), *GetNameSafe(DefaultB)));
	TestTrue(TEXT("Both targets have slots"), !Fixture.Slots1.IsEmpty() && !Fixture.Slots2.IsEmpty());
	// Slot shows a MID of Parent with Expected in the parameter.
	auto ExpectTexture = [&](const FLive& Live, const TArray<int32>& Slots, UMaterialInterface* Parent, UTexture* Expected, const FString& Context)
	{
		for (const int32 Slot : Slots)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Live.Component->GetMaterial(Slot));
			if (!TestNotNull(*FString::Printf(TEXT("%s: slot %d shows a MID"), *Context, Slot), MID))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s: slot %d MID belongs to %s"), *Context, Slot, *Parent->GetName()), MID->Parent == Parent);
			UTexture* Actual = ReadTexture(MID, Fixture.Parameter);
			AddInfo(FString::Printf(TEXT("%s: slot %d shows %s (parent %s) with %s"), *Context, Slot, *MID->GetName(), *GetNameSafe(MID->Parent), *GetNameSafe(Actual)));
			TestTrue(*FString::Printf(TEXT("%s: slot %d texture %s (expected %s)"), *Context, Slot, *GetNameSafe(Actual), *GetNameSafe(Expected)), Actual == Expected);
		}
	};
	auto ExpectMaterial = [&](const FLive& Live, const TArray<int32>& Slots, UMaterialInterface* Expected, const FString& Context)
	{
		for (const int32 Slot : Slots)
		{
			TestTrue(*FString::Printf(TEXT("%s: slot %d shows %s (actual %s)"), *Context, Slot, *Expected->GetName(), *GetNameSafe(Live.Component->GetMaterial(Slot))), Live.Component->GetMaterial(Slot) == Expected);
		}
	};
	auto ExpectShown = [&](const FLive& Live, const TArray<int32>& Slots, const bool bShown, const FString& Context)
	{
		for (const int32 Slot : Slots)
		{
			TestEqual(*FString::Printf(TEXT("%s: slot %d visibility"), *Context, Slot), Live.Component->IsMaterialSectionShown(Slot, 0), bShown);
		}
	};

	// First start without saves: every switch at its initial option.
	{
		FLive Live = StartInstance();
		TestEqual(TEXT("Texture state starts at option 1"), GetState(Live, TextureVar), 0);
		ExpectTexture(Live, Fixture.Slots1, Fixture.M1, T[0], TEXT("Start"));
		ExpectTexture(Live, Fixture.Slots2, Fixture.M2, T[2], TEXT("Start"));
		ExpectShown(Live, Fixture.Slots1, true, TEXT("Start"));
		UMaterialInterface* FirstMID = Live.Component->GetMaterial(Fixture.Slots1[0]);

		UTexture* const ExpectedA[3] = { T[0], T[1], T[2] };
		UTexture* const ExpectedB[3] = { T[2], DefaultB, T[0] };
		for (int32 Option = 0; Option < 3; ++Option)
		{
			SetState(Live, TextureVar, Option);
			const FString Context = FString::Printf(TEXT("Texture option %d"), Option + 1);
			ExpectTexture(Live, Fixture.Slots1, Fixture.M1, ExpectedA[Option], Context);
			ExpectTexture(Live, Fixture.Slots2, Fixture.M2, ExpectedB[Option], Context);
		}
		// The material switch replaces the material; the texture of M1 then does not apply.
		SetState(Live, MaterialVar, 1);
		ExpectMaterial(Live, Fixture.Slots1, Fixture.M3, TEXT("Material option 2"));
		ExpectTexture(Live, Fixture.Slots2, Fixture.M2, T[0], TEXT("Material option 2 leaves other slots"));
		// Back to the original material: its texture applies again, on the same MID.
		SetState(Live, MaterialVar, 0);
		ExpectTexture(Live, Fixture.Slots1, Fixture.M1, T[2], TEXT("Material option 1"));
		TestTrue(TEXT("The MID is created once and reused"), Live.Component->GetMaterial(Fixture.Slots1[0]) == FirstMID);
		// Visibility still works on the same slots, after materials and textures.
		SetState(Live, ShowVar, 1);
		ExpectShown(Live, Fixture.Slots1, false, TEXT("Hidden"));
		ExpectTexture(Live, Fixture.Slots1, Fixture.M1, T[2], TEXT("Hidden keeps the texture"));
		SetState(Live, ShowVar, 0);
		ExpectShown(Live, Fixture.Slots1, true, TEXT("Shown again"));
	}

	// Start with saves: the saved options come back.
	{
		ClearSaves();
		auto WriteSave = [&](const FName Variable, const int32 Value)
		{
			USaveGame* Save = UGameplayStatics::CreateSaveGameObject(SaveClass);
			const FIntProperty* Property = FindFProperty<FIntProperty>(SaveClass, FName(*(Variable.ToString() + TEXT("Save"))));
			if (TestTrue(*FString::Printf(TEXT("Save object for %s"), *Variable.ToString()), Save && Property))
			{
				Property->SetPropertyValue_InContainer(Save, Value);
				TestTrue(TEXT("Save written"), UGameplayStatics::SaveGameToSlot(Save, SlotName(Variable), 0));
			}
		};
		WriteSave(TextureVar, 2);
		WriteSave(MaterialVar, 1);
		WriteSave(ShowVar, 1);
		FLive Live = StartInstance();
		TestEqual(TEXT("Saved texture option is restored"), GetState(Live, TextureVar), 2);
		TestEqual(TEXT("Saved material option is restored"), GetState(Live, MaterialVar), 1);
		ExpectMaterial(Live, Fixture.Slots1, Fixture.M3, TEXT("Restored material"));
		ExpectTexture(Live, Fixture.Slots2, Fixture.M2, T[0], TEXT("Restored texture"));
		ExpectShown(Live, Fixture.Slots1, false, TEXT("Restored hidden"));

		WriteSave(MaterialVar, 0);
		FLive Again = StartInstance();
		ExpectTexture(Again, Fixture.Slots1, Fixture.M1, T[2], TEXT("Restored texture under the original material"));
	}
	return !HasAnyErrors();
}

#endif
