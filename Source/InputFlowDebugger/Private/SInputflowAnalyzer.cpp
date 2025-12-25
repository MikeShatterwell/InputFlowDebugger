// Copyright Mike Desrosiers, All Rights Reserved

#include "SInputFlowAnalyzer.h"

// CommonUI
#if WITH_PLUGIN_COMMONUI
#include <CommonInputSubsystem.h>
#endif

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
#include <Widgets/Layout/SUniformGridPanel.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "SCommonUIHierarchyView.h"
#include "SEnhancedInputInspector.h"
#include "SInputFlowLogView.h"

// --------------------------------------------------------------------
// Focus-Controllable SpinBox
// --------------------------------------------------------------------
// Standard SSpinBox does not allow toggling SupportsKeyboardFocus via arguments.
class SInputFlowSpinBox : public SSpinBox<int32>
{
public:
	SLATE_BEGIN_ARGS(SInputFlowSpinBox)
		: _MinValue(1)
		, _MaxValue(100)
		, _Value(1)
	{}
	SLATE_ATTRIBUTE(TOptional<int32>, MinValue)
	SLATE_ATTRIBUTE(TOptional<int32>, MaxValue)
	SLATE_ATTRIBUTE(int32, Value)
	SLATE_EVENT(FOnValueChanged, OnValueChanged)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs)
	{
		SSpinBox<int32>::Construct(SSpinBox<int32>::FArguments()
			.MinValue(InArgs._MinValue)
			.MaxValue(InArgs._MaxValue)
			.Value(InArgs._Value)
			.OnValueChanged(InArgs._OnValueChanged)
		);
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		// Only support focus if explicitly allowed
		return bCanSupportFocus && SSpinBox<int32>::SupportsKeyboardFocus();
	}

	void SetCanSupportFocus(bool bInCanSupport)
	{
		bCanSupportFocus = bInCanSupport;
	}

private:
	bool bCanSupportFocus = true;
};

// --------------------------------------------------------------------
// SInputFlowStatusDashboard
// --------------------------------------------------------------------

void SInputFlowStatusDashboard::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	WeakSubsystem = InSubsystem;
	auto LabelStyle = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	auto LabelColor = FLinearColor(0.6f, 0.6f, 0.6f);

	ChildSlot
	[
		SNew(SBorder)
		//.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
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
	];
}

void SInputFlowStatusDashboard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Refresh Subsystem reference if stale
	if (!WeakSubsystem.IsValid())
	{
		WeakSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (SlateFocusLabel.IsValid()) SlateFocusLabel->SetText(GetFocusWidgetName());

	UInputDebugSubsystem* DebugSub = WeakSubsystem.Get();
	if (DebugSub)
	{
		if (CommonInputTypeLabel.IsValid()) CommonInputTypeLabel->SetText(GetCommonInputType(DebugSub));

		const FInputOverlayState& State = DebugSub->GetOverlayState();
		if (ActionRouterLeafLabel.IsValid()) ActionRouterLeafLabel->SetText(FText::FromString(State.ActiveCommonUILeaf));
		if (InputConfigLabel.IsValid()) InputConfigLabel->SetText(FText::FromString(State.InputConfig));
		if (MouseCaptureLabel.IsValid()) MouseCaptureLabel->SetText(FText::FromString(State.MouseCaptureMode));
		if (BoundActionsLabel.IsValid()) BoundActionsLabel->SetText(GetActiveBoundActions(DebugSub));
	}
}

FText SInputFlowStatusDashboard::GetFocusWidgetName() const
{
	TSharedPtr<SWidget> FocusWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!FocusWidget.IsValid()) return FText::FromString("Slate Focus: None");

	FString WidgetName = FocusWidget->ToString();
	int32 AddressIndex;
	if(WidgetName.FindChar('@', AddressIndex)) { WidgetName =  WidgetName.Left(AddressIndex); }
	return FText::FromString(FString::Printf(TEXT("%s"), *WidgetName));
}

FText SInputFlowStatusDashboard::GetCommonInputType(UInputDebugSubsystem* Subsystem) const
{
	if(!Subsystem) return FText::GetEmpty();
	if (ULocalPlayer* LP = Subsystem->GetGameInstance()->GetFirstGamePlayer())
	{
#if WITH_PLUGIN_COMMONUI
		if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LP))
		{
			ECommonInputType CurrentInput = CommonInput->GetCurrentInputType();
			return FText::FromString(CurrentInput == ECommonInputType::Gamepad ? TEXT("Gamepad") : TEXT("Mouse/KB"));
		}
#endif
	}
	return FText::FromString(TEXT("CommonUI: N/A"));
}

