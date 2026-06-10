#pragma once

#include "Modules/ModuleManager.h"

class FHTBlueprintToggleToolModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void OpenPluginWindow();

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	static const FName ToolTabName;
};
