#include "HTBlueprintToggleGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "ImageUtils.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/UnrealType.h"

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
	TestTrue(TEXT("LightMap adjustment variable exists"), HasVariable(TEXT("AutomationTuner_LightMap_Adjustment")));
	TestTrue(TEXT("ID texture adjustment variable exists"), HasVariable(TEXT("AutomationTuner_ID_Tex_Adjustment")));
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
	TArray<UK2Node_CallFunction*> ExportCalls;
	bool bMetadataContainsBlackWhiteMode = false;
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
					bMetadataContainsBlackWhiteMode |= Comment->NodeComment.Contains(TEXT(";Mode=BlackWhiteV1;"));
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
				ExportCalls.Add(Call);
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
	TestEqual(TEXT("Each selected map has an original overwrite and a white contribution draw"), DrawTextureCount, 4);
	TestEqual(TEXT("Each selected map has an End Draw call"), EndDrawCount, 2);
	TestEqual(TEXT("Apply and Reset update both slot MIDs for both maps"), SetTextureCount, 8);
	TestEqual(TEXT("Each selected map has one PNG export call"), ExportRenderTargetCount, 2);
	TestEqual(TEXT("Each PNG export resolves the runtime Saved directory"), SavedDirectoryCount, 2);
	TestTrue(TEXT("Tuner metadata exposes both PNG export events and filenames"), bMetadataContainsPNGExport);
	TestTrue(TEXT("Tuner metadata identifies the signed black/white algorithm"), bMetadataContainsBlackWhiteMode);
	TestTrue(TEXT("Every generated export uses an RGB PNG filename"), bAllExportFileNamesArePNG);
	TestTrue(TEXT("MIDs use the slot's current material instead of a packaged hard reference"), bAllMIDSourceMaterialsAreRuntimeCurrent);
	TestEqual(TEXT("Both tuner maps create a render target"), CreateRTCalls.Num(), 2);
	for (const UK2Node_CallFunction* Call : CreateRTCalls)
	{
		const UEdGraphPin* MipsPin = Call->FindPin(TEXT("bAutoGenerateMipMaps"));
		TestTrue(TEXT("Preview targets cannot sample stale lower mips"), MipsPin && MipsPin->DefaultValue == TEXT("false"));
	}

	// Execute the actual generated Apply/Reset/Export events with GPU textures.
	// Cover black source pixels, mixed channels, both endpoints, redraws, and PNG bytes.
	if (FApp::CanEverRender() && FirstResult.bSuccess)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (TestNotNull(TEXT("World for GPU tuner regression"), World))
		{
			// Keep exports isolated from any user files while testing the generated export nodes.
			const FString RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			TMap<FName, FString> ExportFiles;
			for (UK2Node_CallFunction* Export : ExportCalls)
			{
				const UEdGraphPin* RTPin = Export->FindPin(TEXT("TextureRenderTarget"));
				if (RTPin && RTPin->LinkedTo.Num() == 1)
				{
					const FName RTName = RTPin->LinkedTo[0]->PinName;
					const FString FileName = FString::Printf(TEXT("HTTunerTest_%s_%s_RGB.png"), *RunId, *RTName.ToString());
					Export->FindPin(TEXT("FileName"))->DefaultValue = FileName;
					ExportFiles.Add(RTName, FPaths::Combine(FPaths::ProjectSavedDir(), FileName));
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(TestAnim);
			FKismetEditorUtilities::CompileBlueprint(TestAnim);
			TestTrue(TEXT("GPU test blueprint compiles"), TestAnim->Status != BS_Error);

			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
			AActor* Owner = World->SpawnActor<AActor>(SpawnParams);
			if (TestNotNull(TEXT("Transient tuner test actor"), Owner))
			{
				USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Owner, NAME_None, RF_Transient);
				Owner->AddInstanceComponent(Component);
				UAnimInstance* Instance = NewObject<UAnimInstance>(Component, TestAnim->GeneratedClass);
				auto SetObject = [this, Instance](const FName Name, UObject* Value)
				{
					FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Instance->GetClass(), Name);
					if (TestNotNull(*Name.ToString(), Property))
					{
						Property->SetObjectPropertyValue_InContainer(Instance, Value);
					}
				};
				UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Params.SourceMaterialPath);
				TArray<UMaterialInstanceDynamic*> MIDs;
				for (int32 Slot = 0; Slot < 2; ++Slot)
				{
					UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, Owner);
					MIDs.Add(MID);
					SetObject(FName(*FString::Printf(TEXT("AutomationTunerMID_%d"), Slot)), MID);
				}
				const uint8 Levels[] = { 0, 60, 160, 255 };
				TArray<FColor> SourcePixels;
				for (int32 Y = 0; Y < 64; ++Y)
				{
					for (int32 X = 0; X < 64; ++X)
					{
						SourcePixels.Add(FColor(Levels[X / 16], Levels[3 - X / 16], 160, Levels[X / 16]));
					}
				}
				UTexture2D* Original = UTexture2D::CreateTransient(64, 64, PF_B8G8R8A8, NAME_None,
					TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(SourcePixels.GetData()), SourcePixels.Num() * sizeof(FColor)));
				Original->SRGB = false;
				Original->Filter = TF_Nearest;
				Original->UpdateResource();
				const TArray<FLinearColor> Adjustments = {
					FLinearColor(0, 0, 0, 1), FLinearColor(1, 0, 0, 1), FLinearColor(-1, 0, 0, 1),
					FLinearColor(0.5f, -0.5f, 0.25f, 1), FLinearColor(1, 1, 1, 1),
					FLinearColor(-1, -1, -1, 1), FLinearColor(0, 0, 0, 1),
					FLinearColor(2, -2, 0.5f, 1), FLinearColor(0, 0, 0, 1)
				};
				auto AdjustByte = [](const uint8 Value, const float Amount)
				{
					const float T = FMath::Clamp(Amount, -1.0f, 1.0f);
					return static_cast<uint8>(FMath::RoundToInt(T < 0 ? Value * (1 + T) : Value + (255 - Value) * T));
				};
				for (const FName MapName : { FName(TEXT("LightMap")), FName(TEXT("ID_Tex")) })
				{
					const FString Prefix = TEXT("AutomationTuner_") + MapName.ToString();
					UTextureRenderTarget2D* Tuned = UKismetRenderingLibrary::CreateRenderTarget2D(World, 64, 64, RTF_RGBA8, FLinearColor::Black, false);
					SetObject(FName(*(Prefix + TEXT("_Source"))), Original);
					SetObject(FName(*(Prefix + TEXT("_RT"))), Tuned);
					FStructProperty* AdjustmentProperty = FindFProperty<FStructProperty>(Instance->GetClass(), FName(*(Prefix + TEXT("_Adjustment"))));
					UFunction* Apply = Instance->FindFunction(FName(*(TEXT("HTTC_Apply_") + Prefix)));
					UFunction* Export = Instance->FindFunction(FName(*(TEXT("HTTC_Export_") + Prefix)));
					UFunction* Reset = Instance->FindFunction(FName(*(TEXT("HTTC_Reset_") + Prefix)));
					const FString* ExportFile = ExportFiles.Find(FName(*(Prefix + TEXT("_RT"))));
					if (TestNotNull(TEXT("Adjustment property"), AdjustmentProperty) && TestNotNull(TEXT("Apply event"), Apply)
						&& TestNotNull(TEXT("Export event"), Export) && TestNotNull(TEXT("Reset event"), Reset) && TestNotNull(TEXT("PNG path"), ExportFile))
					{
						TestTrue(TEXT("New adjustment defaults to neutral"), AdjustmentProperty->ContainerPtrToValuePtr<FLinearColor>(Instance)->Equals(FLinearColor(0, 0, 0, 1)));
						for (const FLinearColor& Adjustment : Adjustments)
						{
							*AdjustmentProperty->ContainerPtrToValuePtr<FLinearColor>(Instance) = Adjustment;
							Instance->ProcessEvent(Apply, nullptr);
							FImage Preview;
							if (TestTrue(TEXT("Read generated preview"), FImageUtils::GetRenderTargetImage(Tuned, Preview))
								&& TestTrue(TEXT("RGBA8 preview format"), Preview.Format == ERawImageFormat::BGRA8))
							{
								const FColor* Pixels = reinterpret_cast<const FColor*>(Preview.RawData.GetData());
								int32 BadPixels = 0;
								for (int32 Pixel = 0; Pixel < SourcePixels.Num(); ++Pixel)
								{
									const FColor& Before = SourcePixels[Pixel];
									BadPixels += FMath::Abs(int32(Pixels[Pixel].R) - AdjustByte(Before.R, Adjustment.R)) > 2
										|| FMath::Abs(int32(Pixels[Pixel].G) - AdjustByte(Before.G, Adjustment.G)) > 2
										|| FMath::Abs(int32(Pixels[Pixel].B) - AdjustByte(Before.B, Adjustment.B)) > 2
										|| Pixels[Pixel].A != Before.A;
								}
								TestEqual(FString::Printf(TEXT("%s %s updates every RGB pixel and preserves alpha"), *MapName.ToString(), *Adjustment.ToString()), BadPixels, 0);
								// Export via the generated event, then decode PNG and compare exact stored bytes.
								Instance->ProcessEvent(Export, nullptr);
								FImage PNG;
								if (TestTrue(TEXT("Generated export writes a decodable PNG"), FImageUtils::LoadImage(**ExportFile, PNG)))
								{
									TestTrue(TEXT("PNG equals the current preview including alpha"), PNG.SizeX == Preview.SizeX && PNG.SizeY == Preview.SizeY
										&& PNG.Format == Preview.Format && PNG.RawData == Preview.RawData);
								}
							}
							// Probe a constant stripe at three scales to detect stale lower-level sampling.
							for (const int32 ProbeSize : { 64, 8, 1 })
							{
								UTextureRenderTarget2D* Probe = UKismetRenderingLibrary::CreateRenderTarget2D(World, ProbeSize, ProbeSize, RTF_RGBA8, FLinearColor::Black, false);
								UCanvas* Canvas = nullptr;
								FVector2D Size;
								FDrawToRenderTargetContext Context;
								UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Probe, Canvas, Size, Context);
								if (Canvas)
								{
									Canvas->K2_DrawTexture(Tuned, FVector2D::ZeroVector, Size, FVector2D(0.0625f, 0.25f), FVector2D(0.125f, 0.5f), FLinearColor::White, BLEND_Opaque);
									UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
								}
								const FLinearColor Actual = UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Probe, ProbeSize / 2, ProbeSize / 2, false);
								TestTrue(FString::Printf(TEXT("%s adjustment %s at %dx%d has no old pixel data (%s)"), *MapName.ToString(), *Adjustment.ToString(), ProbeSize, ProbeSize, *Actual.ToString()),
									FMath::IsNearlyEqual(Actual.R, float(AdjustByte(0, Adjustment.R)), 2.0f)
									&& FMath::IsNearlyEqual(Actual.G, float(AdjustByte(255, Adjustment.G)), 2.0f)
									&& FMath::IsNearlyEqual(Actual.B, float(AdjustByte(160, Adjustment.B)), 2.0f));
								UKismetRenderingLibrary::ReleaseRenderTarget2D(Probe);
							}
						}
						Instance->ProcessEvent(Reset, nullptr);
						TestTrue(TEXT("Reset restores neutral adjustment"), AdjustmentProperty->ContainerPtrToValuePtr<FLinearColor>(Instance)->Equals(FLinearColor(0, 0, 0, 1)));
						for (UMaterialInstanceDynamic* MID : MIDs)
						{
							TestTrue(TEXT("Reset restores the exact original texture on each MID"), MID->K2_GetTextureParameterValue(MapName) == Original);
						}
					}
					UKismetRenderingLibrary::ReleaseRenderTarget2D(Tuned);
				}
				World->DestroyActor(Owner);
				AddInfo(TEXT("Generated black/white Apply/Reset/Export verified for both maps: 0/60/160/255 sources, endpoints, mixed RGB, repeated neutral, PNG pixels, alpha, and 64x64/8x8/1x1 sampling."));
			}
			for (const auto& File : ExportFiles)
			{
				IFileManager::Get().Delete(*File.Value, false, true);
			}
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

#endif
