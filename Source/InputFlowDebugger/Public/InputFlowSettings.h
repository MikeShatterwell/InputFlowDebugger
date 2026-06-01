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

	float GetOverlayScale() const { return OverlayScale; }
	void SetOverlayScale(float NewScale) { NewScale = FMath::Clamp(NewScale, 0.5f, 3.0f); if (!FMath::IsNearlyEqual(OverlayScale, NewScale)) { OverlayScale = NewScale; SaveConfig(); OnSettingsChanged.Broadcast(); } }
	
	bool IsFocusHighlightEnabled() const { return bShowFocusHighlight; }
	void SetShowFocusHighlight(bool bEnabled) { if (bShowFocusHighlight != bEnabled) { bShowFocusHighlight = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsHitTestGridShown() const { return bShowHitTestGrid; }
	void SetShowHitTestGrid(bool bEnabled) { if (bShowHitTestGrid != bEnabled) { bShowHitTestGrid = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsLogPanelShown() const { return bShowLogPanel; }
	void SetShowLogPanel(bool bEnabled) { if (bShowLogPanel != bEnabled) { bShowLogPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }
	
	bool IsDashboardPanelShown() const { return bShowDashboardPanel; }
	void SetShowDashboardPanel(bool bEnabled) { if (bShowDashboardPanel != bEnabled) { bShowDashboardPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// CommonUI
	bool IsHierarchyPanelShown() const { return bShowHierarchyPanel; }
	void SetShowHierarchyPanel(bool bEnabled) { if (bShowHierarchyPanel != bEnabled) { bShowHierarchyPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// EnhancedInput
	bool IsEnhancedInputPanelShown() const { return bShowEnhancedInputPanel; }
	void SetShowEnhancedInputPanel(bool bEnabled) { if (bShowEnhancedInputPanel != bEnabled) { bShowEnhancedInputPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// MVVM
	bool IsMVVMInspectorPanelShown() const { return bShowMVVMInspectorPanel; }
	void SetShowMVVMInspectorPanel(bool bEnabled) { if (bShowMVVMInspectorPanel != bEnabled) { bShowMVVMInspectorPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Localization overlay toggle
	bool IsLocalizationPanelShown() const { return bShowLocalizationPanel; }
	void SetShowLocalizationPanel(bool bEnabled) { if (bShowLocalizationPanel != bEnabled) { bShowLocalizationPanel = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsLocLabelsEnabled() const { return bShowLocLabels; }
	void SetLocLabelsEnabled(bool bEnabled) { if (bShowLocLabels != bEnabled) { bShowLocLabels = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Loc X-Ray per-status filters. Default to surfacing only problems (Hardcoded).
	bool IsLocShowLocalized() const { return bLocShowLocalized; }
	void SetLocShowLocalized(bool bEnabled) { if (bLocShowLocalized != bEnabled) { bLocShowLocalized = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsLocShowHardcoded() const { return bLocShowHardcoded; }
	void SetLocShowHardcoded(bool bEnabled) { if (bLocShowHardcoded != bEnabled) { bLocShowHardcoded = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsLocShowInvariant() const { return bLocShowInvariant; }
	void SetLocShowInvariant(bool bEnabled) { if (bLocShowInvariant != bEnabled) { bLocShowInvariant = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	bool IsLocShowWidgetName() const { return bLocShowWidgetName; }
	void SetLocShowWidgetName(bool bEnabled) { if (bLocShowWidgetName != bEnabled) { bLocShowWidgetName = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Simulation Settings
	bool IsNavSimulationEnabled() const { return bEnableNavSimulation; }
	void SetEnableNavSimulation(bool bEnabled) { if (bEnableNavSimulation != bEnabled) { bEnableNavSimulation = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	int32 GetNavigationSearchDepth() const { return NavigationSearchDepth; }
	void SetNavigationSearchDepth(int32 NewDepth) { NewDepth = FMath::Clamp(NewDepth, 1, 5); if (NavigationSearchDepth != NewDepth) { NavigationSearchDepth = NewDepth; SaveConfig(); OnSettingsChanged.Broadcast(); } }
	
	float GetNavigationSimPollInterval() const { return NavigationSimPollInterval; }

	void SetNavigationSimPollInterval(float NewInterval) 
	{ 
		NewInterval = FMath::Clamp(NewInterval, 0.05f, 5.0f); 
		if (!FMath::IsNearlyEqual(NavigationSimPollInterval, NewInterval)) 
		{ 
			NavigationSimPollInterval = NewInterval; 
			SaveConfig(); 
			OnSettingsChanged.Broadcast(); 
		} 
	}

	bool IsNavLabelsEnabled() const { return bShowNavLabels; }
	void SetShowNavLabels(bool bEnabled) { if (bShowNavLabels != bEnabled) { bShowNavLabels = bEnabled; SaveConfig(); OnSettingsChanged.Broadcast(); } }

	// Rejection Filter Getters/Setters
	bool IsNavFilterUserIndexEnabled() const { return bShowRejection_UserIndex; }
	void SetNavFilterUserIndex(bool bShow) { bShowRejection_UserIndex = bShow; SaveConfig(); OnSettingsChanged.Broadcast(); }

	bool IsNavFilterIntersectionEnabled() const { return bShowRejection_Intersection; }
	void SetNavFilterIntersection(bool bShow) { bShowRejection_Intersection = bShow; SaveConfig(); OnSettingsChanged.Broadcast(); }

	bool IsNavFilterDistanceEnabled() const { return bShowRejection_Distance; }
	void SetNavFilterDistance(bool bShow) { bShowRejection_Distance = bShow; SaveConfig(); OnSettingsChanged.Broadcast(); }

	bool IsNavFilterDescendantEnabled() const { return bShowRejection_Descendant; }
	void SetNavFilterDescendant(bool bShow) { bShowRejection_Descendant = bShow; SaveConfig(); OnSettingsChanged.Broadcast(); }

	bool IsNavFilterDisabledEnabled() const { return bShowRejection_Disabled; }
	void SetNavFilterDisabled(bool bShow) { bShowRejection_Disabled = bShow; SaveConfig(); OnSettingsChanged.Broadcast(); }

	bool IsNavFilterFocusEnabled() const { return bShowRejection_Focus; }
	void SetNavFilterFocus(bool bShow) { bShowRejection_Focus = bShow; SaveConfig(); OnSettingsChanged.Broadcast(); }

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
	bool bShowHandledEvents = false;

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

	UPROPERTY(EditAnywhere, Config, Category = "Panels")
	bool bShowMVVMInspectorPanel = false;

	UPROPERTY(EditAnywhere, Config, Category = "Panels")
	bool bShowLocalizationPanel = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization|Localization")
	bool bLocXRayOverflow = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization|Localization")
	bool bShowLocLabels = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization|Localization")
	bool bLocShowLocalized = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization|Localization")
	bool bLocShowHardcoded = true;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization|Localization")
	bool bLocShowInvariant = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization|Localization")
	bool bLocShowWidgetName = false;

	UPROPERTY(EditAnywhere, Config, Category = "Visualization", meta=(ClampMin=0.5, ClampMax=3.0))
	float OverlayScale = 1.0f;

	// --- Simulation Settings ---

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation")
	bool bEnableNavSimulation = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation", meta=(ClampMin=1, ClampMax=5))
	int32 NavigationSearchDepth = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation", meta=(ClampMin=0.05, ClampMax=5.0))
	float NavigationSimPollInterval = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation")
	bool bShowNavLabels = true;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation|Filters")
	bool bShowRejection_UserIndex = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation|Filters")
	bool bShowRejection_Intersection = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation|Filters")
	bool bShowRejection_Distance = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation|Filters")
	bool bShowRejection_Descendant = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation|Filters")
	bool bShowRejection_Disabled = false;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation Simulation|Filters")
	bool bShowRejection_Focus = false;
	
	FOnInputFlowSettingsChanged OnSettingsChanged;

	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
};