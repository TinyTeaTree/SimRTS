#pragma once

#include "Modules/ModuleManager.h"

class FSimRTSEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnObstructionBakeTab(const class FSpawnTabArgs& Args);

	static const FName ObstructionBakeTabName;
};
