// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <Modules/ModuleManager.h>
#include <Misc/Attribute.h>

// Slate
#include <Widgets/SWidget.h>

class UInputDebugSubsystem;
class FMenuBuilder;
class FSpawnTabArgs;

// Delegate used by external code to create their custom panel contents
DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnCreateInputFlowPanel, UInputDebugSubsystem* /*Subsystem*/);
// Delegate used by external code to add custom entries to the SInputFlowSettingsPanel menu panel
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBuildExternalSettings, FMenuBuilder& /*MenuBuilder*/);


/*
 * A struct defining the properties and content of an external panel to be registered with the Input Flow Debugger's overlay.
 * External plugins can register panels via FInputFlowDebuggerModule::RegisterExternalOverlayPanel
 */
struct FInputFlowExternalPanelDef
{
	FName PanelId = NAME_None;
	FString Title = TEXT("Custom Panel");
	FVector2D InitialPosition = FVector2D(100, 100);
	FVector2D InitialSize = FVector2D(400, 300);
	bool bCanClose = true;
	bool bCanMinimize = true;
	
	// Delegate to create the inner widget content
	// Return the Slate widget to embed inside the panel, receiving a pointer to the UInputDebugSubsystem for data access and callbacks
	FOnCreateInputFlowPanel CreatePanelDelegate;
	
	// Delegate to handle the user clicking the "Close" button
	FSimpleDelegate OnCloseDelegate;

	// Controls the visibility of the panel, can be bound to external settings/cvars
	TAttribute<EVisibility> VisibilityAttribute = EVisibility::SelfHitTestInvisible;
};

static const FName InputFlowTabName("InputFlowDebugger");

/**
 * Module interface for the Input Flow Debugger.
 *
 * External plugins can hook into the debugger in three ways:
 *
 * 1) OVERLAY PANELS — Add a draggable panel to the in-game overlay. Note that it is recommended to add a toggle
 *   for the panel to the EXTENSIONS MENU (below) and bind its visibility to the same setting, so that users can easily
 *   discover and control it.
 *
 *    // In your module's StartupModule():
 *    if (FInputFlowDebuggerModule::IsAvailable())
 *    {
 *        FInputFlowExternalPanelDef Def;
 *        Def.PanelId    = "MyPluginPanel";
 *        Def.Title      = TEXT("My Debug Panel");
 *
 *        Def.VisibilityAttribute = TAttribute<EVisibility>::CreateLambda([]() {
 *            return bMyPanelVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
 *        });
 *
 *        Def.OnCloseDelegate.BindLambda([]() { bMyPanelVisible = false; });
 *
 *        Def.CreatePanelDelegate.BindLambda([](UInputDebugSubsystem* Sub) -> TSharedRef<SWidget> {
 *            return SNew(SMyDebugWidget, Sub);
 *        });
 *
 *        FInputFlowDebuggerModule::Get().RegisterExternalOverlayPanel(Def);
 *    }
 *
 *    // In your module's ShutdownModule():
 *    if (FInputFlowDebuggerModule::IsAvailable())
 *    {
 *        FInputFlowDebuggerModule::Get().UnregisterExternalOverlayPanel("MyPluginPanel");
 *    }
 *
 * 2) EXTENSIONS MENU — Add a toggle to the Settings toolbar's "Extensions" dropdown.
 *
 *    FInputFlowDebuggerModule::Get().GetBuildExternalSettingsDelegate().AddLambda([](FMenuBuilder& MenuBuilder)
 *    {
 *        MenuBuilder.AddMenuEntry(
 *            INVTEXT("My Tool"),
 *            INVTEXT("Toggles my custom debug visualization"),
 *            FSlateIcon(),
 *            FUIAction(
 *                FExecuteAction::CreateLambda([]() { bMyPanelVisible = !bMyPanelVisible; }),
 *                FCanExecuteAction(),
 *                FIsActionChecked::CreateLambda([]() { return bMyPanelVisible; })
 *            ),
 *            NAME_None,
 *            EUserInterfaceActionType::ToggleButton
 *        );
 *    });
 *
 * 3) DRAWING & LABELS — Draw directly to the overlay via the subsystem's delegates.
 *    The subsystem broadcasts these every frame while the overlay is active.
 *
 *    // Subscribe (e.g. in your panel's Construct, or wherever you have the subsystem):
 *    Subsystem->GetOnDrawOverlay().AddLambda([](UInputDebugSubsystem*, FInputFlowDrawAPI& DrawAPI)
 *    {
 *        DrawAPI.DrawWidgetHighlight(SomeWidget, FLinearColor::Yellow, 2.0f);
 *        DrawAPI.DrawLine(StartAbs, EndAbs, FLinearColor::Red);
 *    });
 *
 *    Subsystem->GetOnGatherLabels().AddLambda([](UInputDebugSubsystem*, FInputFlowLabelAPI& LabelAPI)
 *    {
 *        LabelAPI.QueueWidgetLabel(SomeWidget, TEXT("My Label"), FLinearColor::White);
 *    });
 *
 * 4) INPUT SUPPRESSION — React when the overlay starts/stops capturing input.
 *    Game code subscribes and disables its own input handling for the duration.
 *
 *    // In your PlayerController or input-handling class:
 *    Subsystem->GetOnCaptureChanged().AddLambda([this](bool bIsCapturing)
 *    {
 *        // e.g. toggle an Enhanced Input mapping context, disable a custom input mode, etc.
 *        SetIgnoreMoveInput(bIsCapturing);
 *        SetIgnoreLookInput(bIsCapturing);
 *    });
 *
 *    // Late joiners can also check:
 *    if (Subsystem->IsCapturingInput()) { /... suppress .../ }
 *
 * All four mechanisms are optional and independent.
 */
class INPUTFLOWDEBUGGER_API FInputFlowDebuggerModule : public IModuleInterface
{
	// Internal widgets need direct access to the panel list for construction and visibility checks.
	// The public API is intentionally limited to register/unregister.
	friend class SInputFlowOverlay;
	friend class SInputFlowSettingsPanel;
	
public:
	static FInputFlowDebuggerModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FInputFlowDebuggerModule>("InputFlowDebugger");
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("InputFlowDebugger");
	}

	// Begin IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface implementation

	// --- External Hooks API ---
	void RegisterExternalOverlayPanel(const FInputFlowExternalPanelDef& InDef);
	void UnregisterExternalOverlayPanel(FName PanelId);

	FOnBuildExternalSettings& GetBuildExternalSettingsDelegate() { return BuildExternalSettingsDelegate; }

private:
	const TArray<FInputFlowExternalPanelDef>& GetExternalPanels() const { return ExternalPanels; }
	TArray<FInputFlowExternalPanelDef> ExternalPanels;
	FOnBuildExternalSettings BuildExternalSettingsDelegate;

#if WITH_EDITOR
	TSharedRef<class SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);
#endif
};