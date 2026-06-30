#include "HTBlueprintToggleToolModule.h"

#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "SHTBlueprintToggleToolPanel.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FHTBlueprintToggleToolModule"

const FName FHTBlueprintToggleToolModule::ToolTabName(TEXT("HTBlueprintToggleTool"));

namespace
{
	static bool IsDigitsOnly(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return false;
		}

		for (const TCHAR Character : Value)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}
		return true;
	}

	static FString MakeCharacterNameFromFolder(const FString& CharacterFolder)
	{
		FString FolderName = FPackageName::GetLongPackageAssetName(CharacterFolder);
		int32 UnderscoreIndex = INDEX_NONE;
		if (FolderName.FindChar(TEXT('_'), UnderscoreIndex))
		{
			const FString Prefix = FolderName.Left(UnderscoreIndex);
			if (IsDigitsOnly(Prefix) && UnderscoreIndex + 1 < FolderName.Len())
			{
				FolderName = FolderName.Mid(UnderscoreIndex + 1);
			}
		}

		return ObjectTools::SanitizeObjectName(FolderName);
	}

	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FName PinName)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName == PinName)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	static bool IsSaveSlotFunction(const FName FunctionName)
	{
		return FunctionName == TEXT("DoesSaveGameExist")
			|| FunctionName == TEXT("LoadGameFromSlot")
			|| FunctionName == TEXT("SaveGameToSlot");
	}

	static FString MakeMigratedSlotName(FString CurrentSlotName, const FString& CharacterName)
	{
		CurrentSlotName.TrimStartAndEndInline();
		CurrentSlotName.ReplaceInline(TEXT("\""), TEXT(""));

		if (CurrentSlotName.IsEmpty() || CharacterName.IsEmpty())
		{
			return CurrentSlotName;
		}

		const FString ExpectedSuffix = TEXT(" ") + CharacterName;
		if (CurrentSlotName.EndsWith(ExpectedSuffix, ESearchCase::IgnoreCase) || CurrentSlotName.Contains(TEXT(" ")))
		{
			return CurrentSlotName;
		}

		return CurrentSlotName + ExpectedSuffix;
	}

	static bool MigrateBlueprintSlotNames(UBlueprint* Blueprint, const FString& CharacterName, TArray<FString>& OutChanges)
	{
		if (!Blueprint || CharacterName.IsEmpty())
		{
			return false;
		}

		bool bChanged = false;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			const UEdGraphSchema* Schema = Graph->GetSchema();
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (!CallNode || !CallNode->FunctionReference.GetMemberName().IsValid()
					|| !IsSaveSlotFunction(CallNode->FunctionReference.GetMemberName()))
				{
					continue;
				}

				UEdGraphPin* SlotPin = FindPin(CallNode, TEXT("SlotName"));
				if (!SlotPin)
				{
					continue;
				}

				const FString OldSlotName = SlotPin->DefaultValue;
				const FString NewSlotName = MakeMigratedSlotName(OldSlotName, CharacterName);
				if (NewSlotName.IsEmpty() || NewSlotName == OldSlotName)
				{
					continue;
				}

				CallNode->Modify();
				if (Schema)
				{
					Schema->TrySetDefaultValue(*SlotPin, NewSlotName);
				}
				else
				{
					SlotPin->DefaultValue = NewSlotName;
				}
				bChanged = true;
				OutChanges.Add(FString::Printf(
					TEXT("%s :: %s: %s -> %s"),
					*Blueprint->GetPathName(),
					*CallNode->FunctionReference.GetMemberName().ToString(),
					*OldSlotName,
					*NewSlotName));
			}
		}

		return bChanged;
	}
}

void FHTBlueprintToggleToolModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ToolTabName,
		FOnSpawnTab::CreateRaw(this, &FHTBlueprintToggleToolModule::SpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "HT Blueprint Toggle Tool"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	OpenWindowConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HTBlueprintToggleTool.Open"),
		TEXT("Open the HT Blueprint Toggle Tool window."),
		FConsoleCommandDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::OpenPluginWindow));

	OpenMaterialAnalysisConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HTBlueprintToggleTool.OpenMaterialAnalysis"),
		TEXT("Open the HT Blueprint Toggle Tool and run the material slot analysis."),
		FConsoleCommandDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::OpenMaterialAnalysisWindow));

	OpenMaterialSlotMapperConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HTBlueprintToggleTool.OpenMaterialSlotMapper"),
		TEXT("Open the HT Blueprint Toggle Tool and the material slot mapper."),
		FConsoleCommandDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::OpenMaterialSlotMapperWindow));

	MigrateSaveSlotNamesConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HTBlueprintToggleTool.MigrateSaveSlotNames"),
		TEXT("Append the character name to existing SaveGame SlotName pin defaults under /Game/Characters/Player."),
		FConsoleCommandDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::MigrateSaveSlotNames));

	MigrateSaveSlotNamesAndExitConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HTBlueprintToggleTool.MigrateSaveSlotNamesAndExit"),
		TEXT("Append character names to character SaveGame SlotName pins, save assets, and exit the editor."),
		FConsoleCommandDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::MigrateSaveSlotNamesAndExit));

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::RegisterMenus));
}

