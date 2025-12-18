// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>

class UInputDebugSubsystem;
class SInputFlowLogView;
class SCommonUIHierarchyView;
class SEnhancedInputInspector;
class STextBlock;

/**
 * A reusable dashboard widget that displays current focus, input config, and bound actions.
 * Used in both the Editor Window and the In-Game Overlay.
 */
class SInputFlowStatusDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowStatusDashboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FText GetFocusWidgetName() const;
	FText GetCommonInputType(UInputDebugSubsystem* Subsystem) const;
	FText GetActiveBoundActions(const UInputDebugSubsystem* Subsystem) const;

	TSharedPtr<STextBlock> MouseCaptureLabel;
	TSharedPtr<STextBlock> CommonInputTypeLabel;
	TSharedPtr<STextBlock> SlateFocusLabel;
	TSharedPtr<STextBlock> ActionRouterLeafLabel;
	TSharedPtr<STextBlock> InputConfigLabel;
	TSharedPtr<STextBlock> BoundActionsLabel;

	TWeakObjectPtr<UInputDebugSubsystem> WeakSubsystem;
};

/**
 * A reusable settings panel containing toggles for logging, visualization, and simulation.
 * Used in both the Editor Window and the In-Game Overlay.
 */
class SInputFlowSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowSettingsPanel) {}
		SLATE_ARGUMENT(bool, IsOverlay)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);
	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// Toggle Handlers
	void OnToggleCaptureClicks(ECheckBoxState NewState);
	ECheckBoxState GetCaptureClicksState() const;
	void OnToggleCaptureKeyEvents(ECheckBoxState NewState);
	ECheckBoxState GetCaptureKeyEventsState() const;
	void OnToggleCaptureHover(ECheckBoxState NewState);
	ECheckBoxState GetCaptureHoverState() const;
	void OnToggleCaptureMove(ECheckBoxState NewState);
	ECheckBoxState GetCaptureMoveState() const;
	void OnToggleCaptureAnalog(ECheckBoxState NewState);
	ECheckBoxState GetCaptureAnalogState() const;
	void OnToggleCaptureFocus(ECheckBoxState CheckBoxState);
	ECheckBoxState GetCaptureFocusState() const;
	void OnToggleOverlay(ECheckBoxState NewState);
	ECheckBoxState GetOverlayState() const;
	void OnToggleShowPanels(ECheckBoxState NewState);
	ECheckBoxState GetShowPanelsState() const;
	void OnToggleSpider(ECheckBoxState NewState);
	ECheckBoxState GetSpiderState() const;
	void OnSpiderDepthChanged(int32 NewValue);
	int32 GetSpiderDepth() const;
	void OnToggleHitTestGrid(ECheckBoxState NewState);
	ECheckBoxState GetHitTestGridState() const;

	UInputDebugSubsystem* GetSubsystem() const;
	TWeakObjectPtr<UInputDebugSubsystem> WeakSubsystem;
	bool bIsOverlay = false;
};

/**
 * The main Editor Dock Tab widget.
 * Composes the Dashboard, Settings, Log, Hierarchy, and Inspector into a single layout.
 */
class SInputFlowAnalyzer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowAnalyzer) {}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

private:
	UInputDebugSubsystem* GetActiveSubsystem() const;
	bool IsSessionRunning() const;
	EVisibility GetOverlayVisibility() const;
	EVisibility GetContentVisibility() const;

	TSharedPtr<SInputFlowLogView> LogView;
	TSharedPtr<SCommonUIHierarchyView> HierarchyView;
	TSharedPtr<SEnhancedInputInspector> InspectorView;
};