// Copyright Mike Desrosiers, All Rights Reserved.

#include "SLocalizationInspector.h"

// Internationalization
#include <Internationalization/Culture.h>
#include <Internationalization/Internationalization.h>

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Styling/AppStyle.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SComboBox.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SScrollBox.h>
#include <Widgets/Layout/SSeparator.h>
#include <Widgets/SBoxPanel.h>
#include <Widgets/Text/STextBlock.h>

// Internal
#include "InputFlowLocAnalyzer.h"
#include "InputFlowSettings.h"
#include "LogInputFlow.h"

#define INVTEXT_NAMESPACE "InputFlowDebugger.Localization"

namespace InputFlowLocStyle
{
	static const FLinearColor Color_Localized        = FLinearColor(0.30f, 0.85f, 0.30f);
	static const FLinearColor Color_Hardcoded        = FLinearColor(1.00f, 0.35f, 0.35f);
	static const FLinearColor Color_CultureInvariant = FLinearColor(0.65f, 0.65f, 0.65f);
	static const FLinearColor Color_Empty            = FLinearColor(0.50f, 0.50f, 0.50f);

	static FLinearColor GetStatusColor(const EInputFlowLocStatus Status)
	{
		switch (Status)
		{
			case EInputFlowLocStatus::Localized:        return Color_Localized;
			case EInputFlowLocStatus::Hardcoded:        return Color_Hardcoded;
			case EInputFlowLocStatus::CultureInvariant: return Color_CultureInvariant;
			default:                                    return Color_Empty;
		}
	}

	static FString GetStatusName(const EInputFlowLocStatus Status)
	{
		switch (Status)
		{
			case EInputFlowLocStatus::Localized:        return TEXT("Localized");
			case EInputFlowLocStatus::Hardcoded:        return TEXT("Hardcoded");
			case EInputFlowLocStatus::CultureInvariant: return TEXT("Culture Invariant");
			default:                                    return TEXT("Empty");
		}
	}
}

void SLocalizationInspector::Construct(const FArguments& InArgs)
{
	bCanSupportFocus = false;

	RebuildCultureOptions();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("NoBrush"))
		.Padding(6.0f)
		[
			SNew(SVerticalBox)

			// --- Culture row ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				BuildCultureRow()
			]

			// --- Switch status feedback ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SAssignNew(SwitchStatusText, STextBlock)
				.AutoWrapText(true)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]

			// --- Overlay toggles ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				BuildOverlayTogglesRow()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]
		]
	];

}

void SLocalizationInspector::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (!GetVisibility().IsVisible())
	{
		return;
	}

	if (InCurrentTime - LastCultureRebuildTime > 1.0)
	{
		const int32 OldCount = CultureOptions.Num();
		RebuildCultureOptions();
		if (OldCount != CultureOptions.Num() && CultureCombo.IsValid())
		{
			CultureCombo->RefreshOptions();
		}
		LastCultureRebuildTime = InCurrentTime;
	}
}

// -----------------------------------------------------------------------------
// Culture row
// -----------------------------------------------------------------------------

void SLocalizationInspector::RebuildCultureOptions()
{
	TArray<FString> AllCultureNames = FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game);
	AllCultureNames.Sort();

	CultureOptions.Reset();
	CultureOptions.Reserve(AllCultureNames.Num() + 1);

	const FString CurrentName = FInternationalization::Get().GetCurrentCulture()->GetName();
	if (!AllCultureNames.Contains(CurrentName))
	{
		CultureOptions.Add(MakeShared<FString>(CurrentName));
	}

	for (const FString& Name : AllCultureNames)
	{
		CultureOptions.Add(MakeShared<FString>(Name));
	}
}