FText SInputFlowStatusDashboard::GetActiveBoundActions(const UInputDebugSubsystem* Subsystem) const
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


// --------------------------------------------------------------------
// SInputFlowSettingsPanel
// --------------------------------------------------------------------

void SInputFlowSettingsPanel::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	WeakSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;
	
	// Enable Tick so we can keep the Subsystem pointer fresh across PIE restarts
	SetCanTick(true);

	// Helper to create checkboxes with consistent styling
	auto MakeCheckBox = [this](FString Label, auto GetFunc, auto SetFunc)
	{
		return SNew(SCheckBox)
			.IsFocusable(false)
			.IsChecked(this, GetFunc)
			.OnCheckStateChanged(this, SetFunc)
			[ SNew(STextBlock).Text(FText::FromString(Label)) ];
	};

	TSharedPtr<SUniformGridPanel> VisualGrid;
	SAssignNew(VisualGrid, SUniformGridPanel).SlotPadding(FMargin(0, 0, 10, 4));

	// Construct Visualization Grid dynamically
	int32 Col = 0;

	// Only add "Overlay Enabled" if we are NOT in the overlay itself
	if (!bIsOverlay)
	{
		VisualGrid->AddSlot(Col++, 0).HAlign(HAlign_Left) [ MakeCheckBox("Overlay Enabled", &SInputFlowSettingsPanel::GetOverlayState, &SInputFlowSettingsPanel::OnToggleOverlay) ];
	}
	
	// Panel Visibility Toggles
	VisualGrid->AddSlot(Col++, 0).HAlign(HAlign_Left) [ MakeCheckBox("Show Log", &SInputFlowSettingsPanel::GetShowLogState, &SInputFlowSettingsPanel::OnToggleShowLog) ];
	
	VisualGrid->AddSlot(Col++, 0).HAlign(HAlign_Left) [ MakeCheckBox("Show Dashboard", &SInputFlowSettingsPanel::GetShowDashboardState, &SInputFlowSettingsPanel::OnToggleShowDashboard) ];
	
	// Navigation & Grid
	VisualGrid->AddSlot(0, 1).HAlign(HAlign_Left) [ MakeCheckBox("Nav Spider", &SInputFlowSettingsPanel::GetSpiderState, &SInputFlowSettingsPanel::OnToggleSpider) ];
	VisualGrid->AddSlot(1, 1).HAlign(HAlign_Left) [ MakeCheckBox("Hit Test Grid", &SInputFlowSettingsPanel::GetHitTestGridState, &SInputFlowSettingsPanel::OnToggleHitTestGrid) ];
	VisualGrid->AddSlot(2, 1).HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
		[ SNew(STextBlock).Text(FText::FromString("Depth:")) ]
		+ SHorizontalBox::Slot().AutoWidth().MinWidth(60)
		[
			SAssignNew(DepthSpinBox, SInputFlowSpinBox)
			.MinValue(1)
			.MaxValue(5)
			.Value(this, &SInputFlowSettingsPanel::GetSpiderDepth)
			.OnValueChanged(this, &SInputFlowSettingsPanel::OnSpiderDepthChanged)
		]
	];
#if WITH_PLUGIN_COMMONUI
	VisualGrid->AddSlot(0, 3).HAlign(HAlign_Left) [ MakeCheckBox("Show CommonUI Hierarchy", &SInputFlowSettingsPanel::GetShowHierarchyState, &SInputFlowSettingsPanel::OnToggleShowHierarchy) ];
#endif
#if WITH_PLUGIN_ENHANCEDINPUT
	VisualGrid->AddSlot(0, 3) [ MakeCheckBox("Show EnhancedInput Inspector", &SInputFlowSettingsPanel::GetShowEnhancedInputState, &SInputFlowSettingsPanel::OnToggleShowEnhancedInput) ];
