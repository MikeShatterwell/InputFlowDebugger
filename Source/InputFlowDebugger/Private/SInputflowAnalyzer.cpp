// Copyright Mike Desrosiers, All Rights Reserved

#include "SInputFlowAnalyzer.h"

// CoreUObject
#include <UObject/Package.h>

// Engine
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>

// Editor
#if WITH_EDITOR
#include <Editor.h>
#endif

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Styling/AppStyle.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SComboBox.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Layout/SSeparator.h>
#include <Widgets/Layout/SSplitter.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Layout/SScrollBox.h>
#include <Widgets/Layout/SExpandableArea.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "SInputFlowSettingsPanel.h"
#include "SCommonUIHierarchyView.h"
#include "SEnhancedInputInspector.h"
#include "SInputFlowLogView.h"
#include "SInputFlowStatusDashboard.h"

#define LOCTEXT_NAMESPACE "InputFlowAnalyzer"

// --------------------------------------------------------------------
// SInputFlowAnalyzer
// --------------------------------------------------------------------

void SInputFlowAnalyzer::Construct(const FArguments& InArgs)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	// Seed value only - RefreshTargets() below drives the real selection.
	UInputDebugSubsystem* DebugSub = InputFlowHelpers::GetActiveDebugSubsystem();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Visibility(this, &SInputFlowAnalyzer::GetContentVisibility)
				[
					SNew(SVerticalBox)

					// --- TARGET PICKER ---
					+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 4, 2)
					[
						SNew(SHorizontalBox)
						.Visibility(this, &SInputFlowAnalyzer::GetTargetPickerVisibility)

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("TargetLabel", "Viewport:"))
							.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
						]

						+ SHorizontalBox::Slot().AutoWidth().MinWidth(180)
						[
							SAssignNew(TargetCombo, SComboBox<TSharedPtr<FInputFlowDebugTarget>>)
							.OptionsSource(&Targets)
							.OnGenerateWidget(this, &SInputFlowAnalyzer::GenerateTargetOption)
							.OnSelectionChanged(this, &SInputFlowAnalyzer::OnTargetSelected)
							.ToolTipText(LOCTEXT("TargetTooltip",
								"Which client and local player every panel below reports on.\n\n"
								"CommonUI's action router and Enhanced Input are both LocalPlayerSubsystems, "
								"so each viewport has its own activatable tree and input stack.\n\n"
								"Other PIE clients only appear here when 'Run Under One Process' is enabled "
								"(Editor Preferences > Level Editor > Play > Multiplayer Options). Without it "
								"each client is a separate process and cannot be inspected from this editor."))
							[
								SNew(STextBlock)
								.Text(this, &SInputFlowAnalyzer::GetSelectedTargetLabel)
							]
						]
					]

					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SSplitter)
						.Orientation(Orient_Vertical)

						// --- TOP PANE: DASHBOARD + HIERARCHY + INSPECTOR ---
						+ SSplitter::Slot()
						.Value(0.5f)
						[
							SNew(SVerticalBox)
							// Dashboard
							+ SVerticalBox::Slot().AutoHeight().Padding(4)
							[
								SAssignNew(Dashboard, SInputFlowStatusDashboard, DebugSub)
							]

#if WITH_PLUGIN_COMMONUI || WITH_PLUGIN_ENHANCEDINPUT
							+ SVerticalBox::Slot().AutoHeight() [ SNew(SSeparator) ]

							// Composed Views
							+ SVerticalBox::Slot().FillHeight(1.0f)
							[
								SNew(SSplitter)
								.Orientation(Orient_Horizontal)

#if WITH_PLUGIN_COMMONUI
								+ SSplitter::Slot().Value(0.5f)
								[
									SAssignNew(HierarchyView, SCommonUIHierarchyView, DebugSub)
								]
#endif

#if WITH_PLUGIN_ENHANCEDINPUT
								+ SSplitter::Slot().Value(0.5f)
								[
									SAssignNew(InspectorView, SEnhancedInputInspector, DebugSub)
								]
#endif
							]
#endif
						]

						// --- BOTTOM PANE: LOG & CONFIG ---
						+ SSplitter::Slot()
						.Value(0.5f)
						[
							SNew(SVerticalBox)
							// Config Toggles
							+ SVerticalBox::Slot().AutoHeight().Padding(2)
							[
								SNew(SInputFlowSettingsPanel, DebugSub)
									.IsOverlay(false) // Editor mode
							]
							+ SVerticalBox::Slot().FillHeight(1.0f).HAlign(HAlign_Fill)
							[
								SAssignNew(LogView, SInputFlowLogView, DebugSub)
							]
						]
					]
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(20)
				.Visibility(this, &SInputFlowAnalyzer::GetOverlayVisibility)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WaitingForGameInstance", "Waiting for Game Instance..."))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		]
	];

	RefreshTargets();
}

