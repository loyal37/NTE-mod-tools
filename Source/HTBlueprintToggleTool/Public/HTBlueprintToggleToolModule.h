#pragma once

#include "Modules/ModuleManager.h"

class FAutoConsoleCommand;
class SHTBlueprintToggleToolPanel;

class FHTBlueprintToggleToolModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void OpenPluginWindow();

private:
	void RegisterMenus();
	void OpenMaterialAnalysisWindow();
	void OpenMaterialSlotMapperWindow();
	void MigrateSaveSlotNames();
	void MigrateSaveSlotNamesAndExit();
	TSharedRef<class SDockTab> SpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	static const FName ToolTabName;
	TWeakPtr<SHTBlueprintToggleToolPanel> ActivePanel;
	TUniquePtr<FAutoConsoleCommand> OpenWindowConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> OpenMaterialAnalysisConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> OpenMaterialSlotMapperConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> MigrateSaveSlotNamesConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> MigrateSaveSlotNamesAndExitConsoleCommand;
};
