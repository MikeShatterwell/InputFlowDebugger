// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowDebugger.h"

// Core
#include <Modules/ModuleManager.h>

// Editor
#if WITH_EDITOR
#include <Framework/Docking/TabManager.h>
#include <Widgets/Docking/SDockTab.h>
#include <WorkspaceMenuStructure.h>
#include <WorkspaceMenuStructureModule.h>
#include "SInputFlowAnalyzer.h"
#endif

static_assert(!UE_SERVER, "InputFlowDebugger is not allowed in Server builds.");

IMPLEMENT_MODULE(FInputFlowDebuggerModule, InputFlowDebugger);

void FInputFlowDebuggerModule::StartupModule()
{
#if WITH_EDITOR
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(InputFlowTabName, FOnSpawnTab::CreateRaw(this, &FInputFlowDebuggerModule::OnSpawnPluginTab))
		.SetDisplayName(INVTEXT("Input Flow Debugger"))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Layout"));
#endif
}

void FInputFlowDebuggerModule::ShutdownModule()
{
#if WITH_EDITOR
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(InputFlowTabName);
#endif
}

#if WITH_EDITOR
TSharedRef<SDockTab> FInputFlowDebuggerModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SInputFlowAnalyzer)
		];
}
#endif

void FInputFlowDebuggerModule::RegisterExternalOverlayPanel(const FInputFlowExternalPanelDef& InDef)
{
	// Prevent duplicate registrations by removing existing ones with the same ID
	UnregisterExternalOverlayPanel(InDef.PanelId);
	ExternalPanels.Add(InDef);
}

void FInputFlowDebuggerModule::UnregisterExternalOverlayPanel(FName PanelId)
{
	ExternalPanels.RemoveAll([PanelId](const FInputFlowExternalPanelDef& Def) {
		return Def.PanelId == PanelId;
	});
}