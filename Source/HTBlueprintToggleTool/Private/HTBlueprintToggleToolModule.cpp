#include "HTBlueprintToggleToolModule.h"

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

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHTBlueprintToggleToolModule::RegisterMenus));
}

void FHTBlueprintToggleToolModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ToolTabName);
}

void FHTBlueprintToggleToolModule::OpenPluginWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ToolTabName);
}

TSharedRef<SDockTab> FHTBlueprintToggleToolModule::SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SHTBlueprintToggleToolPanel)
		];
}

void FHTBlueprintToggleToolModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("HTTools");
	Section.AddMenuEntry(
		"OpenHTBlueprintToggleTool",
		LOCTEXT("MenuEntryTitle", "HT Blueprint Toggle Tool"),
		LOCTEXT("MenuEntryTooltip", "Open the HT material toggle blueprint generator."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FHTBlueprintToggleToolModule::OpenPluginWindow)));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHTBlueprintToggleToolModule, HTBlueprintToggleTool)
