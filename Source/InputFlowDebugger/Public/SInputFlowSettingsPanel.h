// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>

class SInputFlowSpinBox;
class UInputDebugSubsystem;

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