#include "HTBlueprintToggleGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

// Texture and Material switches in the toggle tree condition model (no assets involved).
namespace HTNestedToggleEffectTests
{
	static FString SlotMaterial(const int32 Slot)
	{
		return FString::Printf(TEXT("/Game/Fx/M_Slot%d.M_Slot%d"), Slot, Slot);
	}

	static FHTNestedToggleGraph MakeGraph()
	{
		FHTNestedToggleGraph Graph;
		Graph.Name = TEXT("Fx");
		for (int32 Slot = 0; Slot < 10; ++Slot)
		{
			Graph.SlotMaterialPaths.Add(Slot, SlotMaterial(Slot));
		}
		return Graph;
	}

	static FHTNestedToggleSwitch MakeSwitch(const FString& Id, const FString& Key, const int32 Options, const FString& ParentId = FString(), const int32 ParentOption = INDEX_NONE)
	{
		FHTNestedToggleSwitch Switch;
		Switch.NodeId = Id;
		Switch.KeyName = Key;
		Switch.OptionCount = Options;
		Switch.ParentNodeId = ParentId;
		Switch.ParentOption = ParentOption;
		return Switch;
	}

	static FHTNestedToggleSwitch MakeMaterialSwitch(const FString& Id, const FString& Key, const TArray<int32>& Slots, const TArray<FString>& Materials, const FString& ParentId = FString(), const int32 ParentOption = INDEX_NONE)
	{
		FHTNestedToggleSwitch Switch = MakeSwitch(Id, Key, FMath::Max(2, Materials.Num()), ParentId, ParentOption);
		Switch.Kind = EHTNestedToggleSwitchKind::Material;
		Switch.MaterialIDs = Slots;
		Switch.TargetPartNodeIds = { Id + TEXT("_Part") };
		Switch.OptionMaterialPaths = Materials;
		return Switch;
	}

	static FHTNestedToggleTextureTarget MakeTarget(const FString& PartId, const TArray<int32>& Slots, const FString& Parent, const TArray<FString>& Textures)
	{
		FHTNestedToggleTextureTarget Target;
		Target.PartNodeId = PartId;
		Target.MaterialIDs = Slots;
		Target.ParentMaterialPath = Parent;
		Target.TexturePaths = Textures;
		return Target;
	}

	static FHTNestedToggleSwitch MakeTextureSwitch(const FString& Id, const FString& Key, const int32 Options, const FString& Parameter, const TArray<FHTNestedToggleTextureTarget>& Targets, const FString& ParentId = FString(), const int32 ParentOption = INDEX_NONE)
	{
		FHTNestedToggleSwitch Switch = MakeSwitch(Id, Key, Options, ParentId, ParentOption);
		Switch.Kind = EHTNestedToggleSwitchKind::Texture;
		Switch.TextureParameterName = Parameter;
		Switch.TextureTargets = Targets;
		return Switch;
	}

	static FHTNestedTogglePart MakePart(const FString& Id, const TArray<int32>& Slots, const FString& SwitchId = FString(), const int32 Option = 0)
	{
		FHTNestedTogglePart Part;
		Part.NodeId = Id;
		Part.MaterialIDs = Slots;
		if (!SwitchId.IsEmpty())
		{
			FHTNestedTogglePathStep& Step = Part.Path.AddDefaulted_GetRef();
			Step.SwitchNodeId = SwitchId;
			Step.Option = Option;
		}
		return Part;
	}

	static bool Mentions(const TArray<FHTNestedToggleIssue>& Issues, const TArray<FString>& NodeIds)
	{
		for (const FHTNestedToggleIssue& Issue : Issues)
		{
			bool bAll = true;
			for (const FString& Id : NodeIds)
			{
				bAll &= Issue.NodeIds.Contains(Id);
			}
			if (bAll)
			{
				return true;
			}
		}
		return false;
	}

