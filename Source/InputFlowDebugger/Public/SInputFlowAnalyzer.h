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

class SInputFlowAnalyzer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowAnalyzer) {}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	UInputDebugSubsystem* GetActiveSubsystem() const;
	bool IsSessionRunning() const;
	EVisibility GetOverlayVisibility() const;
	EVisibility GetContentVisibility() const;
	
	// Dashboard helpers
	FText GetFocusWidgetName() const;
	FText GetCommonInputType(UInputDebugSubsystem* Subsystem) const;
	FText GetActiveBoundActions(const UInputDebugSubsystem* Subsystem) const;

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

	// Header elements
	TSharedPtr<STextBlock> MouseCaptureLabel;
	TSharedPtr<STextBlock> CommonInputTypeLabel;
	TSharedPtr<STextBlock> SlateFocusLabel;
	TSharedPtr<STextBlock> ActionRouterLeafLabel;
	TSharedPtr<STextBlock> InputConfigLabel;

	TSharedPtr<STextBlock> BoundActionsLabel;

	// Composed Widgets
	TSharedPtr<SInputFlowLogView> LogView;
	TSharedPtr<SCommonUIHierarchyView> HierarchyView;
	TSharedPtr<SEnhancedInputInspector> InspectorView;
};