#endif

	if (bIsOverlay && DepthSpinBox.IsValid())
	{
		// If the overlay is focusable at all, it will interfere with the navigation spider.
		DepthSpinBox->SetCanSupportFocus(false);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(0, 0, 10, 4))
			
			// Logging Toggles
			+ SUniformGridPanel::Slot(0, 0).HAlign(HAlign_Left) [ MakeCheckBox("Log Clicks", &SInputFlowSettingsPanel::GetCaptureClicksState, &SInputFlowSettingsPanel::OnToggleCaptureClicks) ]
			+ SUniformGridPanel::Slot(1, 0).HAlign(HAlign_Left) [ MakeCheckBox("Log Keys", &SInputFlowSettingsPanel::GetCaptureKeyEventsState, &SInputFlowSettingsPanel::OnToggleCaptureKeyEvents) ]
			+ SUniformGridPanel::Slot(2, 0).HAlign(HAlign_Left) [ MakeCheckBox("Log Hover", &SInputFlowSettingsPanel::GetCaptureHoverState, &SInputFlowSettingsPanel::OnToggleCaptureHover) ]
			+ SUniformGridPanel::Slot(0, 1).HAlign(HAlign_Left) [ MakeCheckBox("Log Move", &SInputFlowSettingsPanel::GetCaptureMoveState, &SInputFlowSettingsPanel::OnToggleCaptureMove) ]
			+ SUniformGridPanel::Slot(1, 1).HAlign(HAlign_Left) [ MakeCheckBox("Log Analog", &SInputFlowSettingsPanel::GetCaptureAnalogState, &SInputFlowSettingsPanel::OnToggleCaptureAnalog) ]
			+ SUniformGridPanel::Slot(2, 1).HAlign(HAlign_Left) [ MakeCheckBox("Log Focus", &SInputFlowSettingsPanel::GetCaptureFocusState, &SInputFlowSettingsPanel::OnToggleCaptureFocus) ]
			+ SUniformGridPanel::Slot(0, 2).HAlign(HAlign_Left) [ MakeCheckBox("Log Handled", &SInputFlowSettingsPanel::GetCaptureHandledEventState, &SInputFlowSettingsPanel::OnToggleCaptureHandledEvents) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			VisualGrid.ToSharedRef()
		]
	];
}

void SInputFlowSettingsPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Ensure we always have a valid subsystem pointer.
	// This is critical for the Editor Tab which persists while PIE sessions (and their subsystems) die and respawn.
	if (!WeakSubsystem.IsValid())
	{
		WeakSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}
}

UInputDebugSubsystem* SInputFlowSettingsPanel::GetSubsystem() const
{
	return WeakSubsystem.Get();
}

void SInputFlowSettingsPanel::OnToggleCaptureClicks(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetCaptureClicks(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureClicksState() const { auto* S = GetSubsystem(); return (S && S->GetCaptureClicks()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleCaptureKeyEvents(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetCaptureKeyEvents(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureKeyEventsState() const { auto* S = GetSubsystem(); return (S && S->GetCaptureKeyEvents()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleCaptureHover(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetCaptureHover(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureHoverState() const { auto* S = GetSubsystem(); return (S && S->GetCaptureHover()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleCaptureMove(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetCaptureMouseMove(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureMoveState() const { auto* S = GetSubsystem(); return (S && S->GetCaptureMouseMove()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleCaptureAnalog(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetCaptureAnalog(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureAnalogState() const { auto* S = GetSubsystem(); return (S && S->GetCaptureAnalog()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleCaptureFocus(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetCaptureFocus(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureFocusState() const { auto* S = GetSubsystem(); return (S && S->GetCaptureFocus()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleOverlay(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetOverlayEnabled(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetOverlayState() const { auto* S = GetSubsystem(); return (S && S->GetIsOverlayEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleCaptureHandledEvents(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetShowHandledEvents(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetCaptureHandledEventState() const { auto* S = GetSubsystem(); return (S && S->GetShowHandledEvents()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleSpider(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetNavigationSimulationEnabled(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetSpiderState() const { auto* S = GetSubsystem(); return (S && S->GetNavigationSimulationEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnSpiderDepthChanged(int32 NewValue) { if (auto* S = GetSubsystem()) S->SetNavigationDepth(NewValue); }
int32 SInputFlowSettingsPanel::GetSpiderDepth() const { if (auto* S = GetSubsystem()) return S->GetNavigationDepth(); return 1; }

void SInputFlowSettingsPanel::OnToggleHitTestGrid(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetShowHitTestGrid(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetHitTestGridState() const { auto* S = GetSubsystem(); return (S && S->GetShowHitTestGrid()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleShowLog(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetShowLogPanel(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetShowLogState() const { auto* S = GetSubsystem(); return (S && S->GetShowLogPanel()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleShowHierarchy(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetShowHierarchyPanel(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetShowHierarchyState() const { auto* S = GetSubsystem(); return (S && S->GetShowHierarchyPanel()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleShowEnhancedInput(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetShowEnhancedInputPanel(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetShowEnhancedInputState() const { auto* S = GetSubsystem(); return (S && S->GetShowEnhancedInputPanel()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

void SInputFlowSettingsPanel::OnToggleShowDashboard(ECheckBoxState NewState) { if (auto* S = GetSubsystem()) S->SetShowDashboardPanel(NewState == ECheckBoxState::Checked); }
ECheckBoxState SInputFlowSettingsPanel::GetShowDashboardState() const { auto* S = GetSubsystem(); return (S && S->GetShowDashboardPanel()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }


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