	static FString Describe(const TArray<FHTNestedToggleIssue>& Issues)
	{
		return FString::JoinBy(Issues, TEXT(" | "), [](const FHTNestedToggleIssue& Issue) { return Issue.ToDisplayString(); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTNestedToggleEffectRulesTest,
	"HT.BlueprintToggleTool.NestedToggleEffectRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTNestedToggleEffectRulesTest::RunTest(const FString& Parameters)
{
	using namespace HTNestedToggleEffectTests;
	auto Compile = [](const FHTNestedToggleGraph& Graph, FHTNestedToggleCompiledGraph& Compiled, TArray<FHTNestedToggleIssue>& Errors)
	{
		Errors.Reset();
		return FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Graph, Compiled, Errors);
	};
	FHTNestedToggleCompiledGraph Compiled;
	TArray<FHTNestedToggleIssue> Errors;

	// A valid tree: style switch, a nested texture switch, a root material switch, cross-kind key sharing.
	{
		FHTNestedToggleGraph Graph = MakeGraph();
		Graph.Switches.Add(MakeSwitch(TEXT("Style"), TEXT("alt 5"), 2));
		Graph.Switches.Add(MakeTextureSwitch(TEXT("Tex"), TEXT("alt 9"), 3, TEXT("BaseColor"), {
			MakeTarget(TEXT("PartA"), { 1, 2 }, TEXT("/Game/Fx/MI_Coat.MI_Coat"), { TEXT("/Game/Fx/T_1.T_1"), TEXT(""), TEXT("/Game/Fx/T_3.T_3") }),
			MakeTarget(TEXT("PartB"), { 3 }, FString(), { TEXT("/Game/Fx/T_1.T_1") }) }, TEXT("Style"), 1));
		Graph.Switches.Add(MakeMaterialSwitch(TEXT("Mat"), TEXT("alt 8"), { 1, 4 }, { FString(), TEXT("/Game/Fx/MI_Red.MI_Red") }));
		// Same key as the style switch: one variable for a visibility and a material switch.
		Graph.Switches.Add(MakeMaterialSwitch(TEXT("StyleMat"), TEXT("alt 5"), { 6 }, { TEXT("/Game/Fx/MI_A.MI_A"), TEXT("/Game/Fx/MI_B.MI_B") }));
		Graph.Parts.Add(MakePart(TEXT("PartA"), { 1, 2 }, TEXT("Style"), 0));
		if (!TestTrue(*(TEXT("Valid effect tree compiles: ") + Describe(Errors)), Compile(Graph, Compiled, Errors)))
		{
			return false;
		}
		TestEqual(TEXT("Style and StyleMat share one variable"), Compiled.SwitchVariableIndices[0], Compiled.SwitchVariableIndices[3]);
		TestEqual(TEXT("Three state variables"), Compiled.Variables.Num(), 3);
		TestTrue(TEXT("Effect slots are sorted and unique"), Compiled.EffectMaterialIDs == TArray<int32>({ 1, 2, 3, 4, 6 }));
		TestEqual(TEXT("One texture effect per target slot"), Compiled.TextureEffects.Num(), 3);
		TestEqual(TEXT("One material effect per slot"), Compiled.MaterialEffects.Num(), 3);
		for (const FHTNestedToggleCompiledTextureEffect& Effect : Compiled.TextureEffects)
		{
			TestEqual(TEXT("Nested texture switch is gated by style option 1"), Effect.Conditions.Num(), 1);
			if (Effect.Conditions.Num() == 1)
			{
				TestEqual(TEXT("Gate variable is the style"), Effect.Conditions[0].VariableIndex, Compiled.SwitchVariableIndices[0]);
				TestEqual(TEXT("Gate option"), Effect.Conditions[0].Option, 1);
			}
			TestEqual(TEXT("Texture options are padded to the option count"), Effect.TexturePaths.Num(), 3);
			TestEqual(TEXT("Parameter"), Effect.ParameterName, FName(TEXT("BaseColor")));
			if (Effect.MaterialID == 3)
			{
				TestEqual(TEXT("Manual target belongs to the slot's original material"), Effect.ParentMaterialPath, SlotMaterial(3));
			}
			else
			{
				TestEqual(TEXT("By material target keeps its material"), Effect.ParentMaterialPath, FString(TEXT("/Game/Fx/MI_Coat.MI_Coat")));
			}
		}
		for (const FHTNestedToggleCompiledMaterialEffect& Effect : Compiled.MaterialEffects)
		{
			if (Effect.MaterialID == 1)
			{
				TestTrue(TEXT("Empty material option restores the original"), Effect.OptionMaterialPaths.Num() == 2 && Effect.OptionMaterialPaths[0] == SlotMaterial(1));
				TestEqual(TEXT("Root material switch has no gate"), Effect.Conditions.Num(), 0);
			}
		}

		// Mismatched option counts across kinds are still rejected.
		FHTNestedToggleGraph Mismatch = Graph;
		Mismatch.Switches[3].OptionCount = 3;
		Mismatch.Switches[3].OptionMaterialPaths.Add(FString());
		TestFalse(TEXT("Shared key needs the same option count across kinds"), Compile(Mismatch, Compiled, Errors));
		TestTrue(TEXT("The rejection names both switches"), Mentions(Errors, { TEXT("Style"), TEXT("StyleMat") }));
	}

	// Leaves: nothing hangs on a Texture or Material switch.
	{
		FHTNestedToggleGraph Graph = MakeGraph();
		Graph.Switches.Add(MakeTextureSwitch(TEXT("Tex"), TEXT("alt 9"), 2, TEXT("BaseColor"), { MakeTarget(TEXT("PartA"), { 1 }, FString(), {}) }));
		FHTNestedToggleGraph WithPart = Graph;
		Graph.Switches.Add(MakeSwitch(TEXT("Child"), TEXT("alt 6"), 2, TEXT("Tex"), 0));
		TestFalse(TEXT("A switch hanging on a texture switch is rejected"), Compile(Graph, Compiled, Errors));
		TestTrue(TEXT("Switch on a texture switch names both"), Mentions(Errors, { TEXT("Child"), TEXT("Tex") }));
		WithPart.Parts.Add(MakePart(TEXT("PartB"), { 2 }, TEXT("Tex"), 1));
		TestFalse(TEXT("A part hanging on a texture switch is rejected"), Compile(WithPart, Compiled, Errors));
		TestTrue(TEXT("Part on a texture switch names both"), Mentions(Errors, { TEXT("PartB"), TEXT("Tex") }));
	}

	// Incomplete effect switches.
	{
		FHTNestedToggleGraph Graph = MakeGraph();
		Graph.Switches.Add(MakeTextureSwitch(TEXT("NoParameter"), TEXT("alt 1"), 2, FString(), { MakeTarget(TEXT("PartA"), { 1 }, FString(), {}) }));
		Graph.Switches.Add(MakeTextureSwitch(TEXT("NoTarget"), TEXT("alt 2"), 2, TEXT("BaseColor"), {}));
		Graph.Switches.Add(MakeMaterialSwitch(TEXT("NoSlots"), TEXT("alt 3"), {}, {}));
		TestFalse(TEXT("Incomplete effect switches are rejected"), Compile(Graph, Compiled, Errors));
		for (const TCHAR* Id : { TEXT("NoParameter"), TEXT("NoTarget"), TEXT("NoSlots") })
		{
			TestTrue(*FString::Printf(TEXT("%s is reported"), Id), Mentions(Errors, { Id }));
		}
		// A slot without an original material (not on the preview mesh).
		FHTNestedToggleGraph Unknown = MakeGraph();
		Unknown.Switches.Add(MakeMaterialSwitch(TEXT("UnknownSlot"), TEXT("alt 4"), { 42 }, {}));
		Unknown.Switches.Add(MakeTextureSwitch(TEXT("UnknownTarget"), TEXT("alt 5"), 2, TEXT("BaseColor"), { MakeTarget(TEXT("PartX"), { 43 }, FString(), {}) }));
		TestFalse(TEXT("Slots missing from the mesh are rejected"), Compile(Unknown, Compiled, Errors));
		TestTrue(TEXT("UnknownSlot is reported"), Mentions(Errors, { TEXT("UnknownSlot") }));
		TestTrue(TEXT("UnknownTarget is reported with its part"), Mentions(Errors, { TEXT("UnknownTarget"), TEXT("PartX") }));
	}

	// Material switches on one slot: rejected unless their paths are mutually exclusive.
	{
		FHTNestedToggleGraph Graph = MakeGraph();
		Graph.Switches.Add(MakeSwitch(TEXT("Style"), TEXT("alt 5"), 3));
		Graph.Switches.Add(MakeMaterialSwitch(TEXT("MatA"), TEXT("alt 1"), { 5 }, { FString(), TEXT("/Game/Fx/MI_A.MI_A") }, TEXT("Style"), 0));
		Graph.Switches.Add(MakeMaterialSwitch(TEXT("MatB"), TEXT("alt 2"), { 5 }, { FString(), TEXT("/Game/Fx/MI_B.MI_B") }, TEXT("Style"), 1));
		TestTrue(*(TEXT("Different options of one switch may switch the same slot: ") + Describe(Errors)), Compile(Graph, Compiled, Errors));

		Graph.Switches[2].ParentOption = 0;
		TestFalse(TEXT("The same option may not"), Compile(Graph, Compiled, Errors));
		TestTrue(TEXT("The overlap names both material switches"), Mentions(Errors, { TEXT("MatA"), TEXT("MatB") }));

		Graph.Switches[2].ParentNodeId.Reset();
		Graph.Switches[2].ParentOption = INDEX_NONE;
		TestFalse(TEXT("An unconditional switch overlaps a gated one"), Compile(Graph, Compiled, Errors));
		TestTrue(TEXT("Unconditional overlap is reported"), Mentions(Errors, { TEXT("MatA"), TEXT("MatB") }));
	}

	// Texture switches on one slot and parameter.
	{
		FHTNestedToggleGraph Base = MakeGraph();
		Base.Switches.Add(MakeSwitch(TEXT("Style"), TEXT("alt 5"), 2));
		Base.Switches.Add(MakeTextureSwitch(TEXT("TexA"), TEXT("alt 1"), 2, TEXT("BaseColor"), { MakeTarget(TEXT("PartA"), { 7 }, FString(), {}) }));
		Base.Switches.Add(MakeTextureSwitch(TEXT("TexB"), TEXT("alt 2"), 2, TEXT("BaseColor"), { MakeTarget(TEXT("PartB"), { 7 }, FString(), {}) }));

		TestFalse(TEXT("Same slot, parameter and material at the same time is rejected"), Compile(Base, Compiled, Errors));
		TestTrue(TEXT("The overlap names both texture switches"), Mentions(Errors, { TEXT("TexA"), TEXT("TexB") }));

		FHTNestedToggleGraph OtherParameter = Base;
		OtherParameter.Switches[2].TextureParameterName = TEXT("Normal");
		TestTrue(*(TEXT("Different parameters may share a slot: ") + Describe(Errors)), Compile(OtherParameter, Compiled, Errors));

		FHTNestedToggleGraph OtherMaterial = Base;
		OtherMaterial.Switches[1].TextureTargets[0].ParentMaterialPath = TEXT("/Game/Fx/MI_A.MI_A");
		OtherMaterial.Switches[2].TextureTargets[0].ParentMaterialPath = TEXT("/Game/Fx/MI_B.MI_B");
		TestTrue(*(TEXT("Textures of different materials never apply together: ") + Describe(Errors)), Compile(OtherMaterial, Compiled, Errors));

		FHTNestedToggleGraph Exclusive = Base;
		Exclusive.Switches[1].ParentNodeId = TEXT("Style");
		Exclusive.Switches[1].ParentOption = 0;
		Exclusive.Switches[2].ParentNodeId = TEXT("Style");
		Exclusive.Switches[2].ParentOption = 1;
		TestTrue(*(TEXT("Mutually exclusive texture switches may share a slot: ") + Describe(Errors)), Compile(Exclusive, Compiled, Errors));

		FHTNestedToggleGraph SameSwitch = MakeGraph();
		SameSwitch.Switches.Add(MakeTextureSwitch(TEXT("Tex"), TEXT("alt 1"), 2, TEXT("BaseColor"), {
			MakeTarget(TEXT("PartA"), { 3, 4 }, FString(), {}),
			MakeTarget(TEXT("PartB"), { 4 }, FString(), {}) }));
		TestFalse(TEXT("One slot through two targets of one switch is rejected"), Compile(SameSwitch, Compiled, Errors));
		TestTrue(TEXT("The overlap names the switch and both parts"), Mentions(Errors, { TEXT("Tex"), TEXT("PartA"), TEXT("PartB") }));
	}

	// A slot may be hidden by visibility and switched at the same time.
	{
		FHTNestedToggleGraph Graph = MakeGraph();
		Graph.Switches.Add(MakeSwitch(TEXT("Show"), TEXT("alt 7"), 2));
		Graph.Switches.Add(MakeTextureSwitch(TEXT("Tex"), TEXT("alt 9"), 2, TEXT("BaseColor"), { MakeTarget(TEXT("PartA"), { 1 }, FString(), {}) }));
		Graph.Switches.Add(MakeMaterialSwitch(TEXT("Mat"), TEXT("alt 8"), { 1 }, { FString(), TEXT("/Game/Fx/MI_A.MI_A") }));
		Graph.Parts.Add(MakePart(TEXT("PartA"), { 1 }, TEXT("Show"), 0));
		TestTrue(*(TEXT("Visibility, texture and material on one slot compile: ") + Describe(Errors)), Compile(Graph, Compiled, Errors));
		TestEqual(TEXT("The slot is still a visibility part"), Compiled.Parts.Num(), 1);
	}
	return !HasAnyErrors();
}

#endif
