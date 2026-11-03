#include "HTBlueprintToggleGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "IAssetTools.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "ObjectTools.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTTextureChannelTunerGenerationTest,
	"HT.BlueprintToggleTool.TextureChannelTunerGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTTextureChannelTunerGenerationTest::RunTest(const FString& Parameters)
{
	const FString SourcePath(TEXT("/Game/Characters/Player/004_lacrimosa/lacrimosa_animbp.lacrimosa_animbp"));
	UAnimBlueprint* Source = LoadObject<UAnimBlueprint>(nullptr, *SourcePath);
	if (!TestNotNull(TEXT("Source Animation Blueprint"), Source))
	{
		return false;
	}

	const FString TestFolder(TEXT("/Game/HTTextureTunerAutomation"));
	const FString TestObjectPath(TEXT("/Game/HTTextureTunerAutomation/ABP_HTTextureTunerTest.ABP_HTTextureTunerTest"));
	if (UObject* Existing = LoadObject<UObject>(nullptr, *TestObjectPath))
	{
		ObjectTools::DeleteObjectsUnchecked({ Existing });
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UAnimBlueprint* TestAnim = Cast<UAnimBlueprint>(AssetTools.Get().DuplicateAsset(TEXT("ABP_HTTextureTunerTest"), TestFolder, Source));
	if (!TestNotNull(TEXT("Duplicated Animation Blueprint"), TestAnim))
	{
		return false;
	}

	FHTBlueprintToggleGeneratorParams Params;
	Params.Mode = EHTBlueprintToggleMode::TextureChannelTuner;
	Params.AnimBlueprintPath = TestAnim->GetPathName();
	Params.ToggleVariableName = TEXT("AutomationTuner");
	Params.MaterialElementIndices = { 0, 1 };
	Params.SourceMaterialPath = TEXT("/Game/Characters/Player/004_lacrimosa/ter_new/cloth_ter/MI_player_004_lacrimosa_01.MI_player_004_lacrimosa_01");
	Params.bTuneLightMap = true;
	Params.bTuneIDTexture = true;
	Params.LightMapTexturePath = TEXT("/Game/Characters/Player/004_lacrimosa/ter_new/cloth_ter/T_player_004_lacrimosa_01_m.T_player_004_lacrimosa_01_m");
	Params.IDTexturePath = TEXT("/Game/Characters/Player/004_lacrimosa/ter_new/cloth_ter/T_player_004_lacrimosa_01_id.T_player_004_lacrimosa_01_id");
	Params.bSaveAssets = false;

	const FHTBlueprintToggleGeneratorResult FirstResult = FHTBlueprintToggleGenerator::Generate(Params);
	if (!FirstResult.bSuccess)
	{
		AddError(FirstResult.ToDisplayString());
	}
	TestTrue(TEXT("Texture Channel Tuner generation succeeds"), FirstResult.bSuccess);
	TestTrue(TEXT("Generated Animation Blueprint compiles"), TestAnim->Status != BS_Error);

	auto HasVariable = [TestAnim](const FName Name)
	{
		return TestAnim->NewVariables.ContainsByPredicate([Name](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == Name;
		});
	};
	TestTrue(TEXT("Slot 0 MID variable exists"), HasVariable(TEXT("AutomationTunerMID_0")));
	TestTrue(TEXT("Slot 1 MID variable exists"), HasVariable(TEXT("AutomationTunerMID_1")));
	TestTrue(TEXT("LightMap gain variable exists"), HasVariable(TEXT("AutomationTuner_LightMap_Gain")));
	TestTrue(TEXT("ID texture gain variable exists"), HasVariable(TEXT("AutomationTuner_ID_Tex_Gain")));
	TestTrue(TEXT("LightMap render target variable exists"), HasVariable(TEXT("AutomationTuner_LightMap_RT")));
	TestTrue(TEXT("ID texture render target variable exists"), HasVariable(TEXT("AutomationTuner_ID_Tex_RT")));

	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(TestAnim);
	TestNotNull(TEXT("Generated Event Graph"), Graph);
	int32 MetadataCommentCount = 0;
	int32 CreateMIDCount = 0;
	int32 BeginDrawCount = 0;
	int32 DrawTextureCount = 0;
	int32 EndDrawCount = 0;
	int32 SetTextureCount = 0;
	int32 ExportRenderTargetCount = 0;
	int32 SavedDirectoryCount = 0;
	TArray<UK2Node_CallFunction*> CreateRTCalls;
	bool bMetadataContainsPNGExport = false;
	bool bAllExportFileNamesArePNG = true;
	bool bAllMIDSourceMaterialsAreRuntimeCurrent = true;
	if (Graph)
	{
		TSet<UEdGraphNode*> TunerNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
			{
				if (Comment->NodeComment.StartsWith(TEXT("HT Texture Tuner - AutomationTuner;")))
				{
					++MetadataCommentCount;
					bMetadataContainsPNGExport |= Comment->NodeComment.Contains(TEXT("HTTC_Export_AutomationTuner_LightMap"))
						&& Comment->NodeComment.Contains(TEXT("HTTC_Export_AutomationTuner_ID_Tex"))
						&& Comment->NodeComment.Contains(TEXT("_RGB.png"));
					for (UObject* Object : Comment->GetNodesUnderComment())
					{
						if (UEdGraphNode* TunerNode = Cast<UEdGraphNode>(Object))
						{
							TunerNodes.Add(TunerNode);
						}
					}
				}
			}
		}
		for (UEdGraphNode* Node : TunerNodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call)
			{
				continue;
			}
			const FName FunctionName = Call->FunctionReference.GetMemberName();
			CreateMIDCount += FunctionName == TEXT("CreateDynamicMaterialInstance") ? 1 : 0;
			BeginDrawCount += FunctionName == TEXT("BeginDrawCanvasToRenderTarget") ? 1 : 0;
			DrawTextureCount += FunctionName == TEXT("K2_DrawTexture") ? 1 : 0;
			EndDrawCount += FunctionName == TEXT("EndDrawCanvasToRenderTarget") ? 1 : 0;
			SetTextureCount += FunctionName == TEXT("SetTextureParameterValue") ? 1 : 0;
			ExportRenderTargetCount += FunctionName == TEXT("ExportRenderTarget") ? 1 : 0;
			SavedDirectoryCount += FunctionName == TEXT("GetProjectSavedDirectory") ? 1 : 0;
			if (FunctionName == TEXT("CreateRenderTarget2D"))
			{
				CreateRTCalls.Add(Call);
			}
			if (FunctionName == TEXT("ExportRenderTarget"))
			{
				const UEdGraphPin* FileNamePin = Call->FindPin(TEXT("FileName"));
				bAllExportFileNamesArePNG &= FileNamePin
					&& FileNamePin->DefaultValue.EndsWith(TEXT("_RGB.png"));
			}
			if (FunctionName == TEXT("CreateDynamicMaterialInstance"))
			{
				const UEdGraphPin* SourceMaterialPin = Call->FindPin(TEXT("SourceMaterial"));
				bAllMIDSourceMaterialsAreRuntimeCurrent &= SourceMaterialPin
					&& SourceMaterialPin->DefaultObject == nullptr
					&& SourceMaterialPin->LinkedTo.IsEmpty();
			}
		}
	}
	TestEqual(TEXT("One tuner metadata group is generated"), MetadataCommentCount, 1);
	TestEqual(TEXT("One MID is created per selected slot"), CreateMIDCount, 2);
	TestEqual(TEXT("Each selected map has a Begin Draw call"), BeginDrawCount, 2);
	TestEqual(TEXT("Each selected map has a Draw Texture call"), DrawTextureCount, 2);
	TestEqual(TEXT("Each selected map has an End Draw call"), EndDrawCount, 2);
	TestEqual(TEXT("Apply and Reset update both slot MIDs for both maps"), SetTextureCount, 8);
	TestEqual(TEXT("Each selected map has one PNG export call"), ExportRenderTargetCount, 2);
	TestEqual(TEXT("Each PNG export resolves the runtime Saved directory"), SavedDirectoryCount, 2);
	TestTrue(TEXT("Tuner metadata exposes both PNG export events and filenames"), bMetadataContainsPNGExport);
	TestTrue(TEXT("Every generated export uses an RGB PNG filename"), bAllExportFileNamesArePNG);
	TestTrue(TEXT("MIDs use the slot's current material instead of a packaged hard reference"), bAllMIDSourceMaterialsAreRuntimeCurrent);
	TestEqual(TEXT("Both tuner maps create a render target"), CreateRTCalls.Num(), 2);
	for (const UK2Node_CallFunction* Call : CreateRTCalls)
	{
		const UEdGraphPin* MipsPin = Call->FindPin(TEXT("bAutoGenerateMipMaps"));
		TestTrue(TEXT("Preview targets cannot sample stale lower mips"), MipsPin && MipsPin->DefaultValue == TEXT("false"));
	}

	// Exercise real GPU minification: the previous auto-mip setup can look correct at
	// full size while sampling old/cleared data when viewed smaller or at an angle.
	if (FApp::CanEverRender() && !CreateRTCalls.IsEmpty())
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const UEdGraphPin* MipsPin = CreateRTCalls[0]->FindPin(TEXT("bAutoGenerateMipMaps"));
		if (TestNotNull(TEXT("World for GPU tuner regression"), World) && MipsPin)
		{
			const FLinearColor SourceColor(160.0f / 255.0f, 100.0f / 255.0f, 200.0f / 255.0f, 1.0f);
			UTextureRenderTarget2D* Original = UKismetRenderingLibrary::CreateRenderTarget2D(World, 64, 64, RTF_RGBA8, SourceColor, false);
			UTextureRenderTarget2D* Tuned = UKismetRenderingLibrary::CreateRenderTarget2D(World, 64, 64, RTF_RGBA8, FLinearColor::Black, MipsPin->DefaultValue == TEXT("true"));
			if (TestNotNull(TEXT("GPU original texture"), Original) && TestNotNull(TEXT("GPU tuned texture"), Tuned))
			{
				UKismetRenderingLibrary::ClearRenderTarget2D(World, Original, SourceColor);
				auto Draw = [this, World](UTexture* SourceTexture, UTextureRenderTarget2D* Target, const FLinearColor& Gain)
				{
					UCanvas* Canvas = nullptr;
					FVector2D Size;
					FDrawToRenderTargetContext Context;
					UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Target, Canvas, Size, Context);
					if (!TestNotNull(TEXT("GPU drawing canvas"), Canvas))
					{
						return;
					}
					Canvas->K2_DrawTexture(SourceTexture, FVector2D::ZeroVector, Size, FVector2D::ZeroVector, FVector2D::UnitVector, Gain, BLEND_Opaque);
					UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
				};
				for (const float RedGain : { 1.0f, 0.0f, 2.0f, 1.0f })
				{
					Draw(Original, Tuned, FLinearColor(RedGain, 1.0f, 1.0f, 1.0f));
					// Raw RGBA8 reads with bNormalize=false return byte values, not 0..1.
					const FLinearColor Expected(FMath::Min(160.0f * RedGain, 255.0f), 100.0f, 200.0f, 255.0f);
					const FString PNGName = TEXT("HTGainRollback_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png");
					const FString PNGPath = FPaths::Combine(FPaths::ProjectSavedDir(), PNGName);
					UKismetRenderingLibrary::ExportRenderTarget(World, Tuned, FPaths::ProjectSavedDir(), PNGName);
					FImage PNG;
					if (TestTrue(TEXT("Multiplier render target exports a readable PNG"), FImageUtils::LoadImage(*PNGPath, PNG)))
					{
						if (TestTrue(TEXT("PNG is 64x64 BGRA8"), PNG.SizeX == 64 && PNG.SizeY == 64 && PNG.Format == ERawImageFormat::BGRA8))
						{
							const FColor* Pixels = reinterpret_cast<const FColor*>(PNG.RawData.GetData());
							TestTrue(TEXT("PNG retains multiplied and clamped channel values"), FMath::Abs(Pixels[0].R - Expected.R) <= 2 && FMath::Abs(Pixels[0].G - Expected.G) <= 2 && FMath::Abs(Pixels[0].B - Expected.B) <= 2);
						}
					}
					IFileManager::Get().Delete(*PNGPath, false, true);
					for (const int32 ProbeSize : { 64, 8, 1 })
					{
						UTextureRenderTarget2D* Probe = UKismetRenderingLibrary::CreateRenderTarget2D(World, ProbeSize, ProbeSize, RTF_RGBA8, FLinearColor::Black, false);
						if (TestNotNull(TEXT("GPU minification probe"), Probe))
						{
							Draw(Tuned, Probe, FLinearColor::White);
							const FLinearColor Actual = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Probe, ProbeSize / 2, ProbeSize / 2, false);
							TestTrue(FString::Printf(TEXT("R gain %.0f at %dx%d has current RGB, actual %s"), RedGain, ProbeSize, ProbeSize, *Actual.ToString()),
								FMath::IsNearlyEqual(Actual.R, Expected.R, 2.0f)
								&& FMath::IsNearlyEqual(Actual.G, Expected.G, 2.0f)
								&& FMath::IsNearlyEqual(Actual.B, Expected.B, 2.0f));
							UKismetRenderingLibrary::ReleaseRenderTarget2D(Probe);
						}
					}
				}
				AddInfo(TEXT("GPU multiplier/PNG/minification checked at 64x64, 8x8, and 1x1 with R gain 1 -> 0 -> 2 -> 1."));
			}
			UKismetRenderingLibrary::ReleaseRenderTarget2D(Tuned);
			UKismetRenderingLibrary::ReleaseRenderTarget2D(Original);
		}
	}
	else
	{
		AddInfo(TEXT("GPU minification check skipped because rendering is unavailable; run without -NullRHI to exercise it."));
	}

	// Seed the previous configuration so regeneration must actually upgrade an old tuner.
	for (UK2Node_CallFunction* Call : CreateRTCalls)
	{
		if (UEdGraphPin* MipsPin = Call->FindPin(TEXT("bAutoGenerateMipMaps")))
		{
			MipsPin->DefaultValue = TEXT("true");
		}
	}

	const FHTBlueprintToggleGeneratorResult SecondResult = FHTBlueprintToggleGenerator::Generate(Params);
	if (!SecondResult.bSuccess)
	{
		AddError(SecondResult.ToDisplayString());
	}
	TestTrue(TEXT("Repeated tuner generation succeeds"), SecondResult.bSuccess);
	MetadataCommentCount = 0;
	if (Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
			MetadataCommentCount += Comment && Comment->NodeComment.StartsWith(TEXT("HT Texture Tuner - AutomationTuner;")) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Repeated generation replaces the old tuner group"), MetadataCommentCount, 1);
	if (Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (Call && Call->FunctionReference.GetMemberName() == TEXT("CreateRenderTarget2D"))
			{
				const UEdGraphPin* MipsPin = Call->FindPin(TEXT("bAutoGenerateMipMaps"));
				TestTrue(TEXT("Regeneration removes the old auto-mip configuration"), MipsPin && MipsPin->DefaultValue == TEXT("false"));
			}
		}
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("HTKeepTestAssets")))
	{
		ObjectTools::DeleteObjectsUnchecked({ TestAnim });
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHTTextureTunerMultiplierRollbackTest,
	"HT.BlueprintToggleTool.TextureTunerMultiplierRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHTTextureTunerMultiplierRollbackTest::RunTest(const FString& Parameters)
{
	// Exercise migration on an isolated copy of the user's saved signed tuner.
	const FString Role(TEXT("/Game/Characters/Player/057_AKane_fashion2/"));
	UAnimBlueprint* Source = LoadObject<UAnimBlueprint>(nullptr, *(Role + TEXT("AKane_fashion2_animbp.AKane_fashion2_animbp")));
	if (!TestNotNull(TEXT("Read-only current character fixture"), Source)) { return false; }
	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UAnimBlueprint* Copy = Cast<UAnimBlueprint>(AssetTools.Get().DuplicateAsset(TEXT("ABP_Rollback"),
		TEXT("/Game/HTGainRollbackAutomation/Run_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), Source));
	if (!TestNotNull(TEXT("Isolated rollback copy"), Copy)) { return false; }
	FHTBlueprintToggleGeneratorParams Params;
	Params.Mode = EHTBlueprintToggleMode::TextureChannelTuner;
	Params.AnimBlueprintPath = Copy->GetPathName();
	Params.ToggleVariableName = TEXT("MI_blackbird_057_Akane_cloth_Tuner");
	for (int32 Slot = 13; Slot <= 27; ++Slot) { Params.MaterialElementIndices.Add(Slot); }
	Params.SourceMaterialPath = Role + TEXT("ter/cloth_ter/MI_blackbird_057_Akane_cloth.MI_blackbird_057_Akane_cloth");
	Params.LightMapTexturePath = Role + TEXT("ter/cloth_ter/057_Akane_cloth_m.057_Akane_cloth_m");
	Params.IDTexturePath = Role + TEXT("ter/cloth_ter/057_Akane_cloth_id.057_Akane_cloth_id");
	Params.bTuneLightMap = Params.bTuneIDTexture = true;
	Params.bSaveAssets = false;
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Copy);
	TArray<UEdGraphNode*> UnrelatedNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		// The saved fixture's tuner starts at Y=0; panel routing is above it.
		if (Node->NodePosY < 0) { UnrelatedNodes.Add(Node); }
	}
	const auto Result = FHTBlueprintToggleGenerator::Generate(Params);
	TestTrue(TEXT("Existing signed graph can regenerate as multiplier"), Result.bSuccess);
	if (!Result.bSuccess) { AddError(Result.ToDisplayString()); }
	int32 Comments = 0;
	for (const UEdGraphNode* Node : FBlueprintEditorUtils::FindEventGraph(Copy)->Nodes)
	{
		if (const auto* Event = Cast<UK2Node_CustomEvent>(Node))
		{
			const FString Name = Event->CustomFunctionName.ToString();
			TestFalse(TEXT("Later worker/prepare/diagnostic events removed"), Name.StartsWith(TEXT("HTTC_Work_")) || Name.StartsWith(TEXT("HTTC_Prepare_")) || Name.StartsWith(TEXT("HTTC_Status_")));
		}
		if (const auto* Comment = Cast<UEdGraphNode_Comment>(Node); Comment && Comment->NodeComment.StartsWith(TEXT("HT Texture Tuner - ")))
		{
			++Comments;
			TestTrue(TEXT("Restored metadata references multipliers and PNG"), Comment->NodeComment.Contains(TEXT("_Gain,")) && Comment->NodeComment.Contains(TEXT("_RGB.png")) && !Comment->NodeComment.Contains(TEXT("Mode=")));
		}
	}
	TestEqual(TEXT("Exactly one restored tuner group"), Comments, 1);
	for (const UEdGraphNode* Node : UnrelatedNodes)
	{
		TestTrue(TEXT("Existing panel nodes outside the tuner are preserved"), Graph->Nodes.Contains(Node));
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Comment = Cast<UEdGraphNode_Comment>(Node)) { Comment->ClearNodesUnderComment(); }
	}
	const auto ReopenedResult = FHTBlueprintToggleGenerator::Generate(Params);
	TestTrue(TEXT("Multiplier regeneration also works without transient comment membership"), ReopenedResult.bSuccess);
	int32 MIDCount = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const auto* Call = Cast<UK2Node_CallFunction>(Node); Call && Call->FunctionReference.GetMemberName() == TEXT("CreateDynamicMaterialInstance")) { ++MIDCount; }
	}
	TestEqual(TEXT("No duplicate slot initialization remains after regeneration"), MIDCount, 15);
	ObjectTools::DeleteObjectsUnchecked({ Copy });
	return !HasAnyErrors();
}

#endif
