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
	TSharedRef<class SDockTab> SpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	static const FName ToolTabName;
	TWeakPtr<SHTBlueprintToggleToolPanel> ActivePanel;
	TUniquePtr<FAutoConsoleCommand> OpenWindowConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> OpenMaterialAnalysisConsoleCommand;
};
