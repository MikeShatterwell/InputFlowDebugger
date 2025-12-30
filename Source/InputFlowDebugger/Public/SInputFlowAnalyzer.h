// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>

class UInputDebugSubsystem;
class SInputFlowLogView;
class SInputFlowSpinBox;
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
	TSharedRef<SWidget> MakeNavMenu();
	TSharedRef<SWidget> MakePanelMenu();
	TSharedRef<SWidget> MakeFilterMenu();

	// Generic toggles for filters
	void OnToggleFilter(FName PropertyName);
	bool IsFilterChecked(FName PropertyName) const;

	// Generic toggles for panels
	void OnTogglePanel(FName PanelName);
	bool IsPanelChecked(FName PanelName) const;
	
	void OnToggleOverlay();
	void OnToggleHitTestGrid();

	UInputDebugSubsystem* GetSubsystem() const;
	TWeakObjectPtr<UInputDebugSubsystem> WeakSubsystem;
	
	TSharedPtr<SInputFlowSpinBox> DepthSpinBox; 
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