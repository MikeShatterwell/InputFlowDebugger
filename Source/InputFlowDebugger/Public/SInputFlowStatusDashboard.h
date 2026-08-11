// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>

class UInputDebugSubsystem;
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

	/** Points the dashboard at a different client's subsystem. */
	void SetDebugSubsystem(UInputDebugSubsystem* InSubsystem);

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