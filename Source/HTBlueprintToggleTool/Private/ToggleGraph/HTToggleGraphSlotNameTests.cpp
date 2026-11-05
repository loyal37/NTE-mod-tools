// Automation test for Part slot names: binding, Refresh after mesh slots moved, and the Compile check.
#include "HTToggleGraph.h"
#include "HTToggleGraphCompiler.h"
#include "HTToggleGraphNodes.h"
#include "HTToggleGraphSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace HTToggleGraphSlotNameTests
{
	static void SetSlots(USkeletalMesh* Mesh, const TArray<FName>& Names)
	{
		TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		Materials.Reset();
		for (const FName Name : Names)
		{
			FSkeletalMaterial& Material = Materials.AddDefaulted_GetRef();
			Material.MaterialSlotName = Name;
		}
	}

	static UHTToggleGraphNode_Part* SpawnPart(UEdGraph* Graph, const float Y)
	{
		return Cast<UHTToggleGraphNode_Part>(FHTToggleGraphSchemaAction_NewNode::SpawnNode(Graph, UHTToggleGraphNode_Part::StaticClass(), FVector2f(-600.0f, Y), nullptr, false));
	}

	static bool AnyContains(const TArray<FString>& Lines, const FString& Text)
	{
		return Lines.ContainsByPredicate([&Text](const FString& Line) { return Line.Contains(Text); });
	}

	static bool AnyIssueContains(const TArray<FHTNestedToggleIssue>& Issues, const FString& Text)
	{
		return Issues.ContainsByPredicate([&Text](const FHTNestedToggleIssue& Issue) { return Issue.Message.Contains(Text); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTToggleGraphSlotNamesTest,
	"HT.BlueprintToggleTool.ToggleGraphSlotNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTToggleGraphSlotNamesTest::RunTest(const FString& Parameters)
{
	using namespace HTToggleGraphSlotNameTests;
	// Undo needs transactional, non-transient objects; /Temp packages are never saved.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HTSlotNames_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	ON_SCOPE_EXIT
	{
		Package->SetDirtyFlag(false);
	};
	TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>(Package, TEXT("SK_Slots"), RF_Public | RF_Transactional));
	SetSlots(Mesh.Get(), { TEXT("body"), TEXT("sleeve"), TEXT("skirt") });
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = UAnimInstance::StaticClass();
	Factory->bTemplate = true;
	TStrongObjectPtr<UAnimBlueprint> Anim(Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, TEXT("ABP_Slots"), RF_Public | RF_Transactional, nullptr, GWarn)));
	if (!TestTrue(TEXT("Test Anim Blueprint"), Anim.IsValid()))
	{
		return false;
	}
	Anim->SetPreviewMesh(Mesh.Get(), false);
	TStrongObjectPtr<UHTToggleGraph> Asset(NewObject<UHTToggleGraph>(Package, TEXT("TG_Slots"), RF_Public | RF_Transactional));
	Asset->AnimBlueprint = Anim.Get();
	UEdGraph* Graph = Asset->EnsureGraph();
	if (!TestTrue(TEXT("The toggle graph sees the preview mesh"), Graph && Asset->ResolvePreviewMesh() == Mesh.Get()))
	{
		return false;
	}

	// Picking IDs records their slot names: typed IDs and the slot checkboxes.
	UHTToggleGraphNode_Part* Sleeve = SpawnPart(Graph, 0.0f);
	UHTToggleGraphNode_Part* Skirt = SpawnPart(Graph, 200.0f);
	UHTToggleGraphNode_Part* Legacy = SpawnPart(Graph, 400.0f);
	if (!TestTrue(TEXT("Parts spawn"), Sleeve && Skirt && Legacy))
	{
		return false;
	}
	TestTrue(TEXT("Typed IDs are set"), Sleeve->SetMaterialIDsInline({ 1 }));
	TestTrue(TEXT("Typed IDs record their slot names"), Sleeve->SlotNames == TArray<FName>({ TEXT("sleeve") }));
	TestTrue(TEXT("A ticked slot is set"), Skirt->SetMaterialIDSelectedInline(2, true));
	TestTrue(TEXT("A ticked slot records its name"), Skirt->SlotNames == TArray<FName>({ TEXT("skirt") }));
	TestEqual(TEXT("The node shows the slot names"), Sleeve->GetSlotNamesText(), FString(TEXT("sleeve")));
	// A part saved before slot names were recorded.
	Legacy->MaterialIDs = { 0 };
	Legacy->SlotNames.Reset();

	FHTPartSlotReport Check = FHTToggleGraphCompiler::CheckPartSlots(Asset.Get());
	TestTrue(TEXT("Nothing moved yet"), Check.Errors.IsEmpty());
	TestEqual(TEXT("The unbound part is listed for binding"), Check.Bindings.Num(), 1);

	// A slot inserted in the middle shifts every later index.
	SetSlots(Mesh.Get(), { TEXT("body"), TEXT("collar"), TEXT("sleeve"), TEXT("skirt") });
	Check = FHTToggleGraphCompiler::CheckPartSlots(Asset.Get());
	TestEqual(TEXT("Compile's check finds both moved IDs"), Check.Errors.Num(), 2);
	TestTrue(TEXT("The check names the moved slot"), AnyIssueContains(Check.Errors, TEXT("slot sleeve moved from ID 1 to 2")));
	TestTrue(TEXT("The check changes nothing"), Sleeve->MaterialIDs == TArray<int32>({ 1 }) && Skirt->MaterialIDs == TArray<int32>({ 2 }));
	const FHTNestedToggleGeneratorResult Compile = FHTToggleGraphCompiler::Generate(Asset.Get(), false);
	TestFalse(TEXT("Compile refuses moved IDs"), Compile.bSuccess);
	TestTrue(TEXT("Compile tells the user to press Refresh"), AnyIssueContains(Compile.Errors, TEXT("Press Refresh")));
	TestTrue(TEXT("Compile lists the moved part"), Compile.Errors.ContainsByPredicate([Sleeve](const FHTNestedToggleIssue& Issue) { return Issue.NodeIds.Contains(Sleeve->GetNodeId()); }));

	// Refresh moves the IDs back onto their slots and binds the old part, as one transaction.
	FHTPartSlotReport Refresh = FHTToggleGraphCompiler::RefreshPartSlots(Asset.Get());
	AddInfo(Refresh.ToDisplayString());
	TestTrue(TEXT("Refresh has no errors"), Refresh.Errors.IsEmpty());
	TestEqual(TEXT("Refresh changed three parts"), Refresh.ChangedParts, 3);
	TestTrue(TEXT("Refresh lists the sleeve move"), AnyContains(Refresh.Changes, TEXT("ID 1 -> 2 (slot sleeve)")));
	TestTrue(TEXT("Refresh lists the skirt move"), AnyContains(Refresh.Changes, TEXT("ID 2 -> 3 (slot skirt)")));
	TestTrue(TEXT("Refresh lists the binding for the user to check"), AnyContains(Refresh.Bindings, TEXT("ID 0 -> slot body")));
	TestTrue(TEXT("The sleeve part follows its slot"), Sleeve->MaterialIDs == TArray<int32>({ 2 }) && Sleeve->SlotNames == TArray<FName>({ TEXT("sleeve") }));
	TestTrue(TEXT("The skirt part follows its slot"), Skirt->MaterialIDs == TArray<int32>({ 3 }));
	TestTrue(TEXT("The old part is bound"), Legacy->MaterialIDs == TArray<int32>({ 0 }) && Legacy->SlotNames == TArray<FName>({ TEXT("body") }));
	Check = FHTToggleGraphCompiler::CheckPartSlots(Asset.Get());
	TestTrue(TEXT("After Refresh Compile's check passes"), Check.Errors.IsEmpty() && Check.Bindings.IsEmpty());

	TestTrue(TEXT("Undo the refresh"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores the old IDs"), Sleeve->MaterialIDs == TArray<int32>({ 1 }) && Skirt->MaterialIDs == TArray<int32>({ 2 }));
	TestTrue(TEXT("Undo restores the unbound part"), Legacy->SlotNames.IsEmpty() || Legacy->SlotNames[0].IsNone());
	TestTrue(TEXT("Redo the refresh"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo moves the IDs again"), Sleeve->MaterialIDs == TArray<int32>({ 2 }) && Skirt->MaterialIDs == TArray<int32>({ 3 }));

	// A slot name that is gone, or that several slots share, keeps the ID and is reported.
	SetSlots(Mesh.Get(), { TEXT("body"), TEXT("collar"), TEXT("sleeve_new"), TEXT("skirt") });
	Refresh = FHTToggleGraphCompiler::RefreshPartSlots(Asset.Get());
	TestTrue(TEXT("A missing slot name is reported"), AnyIssueContains(Refresh.Errors, TEXT("slot sleeve (ID 2) is no longer on the preview mesh")));
	TestTrue(TEXT("The report selects the part"), Refresh.Errors.Num() == 1 && Refresh.Errors[0].NodeIds.Contains(Sleeve->GetNodeId()));
	TestTrue(TEXT("A missing slot name keeps the ID"), Sleeve->MaterialIDs == TArray<int32>({ 2 }));
	SetSlots(Mesh.Get(), { TEXT("body"), TEXT("collar"), TEXT("x"), TEXT("skirt"), TEXT("sleeve"), TEXT("sleeve") });
	Refresh = FHTToggleGraphCompiler::RefreshPartSlots(Asset.Get());
	TestTrue(TEXT("A slot name used twice is reported"), AnyIssueContains(Refresh.Errors, TEXT("2 slots named sleeve")));
	TestTrue(TEXT("A slot name used twice keeps the ID"), Sleeve->MaterialIDs == TArray<int32>({ 2 }));
	TestFalse(TEXT("Compile refuses an ambiguous slot"), FHTToggleGraphCompiler::Generate(Asset.Get(), false).bSuccess);
	return !HasAnyErrors();
}

#endif
