// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>

class STextBlock;
class SVerticalBox;
template <typename ItemType> class SComboBox;

/**
 * The contents of the "Localization Inspector" draggable panel.
 */
class SLocalizationInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLocalizationInspector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Begin SWidget overrides
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// End SWidget overrides

private:
	TSharedRef<SWidget> BuildCultureRow();
	TSharedRef<SWidget> BuildOverlayTogglesRow();

	/** Refresh the cached culture list from FInternationalization. */
	void RebuildCultureOptions();

	// --- Combo box callbacks ---
	TSharedRef<SWidget> MakeCultureComboItemWidget(TSharedPtr<FString> Item);
	void OnCultureSelected(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetCurrentCultureLabel() const;

private:
	/** Cached list of cultures with localization data, used as the data source for the combo. */
	TArray<TSharedPtr<FString>> CultureOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CultureCombo = nullptr;

	/** Weak pointer to the widget currently under the mouse, if it has extractable text. */
	TWeakPtr<SWidget> HoveredTextWidget = nullptr;

	/** Throttle culture-list rebuilds to once per second. */
	double LastCultureRebuildTime = 0.0;

	/** Transient feedback shown beneath the culture row after a switch attempt. */
	TSharedPtr<STextBlock> SwitchStatusText;
};