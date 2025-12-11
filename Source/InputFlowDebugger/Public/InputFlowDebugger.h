// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <Modules/ModuleManager.h>

// Slate
#include <Widgets/Docking/SDockTab.h>

// Editor
#if WITH_EDITOR
#include <Framework/Docking/TabManager.h>
#include <WorkspaceMenuStructure.h>
#include <WorkspaceMenuStructureModule.h>
#endif

// Internal
#if WITH_EDITOR
#include "SInputFlowAnalyzer.h"
#endif

#define LOCTEXT_NAMESPACE "FInputFlowDebuggerModule"

#define LOCTEXT_NAMESPACE "FInputFlowDebuggerModule"

static const FName InputFlowTabName("InputFlowDebugger");

class FInputFlowDebuggerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(InputFlowTabName, FOnSpawnTab::CreateRaw(this, &FInputFlowDebuggerModule::OnSpawnPluginTab))
			.SetDisplayName(LOCTEXT("InputFlowTabTitle", "Input Flow Debugger"))
			.SetMenuType(ETabSpawnerMenuType::Enabled)
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(InputFlowTabName);
#endif
	}

private:
#if WITH_EDITOR
	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SInputFlowAnalyzer)
			];
	}
#endif
};

#undef LOCTEXT_NAMESPACE