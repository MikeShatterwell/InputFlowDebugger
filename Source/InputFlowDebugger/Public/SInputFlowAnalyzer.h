// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Input/SComboBox.h>

// Internal
#include "InputFlowHelpers.h"

class UInputDebugSubsystem;
class SInputFlowLogView;
class SCommonUIHierarchyView;
class SEnhancedInputInspector;
class SInputFlowStatusDashboard;
class STextBlock;

/**
 * The main Editor Dock Tab widget.
 * Composes the Dashboard, Settings, Log, Hierarchy, and Inspector into a single layout.
 *
 * Owns the target picker: every composed view reports on one (client, local player) pair,
 * selected here rather than defaulting to "first world, first player".
 */
class SInputFlowAnalyzer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowAnalyzer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	bool IsSessionRunning() const;
	EVisibility GetOverlayVisibility() const;
	EVisibility GetContentVisibility() const;

	// --- Target Picker ---

	/** Re-enumerates targets, preserving the current selection where possible. */
	void RefreshTargets();

	/** Pushes a target down into every composed view. */
	void SetActiveTarget(TSharedPtr<FInputFlowDebugTarget> InTarget);

	TSharedRef<SWidget> GenerateTargetOption(TSharedPtr<FInputFlowDebugTarget> InTarget) const;
	void OnTargetSelected(TSharedPtr<FInputFlowDebugTarget> InTarget, ESelectInfo::Type SelectInfo);
	FText GetSelectedTargetLabel() const;
	EVisibility GetTargetPickerVisibility() const;

	TArray<TSharedPtr<FInputFlowDebugTarget>> Targets;
	TSharedPtr<FInputFlowDebugTarget> SelectedTarget;
	TSharedPtr<SComboBox<TSharedPtr<FInputFlowDebugTarget>>> TargetCombo;

	double LastTargetPollTime = 0.0;
	static constexpr double TargetPollInterval = 0.5;

	TSharedPtr<SInputFlowLogView> LogView;
	TSharedPtr<SCommonUIHierarchyView> HierarchyView;
	TSharedPtr<SEnhancedInputInspector> InspectorView;
	TSharedPtr<SInputFlowStatusDashboard> Dashboard;
};
