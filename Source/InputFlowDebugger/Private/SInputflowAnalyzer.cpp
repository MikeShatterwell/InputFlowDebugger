// Copyright Mike Desrosiers, All Rights Reserved

#include "SInputFlowAnalyzer.h"

// Editor
#if WITH_EDITOR
#include <Editor.h>
#endif

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Styling/AppStyle.h>
#include <Widgets/Input/SCheckBox.h>
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

// --------------------------------------------------------------------
// SInputFlowAnalyzer
// --------------------------------------------------------------------

void SInputFlowAnalyzer::Construct(const FArguments& InArgs)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	UInputDebugSubsystem* DebugSub = GetActiveSubsystem();

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
							SNew(SInputFlowStatusDashboard, DebugSub)
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
						.Text(FText::FromString("Waiting for Game Instance..."))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		]
	];
}

UInputDebugSubsystem* SInputFlowAnalyzer::GetActiveSubsystem() const
{
#if WITH_EDITOR
	if (GEditor && GEditor->PlayWorld && GEditor->PlayWorld->GetGameInstance())
	{
		return GEditor->PlayWorld->GetGameInstance()->GetSubsystem<UInputDebugSubsystem>();
	}
#endif
	if (UWorld* World = GEngine->GetWorldFromContextObject(GetTransientPackage(), EGetWorldErrorMode::ReturnNull))
	{
		if (World->IsGameWorld() && World->GetGameInstance())
		{
			return World->GetGameInstance()->GetSubsystem<UInputDebugSubsystem>();
		}
	}
	return nullptr;
}

bool SInputFlowAnalyzer::IsSessionRunning() const
{
	return GetActiveSubsystem() != nullptr;
}

EVisibility SInputFlowAnalyzer::GetOverlayVisibility() const
{
	return IsSessionRunning() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SInputFlowAnalyzer::GetContentVisibility() const
{
	return IsSessionRunning() ? EVisibility::Visible : EVisibility::Collapsed;
}