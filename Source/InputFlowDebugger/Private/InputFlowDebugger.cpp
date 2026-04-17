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

// UMGWidgetPreview
#include <IUMGWidgetPreviewModule.h>
#include <IWidgetPreviewToolkit.h>

// Internal
#include "SInputFlowAnalyzer.h"
#include "SMVVMInspectorPanel.h"
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

	// Register the MVVM Inspector into the Widget Preview toolkit
	if (FModuleManager::Get().IsModuleLoaded("UMGWidgetPreview"))
	{
		IUMGWidgetPreviewModule& PreviewModule = FModuleManager::LoadModuleChecked<IUMGWidgetPreviewModule>("UMGWidgetPreview");

		PreviewTabsDelegateHandle = PreviewModule.OnRegisterTabsForEditor().AddLambda([](const TSharedPtr<UE::UMGWidgetPreview::IWidgetPreviewToolkit>& Toolkit, const TSharedRef<FTabManager>& TabManager)
		{
			const FName MVVMInspectorTabId("WidgetPreview_MVVMInspector");

			// Capture as TWeakPtr to avoid a circular reference:
			// Toolkit → TabManager → TabSpawner lambda → Toolkit
			// A strong capture here would prevent the toolkit from ever being destroyed.
			TWeakPtr<UE::UMGWidgetPreview::IWidgetPreviewToolkit> WeakToolkit = Toolkit;

			// Use the toolkit's own workspace menu category. Without SetGroup(), Unreal's
			// menu population auto-adds the tab to multiple sections (workspace root +
			// an ungrouped fallback), producing duplicate "top and bottom" entries in the
			// Window dropdown. Explicitly grouping it restricts placement to one section.
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
			// Users can still redock it elsewhere — layout changes are persisted per-asset.
			if (TSharedPtr<FLayoutExtender> LayoutExtender = Toolkit->GetLayoutExtender())
			{
				FTabManager::FTab MVVMTab(FTabId(MVVMInspectorTabId, ETabIdFlags::SaveLayout), ETabState::OpenedTab);
				LayoutExtender->ExtendLayout(FTabId(TEXT("WidgetPreviewToolkit_PreviewScene")), ELayoutExtensionPosition::After, MVVMTab);
			}
		});
	}
#endif
}

void FInputFlowDebuggerModule::ShutdownModule()
{
#if WITH_EDITOR
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(InputFlowTabName);

	if (PreviewTabsDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("UMGWidgetPreview"))
	{
		IUMGWidgetPreviewModule& PreviewModule = FModuleManager::LoadModuleChecked<IUMGWidgetPreviewModule>("UMGWidgetPreview");
		PreviewModule.OnRegisterTabsForEditor().Remove(PreviewTabsDelegateHandle);
		PreviewTabsDelegateHandle.Reset();
	}
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