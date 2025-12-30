// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputFlowSettings.generated.h"

/**
 * Settings class for the Input Flow Debugger tool.
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "Input Flow Debugger"))
class INPUTFLOWDEBUGGER_API UInputFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UInputFlowSettings();

	static const UInputFlowSettings* Get();

	// --- Setters and Getters ---

	// Delegate type for settings changes (public so callers can bind)
	DECLARE_MULTICAST_DELEGATE(FOnInputFlowSettingsChanged);

	// Capture Settings
	bool IsCaptureClicksEnabled() const { return bCaptureClicks; }
	void SetCaptureClicks(bool bEnabled) { if (bCaptureClicks != bEnabled) { bCaptureClicks = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsCaptureKeyEventsEnabled() const { return bCaptureKeyEvents; }
	void SetCaptureKeyEvents(bool bEnabled) { if (bCaptureKeyEvents != bEnabled) { bCaptureKeyEvents = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsCaptureHoverEnabled() const { return bCaptureHover; }
	void SetCaptureHover(bool bEnabled) { if (bCaptureHover != bEnabled) { bCaptureHover = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsCaptureMouseMoveEnabled() const { return bCaptureMouseMove; }
	void SetCaptureMouseMove(bool bEnabled) { if (bCaptureMouseMove != bEnabled) { bCaptureMouseMove = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsCaptureAnalogEnabled() const { return bCaptureAnalog; }
	void SetCaptureAnalog(bool bEnabled) { if (bCaptureAnalog != bEnabled) { bCaptureAnalog = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsCaptureFocusEnabled() const { return bCaptureFocus; }
	void SetCaptureFocus(bool bEnabled) { if (bCaptureFocus != bEnabled) { bCaptureFocus = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool GetShowHandledEventsEnabled() const { return bShowHandledEvents; }
	void SetShowHandledEvents(bool bEnabled) { if (bShowHandledEvents != bEnabled) { bShowHandledEvents = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Visualization Settings
	bool IsOverlayEnabled() const { return bEnableOverlay; }
	void SetEnableOverlay(bool bEnabled) { if (bEnableOverlay != bEnabled) { bEnableOverlay = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }
	
	bool IsFocusHighlightEnabled() const { return bShowFocusHighlight; }
	void SetShowFocusHighlight(bool bEnabled) { if (bShowFocusHighlight != bEnabled) { bShowFocusHighlight = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsHitTestGridShown() const { return bShowHitTestGrid; }
	void SetShowHitTestGrid(bool bEnabled) { if (bShowHitTestGrid != bEnabled) { bShowHitTestGrid = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsLogPanelShown() const { return bShowLogPanel; }
	void SetShowLogPanel(bool bEnabled) { if (bShowLogPanel != bEnabled) { bShowLogPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsDashboardPanelShown() const { return bShowDashboardPanel; }
	void SetShowDashboardPanel(bool bEnabled) { if (bShowDashboardPanel != bEnabled) { bShowDashboardPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsHierarchyPanelShown() const { return bShowHierarchyPanel; }
	void SetShowHierarchyPanel(bool bEnabled) { if (bShowHierarchyPanel != bEnabled) { bShowHierarchyPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsEnhancedInputPanelShown() const { return bShowEnhancedInputPanel; }
	void SetShowEnhancedInputPanel(bool bEnabled) { if (bShowEnhancedInputPanel != bEnabled) { bShowEnhancedInputPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Simulation Settings
	bool IsNavSimulationEnabled() const { return bEnableNavSimulation; }
	void SetEnableNavSimulation(bool bEnabled) { if (bEnableNavSimulation != bEnabled) { bEnableNavSimulation = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	int32 GetNavigationSearchDepth() const { return NavigationSearchDepth; }
	void SetNavigationSearchDepth(int32 NewDepth) { NewDepth = FMath::Clamp(NewDepth, 1, 5); if (NavigationSearchDepth != NewDepth) { NavigationSearchDepth = NewDepth; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Accessor for the settings changed delegate
	FOnInputFlowSettingsChanged& GetOnSettingsChanged() { return OnSettingsChanged; }

private:
	// --- Capture Settings (capture in this context means to log the event, which passes through to the game as normal) ---
	
	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bCaptureClicks = false;

	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bCaptureKeyEvents = false;

	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bCaptureHover = false;

	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bCaptureMouseMove = false;

	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bCaptureAnalog = false;

	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bCaptureFocus = true;

	UPROPERTY(EditAnywhere, Config, Category = "Capture Filters")
	bool bShowHandledEvents = true;

	// --- Visualization Settings ---

	UPROPERTY(EditAnywhere, Config, Category = "Visualization")
	bool bEnableOverlay = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization")
	bool bShowFocusHighlight = true;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization")
	bool bShowHitTestGrid = false;

	UPROPERTY(EditAnywhere, Config, Category = "Panels")
	bool bShowLogPanel = false;

	UPROPERTY(EditAnywhere, Config, Category = "Panels")
	bool bShowDashboardPanel = false;

	UPROPERTY(EditAnywhere, Config, Category = "Panels")
	bool bShowHierarchyPanel = false;

	UPROPERTY(EditAnywhere, Config, Category = "Panels")
	bool bShowEnhancedInputPanel = false;

	// --- Simulation Settings ---

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation")
	bool bEnableNavSimulation = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation", meta=(ClampMin=1, ClampMax=5))
	int32 NavigationSearchDepth = 1;
	
	FOnInputFlowSettingsChanged OnSettingsChanged;

	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
};