void SInputFlowAnalyzer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Poll rather than hook PIE delegates: players can also join or drop mid-session.
	if (InCurrentTime - LastTargetPollTime > TargetPollInterval)
	{
		LastTargetPollTime = InCurrentTime;
		RefreshTargets();
	}
}

// --------------------------------------------------------------------
// Target Picker
// --------------------------------------------------------------------

void SInputFlowAnalyzer::RefreshTargets()
{
	TArray<TSharedPtr<FInputFlowDebugTarget>> NewTargets;
	InputFlowHelpers::GatherDebugTargets(NewTargets);

	bool bListChanged = NewTargets.Num() != Targets.Num();
	if (!bListChanged)
	{
		for (int32 i = 0; i < NewTargets.Num(); ++i)
		{
			if (!(*NewTargets[i] == *Targets[i]) || NewTargets[i]->Label != Targets[i]->Label)
			{
				bListChanged = true;
				break;
			}
		}
	}

	if (!bListChanged)
	{
		// Same set of targets - keep the existing shared pointers so the combo's selection
		// stays put, but re-assert the player choice in case something else reset it.
		if (SelectedTarget.IsValid())
		{
			if (UInputDebugSubsystem* Subsystem = SelectedTarget->Subsystem.Get())
			{
				Subsystem->SetDebugLocalPlayer(SelectedTarget->LocalPlayer.Get());
			}
		}
		return;
	}

	// Carry the selection across the rebuild by value, since the pointers are all new.
	TSharedPtr<FInputFlowDebugTarget> NewSelection;
	if (SelectedTarget.IsValid())
	{
		for (const TSharedPtr<FInputFlowDebugTarget>& Candidate : NewTargets)
		{
			if (*Candidate == *SelectedTarget)
			{
				NewSelection = Candidate;
				break;
			}
		}
	}

	// First run, or the previous target is gone (re-PIE, player left): fall back to the
	// first entry, which GatherDebugTargets orders as server / lowest player index.
	if (!NewSelection.IsValid() && NewTargets.Num() > 0)
	{
		NewSelection = NewTargets[0];
	}

	Targets = MoveTemp(NewTargets);

	if (TargetCombo.IsValid())
	{
		TargetCombo->RefreshOptions();
	}

	SetActiveTarget(NewSelection);
}

void SInputFlowAnalyzer::SetActiveTarget(TSharedPtr<FInputFlowDebugTarget> InTarget)
{
	SelectedTarget = InTarget;

	UInputDebugSubsystem* Subsystem = InTarget.IsValid() ? InTarget->Subsystem.Get() : nullptr;
	ULocalPlayer* LocalPlayer = InTarget.IsValid() ? InTarget->LocalPlayer.Get() : nullptr;

	if (IsValid(Subsystem))
	{
		// The subsystem is the single source of truth for "which local player" - the
		// in-game overlay panels read it too, so they follow this selection as well.
		Subsystem->SetDebugLocalPlayer(LocalPlayer);
	}

	// Picking a client means pointing the views at that client's subsystem.
	if (Dashboard.IsValid())     Dashboard->SetDebugSubsystem(Subsystem);
	if (LogView.IsValid())       LogView->SetDebugSubsystem(Subsystem);
#if WITH_PLUGIN_COMMONUI
	if (HierarchyView.IsValid()) HierarchyView->SetDebugSubsystem(Subsystem);
#endif
#if WITH_PLUGIN_ENHANCEDINPUT
	if (InspectorView.IsValid()) InspectorView->SetDebugSubsystem(Subsystem);
#endif

	if (TargetCombo.IsValid() && TargetCombo->GetSelectedItem() != InTarget)
	{
		TargetCombo->SetSelectedItem(InTarget);
	}
}

TSharedRef<SWidget> SInputFlowAnalyzer::GenerateTargetOption(TSharedPtr<FInputFlowDebugTarget> InTarget) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(InTarget.IsValid() ? InTarget->Label : TEXT("None")));
}

void SInputFlowAnalyzer::OnTargetSelected(TSharedPtr<FInputFlowDebugTarget> InTarget, ESelectInfo::Type SelectInfo)
{
	// Direct means SetSelectedItem() syncing the combo from SetActiveTarget - not a user pick.
	if (SelectInfo == ESelectInfo::Direct) return;

	SetActiveTarget(InTarget);
}

FText SInputFlowAnalyzer::GetSelectedTargetLabel() const
{
	if (SelectedTarget.IsValid())
	{
		return FText::FromString(SelectedTarget->Label);
	}
	return LOCTEXT("NoTarget", "None");
}

EVisibility SInputFlowAnalyzer::GetTargetPickerVisibility() const
{
	return Targets.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

// --------------------------------------------------------------------
// Session State
// --------------------------------------------------------------------

bool SInputFlowAnalyzer::IsSessionRunning() const
{
	return Targets.Num() > 0;
}

EVisibility SInputFlowAnalyzer::GetOverlayVisibility() const
{
	return IsSessionRunning() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SInputFlowAnalyzer::GetContentVisibility() const
{
	return IsSessionRunning() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
