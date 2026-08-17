#include "SimRTSEditor.h"

#include "SObstructionBakeWindow.h"

#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "SimRTSEditor"

const FName FSimRTSEditorModule::ObstructionBakeTabName(TEXT("SimRTSObstructionBake"));

void FSimRTSEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ObstructionBakeTabName,
		FOnSpawnTab::CreateRaw(this, &FSimRTSEditorModule::SpawnObstructionBakeTab))
		.SetDisplayName(LOCTEXT("ObstructionBakeTabTitle", "Level Bake"))
		.SetTooltipText(LOCTEXT("ObstructionBakeTabTooltip", "Bake obstruction volumes and spawn markers into level JSON"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetMenuType(ETabSpawnerMenuType::Enabled);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSimRTSEditorModule::RegisterMenus));
}

void FSimRTSEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ObstructionBakeTabName);
}

void FSimRTSEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("SimRTS");
	Section.AddMenuEntry(
		"SimRTSObstructionBake",
		LOCTEXT("OpenObstructionBake", "SimRTS Level Bake..."),
		LOCTEXT("OpenObstructionBakeTooltip", "Open the obstruction + spawn baker"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(ObstructionBakeTabName);
		})));
}

TSharedRef<SDockTab> FSimRTSEditorModule::SpawnObstructionBakeTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SObstructionBakeWindow)
		];
}

IMPLEMENT_MODULE(FSimRTSEditorModule, SimRTSEditor)

#undef LOCTEXT_NAMESPACE
