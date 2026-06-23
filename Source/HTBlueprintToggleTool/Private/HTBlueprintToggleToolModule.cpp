#include "HTBlueprintToggleToolModule.h"

#include "HAL/IConsoleManager.h"
#include "SHTBlueprintToggleToolPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FHTBlueprintToggleToolModule"

const FName FHTBlueprintToggleToolModule::ToolTabName(TEXT("HTBlueprintToggleTool"));

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

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::RegisterMenus));
}

void FHTBlueprintToggleToolModule::ShutdownModule()
{
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