void FHTBlueprintToggleToolModule::ShutdownModule()
{
	MigrateSaveSlotNamesAndExitConsoleCommand.Reset();
	MigrateSaveSlotNamesConsoleCommand.Reset();
	OpenMaterialSlotMapperConsoleCommand.Reset();
	OpenMaterialAnalysisConsoleCommand.Reset();
	OpenWindowConsoleCommand.Reset();
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ToolTabName);
}

void FHTBlueprintToggleToolModule::OpenPluginWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ToolTabName);
}

void FHTBlueprintToggleToolModule::OpenMaterialAnalysisWindow()
{
	OpenPluginWindow();
	if (TSharedPtr<SHTBlueprintToggleToolPanel> Panel = ActivePanel.Pin())
	{
		Panel->OpenMaterialAnalysisFromCommand();
	}
}

void FHTBlueprintToggleToolModule::OpenMaterialSlotMapperWindow()
{
	OpenPluginWindow();
	if (TSharedPtr<SHTBlueprintToggleToolPanel> Panel = ActivePanel.Pin())
	{
		Panel->OpenMaterialSlotMapperFromCommand();
	}
}

void FHTBlueprintToggleToolModule::MigrateSaveSlotNames()
{
	UE_LOG(LogTemp, Display, TEXT("HTBlueprintToggleTool.MigrateSaveSlotNames started."));

	static const FString PlayerRootPath = TEXT("/Game/Characters/Player");
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FString> CharacterFolders;
	AssetRegistry.GetSubPaths(PlayerRootPath, CharacterFolders, false);

	TArray<UPackage*> PackagesToSave;
	TArray<FString> Changes;
	for (const FString& CharacterFolder : CharacterFolders)
	{
		const FString CharacterName = MakeCharacterNameFromFolder(CharacterFolder);
		if (CharacterName.IsEmpty())
		{
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("Scanning %s for AnimBP SlotName migration (%s)."), *CharacterFolder, *CharacterName);
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*CharacterFolder), Assets, true);
		for (const FAssetData& AssetData : Assets)
		{
			if (AssetData.AssetClassPath != UAnimBlueprint::StaticClass()->GetClassPathName())
			{
				continue;
			}

			UE_LOG(LogTemp, Display, TEXT("Migrating SaveGame SlotName pins for %s (%s)."), *AssetData.GetObjectPathString(), *CharacterName);
			UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
			if (!AnimBlueprint)
			{
				UE_LOG(LogTemp, Warning, TEXT("Could not load Anim Blueprint: %s"), *AssetData.GetObjectPathString());
				continue;
			}

			TArray<FString> BlueprintChanges;
			if (!MigrateBlueprintSlotNames(AnimBlueprint, CharacterName, BlueprintChanges))
			{
				UE_LOG(LogTemp, Display, TEXT("No SlotName changes needed for %s."), *AnimBlueprint->GetPathName());
				continue;
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
			UE_LOG(LogTemp, Display, TEXT("Compiling %s after SlotName migration."), *AnimBlueprint->GetPathName());
			FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
			PackagesToSave.AddUnique(AnimBlueprint->GetOutermost());
			Changes.Append(BlueprintChanges);
		}
	}

	if (PackagesToSave.Num() > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("Saving %d migrated AnimBP package(s)."), PackagesToSave.Num());
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}

	UE_LOG(LogTemp, Display, TEXT("HTBlueprintToggleTool.MigrateSaveSlotNames changed %d SlotName pins in %d packages."), Changes.Num(), PackagesToSave.Num());
	for (const FString& Change : Changes)
	{
		UE_LOG(LogTemp, Display, TEXT("%s"), *Change);
	}
}

void FHTBlueprintToggleToolModule::MigrateSaveSlotNamesAndExit()
{
	MigrateSaveSlotNames();
	FPlatformMisc::RequestExit(false);
}

TSharedRef<SDockTab> FHTBlueprintToggleToolModule::SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedPtr<SHTBlueprintToggleToolPanel> Panel;
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SAssignNew(Panel, SHTBlueprintToggleToolPanel)
		];
	ActivePanel = Panel;
	return Tab;
}

void FHTBlueprintToggleToolModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("HTTools");
	Section.AddMenuEntry(
		"OpenHTBlueprintToggleTool",
		LOCTEXT("MenuEntryTitle", "HT Blueprint Toggle Tool"),
		LOCTEXT("MenuEntryTooltip", "Open the HT material and texture toggle blueprint generator."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FHTBlueprintToggleToolModule::OpenPluginWindow)));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHTBlueprintToggleToolModule, HTBlueprintToggleTool)
