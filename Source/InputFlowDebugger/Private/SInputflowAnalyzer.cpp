// Copyright Mike Desrosiers, All Rights Reserved

#include "SInputFlowAnalyzer.h"

// CommonUI
#include <CommonInputSubsystem.h>

// Editor
#if WITH_EDITOR
#include <Editor.h>
#endif

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Styling/AppStyle.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SSpinBox.h>
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
#include "SCommonUIHierarchyView.h"
#include "SEnhancedInputInspector.h"
#include "SInputFlowLogView.h"

void SInputFlowAnalyzer::Construct(const FArguments& InArgs)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	UInputDebugSubsystem* DebugSub = GetActiveSubsystem();
	auto LabelStyle = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	auto LabelColor = FLinearColor(0.6f, 0.6f, 0.6f);

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
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f))
							.Padding(8)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().MinWidth(110)
									[ SNew(STextBlock).Text(FText::FromString("Slate Focus:")).ColorAndOpacity(LabelColor) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[ SAssignNew(SlateFocusLabel, STextBlock).ColorAndOpacity(FLinearColor::White) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().MinWidth(110)
									[ SNew(STextBlock).Text(FText::FromString("Active Leaf:")).ColorAndOpacity(LabelColor).Font(LabelStyle) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[ SAssignNew(ActionRouterLeafLabel, STextBlock).ColorAndOpacity(FLinearColor(0.2f, 1.0f, 0.4f)).Font(LabelStyle) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().MinWidth(110)
									[ SNew(STextBlock).Text(FText::FromString("Input Config:")).ColorAndOpacity(LabelColor) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[ SAssignNew(InputConfigLabel, STextBlock).ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.2f)) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().MinWidth(110)
									[ SNew(STextBlock).Text(FText::FromString("Mouse Capture:")).ColorAndOpacity(LabelColor) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[ SAssignNew(MouseCaptureLabel, STextBlock).ColorAndOpacity(FLinearColor(0.4f, 0.8f, 1.0f)) ]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().MinWidth(110)
									[ SNew(STextBlock).Text(FText::FromString("Input Type:")).ColorAndOpacity(LabelColor) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[ SAssignNew(CommonInputTypeLabel, STextBlock).ColorAndOpacity(FLinearColor::White) ]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SExpandableArea)
									.AreaTitle(FText::FromString("Bound Actions"))
									.BodyContent()
									[
										SNew(SBox)
										.MaxDesiredHeight(100.0f)
										[
											SNew(SScrollBox)
											+ SScrollBox::Slot()
											[
												SAssignNew(BoundActionsLabel, STextBlock)
												.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
												.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
											]
										]
									]
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight() [ SNew(SSeparator) ]
						
						// Composed Views
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SSplitter)
							.Orientation(Orient_Horizontal)
							
							+ SSplitter::Slot().Value(0.5f)
							[
								SAssignNew(HierarchyView, SCommonUIHierarchyView, DebugSub)
							]

							+ SSplitter::Slot().Value(0.5f)
							[
								SAssignNew(InspectorView, SEnhancedInputInspector, DebugSub)
							]
						]
					]

					// --- BOTTOM PANE: LOG & CONFIG ---
					+ SSplitter::Slot()
					.Value(0.5f)
					[
						SNew(SVerticalBox)
						// Config Toggles
						+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(4,0) [ SNew(SCheckBox).IsChecked(this, &SInputFlowAnalyzer::GetCaptureClicksState).OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleCaptureClicks) [ SNew(STextBlock).Text(FText::FromString("Log Clicks")) ] ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4,0) [ SNew(SCheckBox).IsChecked(this, &SInputFlowAnalyzer::GetCaptureKeyEventsState).OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleCaptureKeyEvents) [ SNew(STextBlock).Text(FText::FromString("Log Key Events")) ] ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4,0) [ SNew(SCheckBox).IsChecked(this, &SInputFlowAnalyzer::GetCaptureHoverState).OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleCaptureHover) [ SNew(STextBlock).Text(FText::FromString("Log Hover Events")) ] ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4,0) [ SNew(SCheckBox).IsChecked(this, &SInputFlowAnalyzer::GetCaptureMoveState).OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleCaptureMove) [ SNew(STextBlock).Text(FText::FromString("Log Mouse Move")) ] ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4,0) [ SNew(SCheckBox).IsChecked(this, &SInputFlowAnalyzer::GetCaptureAnalogState).OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleCaptureAnalog) [ SNew(STextBlock).Text(FText::FromString("Log Analog Input")) ] ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4,0) [ SNew(SCheckBox).IsChecked(this, &SInputFlowAnalyzer::GetCaptureFocusState).OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleCaptureFocus) [ SNew(STextBlock).Text(FText::FromString("Log Focus Events")) ] ]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 4)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SInputFlowAnalyzer::GetOverlayState)
								.OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleOverlay)
								[ SNew(STextBlock).Text(FText::FromString("Show In-Game Overlay")) ]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 4, 0).VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SInputFlowAnalyzer::GetShowPanelsState)
								.OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleShowPanels)
								[ SNew(STextBlock).Text(FText::FromString("Show Panels")) ]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 4, 0).VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SInputFlowAnalyzer::GetSpiderState)
								.OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleSpider)
								[ SNew(STextBlock).Text(FText::FromString("Nav Preview")) ]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 4, 0).VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SInputFlowAnalyzer::GetHitTestGridState)
								.OnCheckStateChanged(this, &SInputFlowAnalyzer::OnToggleHitTestGrid)
								[ SNew(STextBlock).Text(FText::FromString("Show Hit Test Grid")) ]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 4, 0).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(FText::FromString("Nav Preview Depth:"))
							]
							+ SHorizontalBox::Slot().AutoWidth().MinWidth(80).VAlign(VAlign_Center)
							[
								SNew(SSpinBox<int32>)
								.MinValue(1)
								.MaxValue(5)
								.Value(this, &SInputFlowAnalyzer::GetSpiderDepth)
								.OnValueChanged(this, &SInputFlowAnalyzer::OnSpiderDepthChanged)
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
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

void SInputFlowAnalyzer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (IsSessionRunning())
	{
		if (SlateFocusLabel.IsValid()) SlateFocusLabel->SetText(GetFocusWidgetName());
		
		UInputDebugSubsystem* DebugSub = GetActiveSubsystem();
		if (CommonInputTypeLabel.IsValid()) CommonInputTypeLabel->SetText(GetCommonInputType(DebugSub));

		// Dashboard Update Logic (Active Leaf)
		if (DebugSub)
		{
			const FInputOverlayState& State = DebugSub->GetOverlayState();
			if (ActionRouterLeafLabel.IsValid()) ActionRouterLeafLabel->SetText(FText::FromString(State.ActiveCommonUILeaf));
			if (InputConfigLabel.IsValid()) InputConfigLabel->SetText(FText::FromString(State.InputConfig));

			if (MouseCaptureLabel.IsValid()) MouseCaptureLabel->SetText(FText::FromString(State.MouseCaptureMode));
			if (BoundActionsLabel.IsValid()) BoundActionsLabel->SetText(GetActiveBoundActions(DebugSub));
		}
	}
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

// Helpers
FText SInputFlowAnalyzer::GetFocusWidgetName() const
{
	TSharedPtr<SWidget> FocusWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!FocusWidget.IsValid()) return FText::FromString("Slate Focus: None");

	FString WidgetName = FocusWidget->ToString();
	int32 AddressIndex;
	if(WidgetName.FindChar('@', AddressIndex)) { WidgetName =  WidgetName.Left(AddressIndex); }
	return FText::FromString(FString::Printf(TEXT("%s"), *WidgetName));
}