TSharedRef<SWidget> SLocalizationInspector::BuildCultureRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 6, 0)
		[
			SNew(STextBlock)
			.Text(INVTEXT("Culture:"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(CultureCombo, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&CultureOptions)
			.OnGenerateWidget(this, &SLocalizationInspector::MakeCultureComboItemWidget)
			.OnSelectionChanged(this, &SLocalizationInspector::OnCultureSelected)
			.IsFocusable(false)
			[
				SNew(STextBlock)
				.Text(this, &SLocalizationInspector::GetCurrentCultureLabel)
			]
		];
}

TSharedRef<SWidget> SLocalizationInspector::MakeCultureComboItemWidget(TSharedPtr<FString> Item)
{
	const FString Name = Item.IsValid() ? *Item : FString();
	FString DisplayName = Name;

	if (const FCulturePtr Culture = FInternationalization::Get().GetCulture(Name))
	{
		DisplayName = FString::Printf(TEXT("%s  (%s)"), *Culture->GetDisplayName(), *Name);
	}

	return SNew(STextBlock).Text(FText::FromString(DisplayName));
}

void SLocalizationInspector::OnCultureSelected(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	
	const FString Target = *NewSelection;
	const bool bOk = FInternationalization::Get().SetCurrentLanguageAndLocale(Target);

	UE_LOG(LogInputFlow, Log,
		TEXT("Localization Inspector: SetCurrentLanguageAndLocale('%s') -> %s"),
		*Target, bOk ? TEXT("OK") : TEXT("FAILED"));

	if (SwitchStatusText.IsValid())
	{
		SwitchStatusText->SetText(FText::FromString(bOk
			? FString::Printf(TEXT("Switched to '%s'. (No visible change? Project may have no loc data for this culture, or text is hardcoded.)"), *Target)
			: FString::Printf(TEXT("FAILED to switch to '%s' (culture not found)."), *Target)));
		SwitchStatusText->SetColorAndOpacity(FSlateColor(bOk
			? FLinearColor(0.30f, 0.85f, 0.30f)
			: FLinearColor(1.00f, 0.35f, 0.35f)));
	}
}

FText SLocalizationInspector::GetCurrentCultureLabel() const
{
	const FCultureRef Current = FInternationalization::Get().GetCurrentCulture();
	return FText::FromString(FString::Printf(TEXT("%s  (%s)"), *Current->GetDisplayName(), *Current->GetName()));
}

// -----------------------------------------------------------------------------
// Render toggles row
// -----------------------------------------------------------------------------

namespace
{
	template <typename TGetter, typename TSetter>
	TSharedRef<SWidget> MakeToggleCheckBox(const FText& Label, const FText& Tooltip, TGetter Getter, TSetter Setter)
	{
		return SNew(SCheckBox)
			.IsFocusable(false)
			.ToolTipText(Tooltip)
			.IsChecked_Lambda([Getter]()
			{
				return (UInputFlowSettings::Get()->*Getter)() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Getter, Setter](ECheckBoxState)
			{
				UInputFlowSettings* Settings = GetMutableDefault<UInputFlowSettings>();
				(Settings->*Setter)(!(Settings->*Getter)()); // Toggle the value
			})
			[
				SNew(STextBlock)
				.Text(Label)
				.Margin(FMargin(4, 2, 4, 2))
			];
	}
}

TSharedRef<SWidget> SLocalizationInspector::BuildOverlayTogglesRow()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SNew(STextBlock)
			.Text(INVTEXT("Visualizers"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeToggleCheckBox(
					INVTEXT("Draw Labels"),
					INVTEXT("Master toggle for the localization X-Ray. Must be on for any of the labels below to appear."),
					&UInputFlowSettings::IsLocLabelsEnabled,
					&UInputFlowSettings::SetLocLabelsEnabled)
			]
		]

		// --- Per-status filter ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(STextBlock)
			.Text(INVTEXT("Show statuses:"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeToggleCheckBox(
					INVTEXT("Localized"),
					INVTEXT("Show text that resolves through a namespace/key. Usually the noisy majority, so off by default."),
					&UInputFlowSettings::IsLocShowLocalized,
					&UInputFlowSettings::SetLocShowLocalized)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeToggleCheckBox(
					INVTEXT("Hardcoded"),
					INVTEXT("Show non-localized text (FText::FromString or a missing key). On by default."),
					&UInputFlowSettings::IsLocShowHardcoded,
					&UInputFlowSettings::SetLocShowHardcoded)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeToggleCheckBox(
					INVTEXT("Invariant"),
					INVTEXT("Show culture-invariant text (e.g. numeric formatting)."),
					&UInputFlowSettings::IsLocShowInvariant,
					&UInputFlowSettings::SetLocShowInvariant)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeToggleCheckBox(
					INVTEXT("Widget Names"),
					INVTEXT("Add the widget's name as a second line on each label. Roughly doubles label height."),
					&UInputFlowSettings::IsLocShowWidgetName,
					&UInputFlowSettings::SetLocShowWidgetName)
			]
		];
}

#undef INVTEXT_NAMESPACE