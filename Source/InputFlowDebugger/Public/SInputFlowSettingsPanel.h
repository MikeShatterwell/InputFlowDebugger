// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/Input/SSpinBox.h>
#include <Widgets/SCompoundWidget.h>

// Standard SSpinBox does not allow toggling SupportsKeyboardFocus via arguments.
template<typename NumericType>
class SInputFlowSpinBox : public SSpinBox<NumericType>
{
public:
	SLATE_BEGIN_ARGS(SInputFlowSpinBox<NumericType>)
			: _MinValue(1)
			, _MaxValue(100)
			, _Value(1)
	{}
		SLATE_ATTRIBUTE(TOptional<NumericType>, MinValue)
		SLATE_ATTRIBUTE(TOptional<NumericType>, MaxValue)
		SLATE_ATTRIBUTE(NumericType, Value)
		SLATE_EVENT(SSpinBox<NumericType>::FOnValueChanged, OnValueChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SSpinBox<NumericType>::Construct(typename SSpinBox<NumericType>::FArguments()
			.MinValue(InArgs._MinValue)
			.MaxValue(InArgs._MaxValue)
			.Value(InArgs._Value)
			.OnValueChanged(InArgs._OnValueChanged)
		);
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return bCanSupportFocus && SSpinBox<NumericType>::SupportsKeyboardFocus();
	}

	void SetCanSupportFocus(bool bInCanSupport)
	{
		bCanSupportFocus = bInCanSupport;
	}

private:
	bool bCanSupportFocus = true;
};

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
	TSharedRef<SWidget> MakeScaleMenu();
	TSharedRef<SWidget> MakeNavMenu();
	TSharedRef<SWidget> MakePanelMenu();

	// Generic toggles for panels
	void OnTogglePanel(FName PanelName);
	bool IsPanelChecked(FName PanelName) const;
	
	void OnToggleOverlay();
	void OnToggleHitTestGrid();

	UInputDebugSubsystem* GetSubsystem() const;
	TWeakObjectPtr<UInputDebugSubsystem> WeakSubsystem;
	
	TSharedPtr<SInputFlowSpinBox<int32>> DepthSpinBox;
	TSharedPtr<SInputFlowSpinBox<float>> ScaleSpinBox;
	bool bIsOverlay = false;
};