FText SInputFlowAnalyzer::GetCommonInputType(UInputDebugSubsystem* Subsystem) const
{
	if(!Subsystem) return FText::GetEmpty();
	if (ULocalPlayer* LP = Subsystem->GetGameInstance()->GetFirstGamePlayer())
	{
		if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LP))
		{
			ECommonInputType CurrentInput = CommonInput->GetCurrentInputType();
			return FText::FromString(CurrentInput == ECommonInputType::Gamepad ? TEXT("Gamepad") : TEXT("Mouse/KB"));
		}
	}
	return FText::FromString(TEXT("CommonUI: N/A"));
}

FText SInputFlowAnalyzer::GetActiveBoundActions(const UInputDebugSubsystem* Subsystem) const
{
	if (!Subsystem) return FText::GetEmpty();
	const FInputOverlayState& State = Subsystem->GetOverlayState();
	if (State.BoundActions.Num() == 0) return FText::FromString("No active bindings detected.");
	
	FString Combined;
	for (const FString& S : State.BoundActions)
	{
		Combined += S + TEXT("\n");
	}
	return FText::FromString(Combined);
}

// Toggles
void SInputFlowAnalyzer::OnToggleCaptureClicks(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetCaptureClicks(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetCaptureClicksState() const { auto* S = GetActiveSubsystem(); return (S && S->GetCaptureClicks()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleCaptureKeyEvents(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetCaptureKeyEvents(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetCaptureKeyEventsState() const { auto* S = GetActiveSubsystem(); return (S && S->GetCaptureKeyEvents()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleCaptureHover(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetCaptureHover(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetCaptureHoverState() const { auto* S = GetActiveSubsystem(); return (S && S->GetCaptureHover()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleCaptureMove(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetCaptureMouseMove(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetCaptureMoveState() const { auto* S = GetActiveSubsystem(); return (S && S->GetCaptureMouseMove()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleCaptureAnalog(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetCaptureAnalog(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetCaptureAnalogState() const { auto* S = GetActiveSubsystem(); return (S && S->GetCaptureAnalog()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleCaptureFocus(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetCaptureFocus(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetCaptureFocusState() const { auto* S = GetActiveSubsystem(); return (S && S->GetCaptureFocus()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleOverlay(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetOverlayEnabled(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetOverlayState() const { auto* S = GetActiveSubsystem(); return (S && S->IsOverlayEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleShowPanels(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetShowOverlayPanels(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetShowPanelsState() const { auto* S = GetActiveSubsystem(); return (S && S->GetShowOverlayPanels()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnToggleSpider(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetNavigationSimulationEnabled(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetSpiderState() const { auto* S = GetActiveSubsystem(); return (S && S->GetNavigationSimulationEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowAnalyzer::OnSpiderDepthChanged(int32 NewValue) { if (auto* S = GetActiveSubsystem()) S->SetNavigationDepth(NewValue); }
int32 SInputFlowAnalyzer::GetSpiderDepth() const { if (auto* S = GetActiveSubsystem()) return S->GetNavigationDepth(); return 1; }

void SInputFlowAnalyzer::OnToggleHitTestGrid(ECheckBoxState NewState) { if (auto* S = GetActiveSubsystem()) S->SetShowHitTestGrid(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowAnalyzer::GetHitTestGridState() const { auto* S = GetActiveSubsystem(); return (S && S->GetShowHitTestGrid()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }