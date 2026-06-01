// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowDebugger.h"

// Core
#include <Modules/ModuleManager.h>

// Editor
#if WITH_EDITOR
#include <WorkspaceMenuStructure.h>
#include <WorkspaceMenuStructureModule.h>

// Slate
#include <Framework/Docking/LayoutExtender.h>
#include <Framework/Docking/TabManager.h>
#include <Widgets/Docking/SDockTab.h>

#if WITH_PLUGIN_UMGWIDGETPREVIEW
// UMGWidgetPreview
#include <IUMGWidgetPreviewModule.h>
#include <IWidgetPreviewToolkit.h>
#endif // WITH_PLUGIN_UMGWIDGETPREVIEW

// Internal
#include "SInputFlowAnalyzer.h"
#include "SMVVMInspectorPanel.h"
#endif // WITH_EDITOR

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

#if WITH_PLUGIN_UMGWIDGETPREVIEW && WITH_PLUGIN_MODELVIEWVIEWMODEL
	// Register the MVVM Inspector into the Widget Preview toolkit
	if (FModuleManager::Get().IsModuleLoaded("UMGWidgetPreview"))
	{
		IUMGWidgetPreviewModule& PreviewModule = FModuleManager::LoadModuleChecked<IUMGWidgetPreviewModule>("UMGWidgetPreview");

		PreviewTabsDelegateHandle = PreviewModule.OnRegisterTabsForEditor().AddLambda([](const TSharedPtr<UE::UMGWidgetPreview::IWidgetPreviewToolkit>& Toolkit, const TSharedRef<FTabManager>& TabManager)
		{
			const FName MVVMInspectorTabId("WidgetPreview_MVVMInspector");
			
			TWeakPtr<UE::UMGWidgetPreview::IWidgetPreviewToolkit> WeakToolkit = Toolkit;
			TSharedRef<FWorkspaceItem> WorkspaceGroup = TabManager->GetLocalWorkspaceMenuRoot();

			TabManager->RegisterTabSpawner(
				MVVMInspectorTabId,
				FOnSpawnTab::CreateLambda([WeakToolkit](const FSpawnTabArgs& Args)
				{
					return SNew(SDockTab)
					.Label(INVTEXT("MVVM Inspector"))
					[
						SNew(SMVVMInspectorPanel).PreviewToolkit(WeakToolkit)
					];
				}))
				.SetDisplayName(INVTEXT("MVVM Inspector"))
				.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"))
				.SetGroup(WorkspaceGroup);

			// Dock the tab next to the existing Preview Scene Settings tab on first open.
			// Users can still redock it elsewhere, layout changes are persisted per-asset.
			if (TSharedPtr<FLayoutExtender> LayoutExtender = Toolkit->GetLayoutExtender())
			{
				FTabManager::FTab MVVMTab(FTabId(MVVMInspectorTabId, ETabIdFlags::SaveLayout), ETabState::OpenedTab);
				LayoutExtender->ExtendLayout(FTabId(TEXT("WidgetPreviewToolkit_PreviewScene")), ELayoutExtensionPosition::After, MVVMTab);
			}
		});
	}
#endif // WITH_PLUGIN_UMGWIDGETPREVIEW
#endif // WITH_EDITOR
}

void FInputFlowDebuggerModule::ShutdownModule()
{
#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(InputFlowTabName);

	if (PreviewTabsDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("UMGWidgetPreview"))
	{
		IUMGWidgetPreviewModule& PreviewModule = FModuleManager::LoadModuleChecked<IUMGWidgetPreviewModule>("UMGWidgetPreview");
		PreviewModule.OnRegisterTabsForEditor().Remove(PreviewTabsDelegateHandle);
		PreviewTabsDelegateHandle.Reset();
	}
#endif // WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
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