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
#include "InputFlowSettings.h"
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
	if (WidgetName.FindChar('@', AddressIndex)) { WidgetName =  WidgetName.Left(AddressIndex); }
	return FText::FromString(FString::Printf(TEXT("%s"), *WidgetName));
}

FText SInputFlowStatusDashboard::GetCommonInputType(UInputDebugSubsystem* Subsystem) const
{
	if (!Subsystem) return FText::GetEmpty();
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

	TAttribute<bool> VizEnabledAttr = TAttribute<bool>::Create([this]()
	{
		return bIsOverlay || UInputFlowSettings::Get()->IsOverlayEnabled();
	});

	const TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);

	// Capture Filters (Combo Button)
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.OnGetMenuContent(this, &SInputFlowSettingsPanel::MakeFilterMenu)
		.Cursor(EMouseCursor::Default)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Filter")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(INVTEXT("Capture Filters")) ]
		]
	];

	// Panel Visibility (Combo Button)
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.IsEnabled(VizEnabledAttr)
		.OnGetMenuContent(this, &SInputFlowSettingsPanel::MakePanelMenu)
		.Cursor(EMouseCursor::Default)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Layout")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(INVTEXT("Panels")) ]
		]
	];
	
	Toolbar->AddSlot().AutoWidth().Padding(8, 2) [ SNew(SSeparator).Orientation(Orient_Vertical) ];

	// Navigation Simulation
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.IsEnabled(VizEnabledAttr)
		.OnGetMenuContent(this, &SInputFlowSettingsPanel::MakeNavMenu)
		.Cursor(EMouseCursor::Default)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Transform")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(INVTEXT("Nav Sim")) ]
		]
	];

	Toolbar->AddSlot().AutoWidth().Padding(8, 2) [ SNew(SSeparator).Orientation(Orient_Vertical) ];

	// Focus Highlight Toggle
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.IsEnabled(VizEnabledAttr)
		.IsChecked_Lambda([this](){ return UInputFlowSettings::Get()->IsFocusHighlightEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState){ 
			UInputFlowSettings* S = GetMutableDefault<UInputFlowSettings>();
			S->SetShowFocusHighlight(!S->IsFocusHighlightEnabled());
		})
		.Cursor(EMouseCursor::Default)
		.ToolTipText(INVTEXT("Highlight the currently focused widget"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Visible")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
			[ SNew(STextBlock).Text(INVTEXT("Focus")) ]
		]
	];

	// Hit Test Grid
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.IsEnabled(VizEnabledAttr)
		.IsChecked_Lambda([this](){ return UInputFlowSettings::Get()->IsHitTestGridShown() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState){ OnToggleHitTestGrid(); })
		.Cursor(EMouseCursor::Default)
		.ToolTipText(INVTEXT("Show Hit Test Grid overlay"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Visible")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
			[ SNew(STextBlock).Text(INVTEXT("Hit Test")) ]
		]
	];

	// Overlay Toggle (Only in Editor)
	if (!bIsOverlay)
	{
		Toolbar->AddSlot().AutoWidth().Padding(2, 0)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([this](){ return UInputFlowSettings::Get()->IsOverlayEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState){ OnToggleOverlay(); })
			.Cursor(EMouseCursor::Default)
			.ToolTipText(INVTEXT("Enable In-Game Overlay"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
				[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Visible")) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
				[ SNew(STextBlock).Text(INVTEXT("Overlay")) ]
			]
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush(bIsOverlay ? "NoBrush" : "ToolPanel.GroupBorder"))
		.Padding(4)
		[
			Toolbar
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

TSharedRef<SWidget> SInputFlowSettingsPanel::MakeNavMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// Toggle Simulation
	MenuBuilder.AddMenuEntry(
		INVTEXT("Enable Nav Spider"),
		INVTEXT("Simulates navigation paths from the currently focused widget"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				UInputFlowSettings* S = GetMutableDefault<UInputFlowSettings>();
				S->SetEnableNavSimulation(!S->IsNavSimulationEnabled());
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() {
				return UInputFlowSettings::Get()->IsNavSimulationEnabled();
			})
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	// Depth Spinbox
	const TSharedRef<SWidget> DepthWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[ SNew(STextBlock).Text(INVTEXT("Search Depth")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SAssignNew(DepthSpinBox, SInputFlowSpinBox)
			.MinValue(1)
			.MaxValue(5)
			.Value_Lambda([this]() { return UInputFlowSettings::Get()->GetNavigationSearchDepth(); })
			.OnValueChanged_Lambda([this](int32 NewVal) { 
				GetMutableDefault<UInputFlowSettings>()->SetNavigationSearchDepth(NewVal);
			})
		];
	
	if (bIsOverlay && DepthSpinBox.IsValid()) DepthSpinBox->SetCanSupportFocus(false);
	MenuBuilder.AddWidget(DepthWidget, FText::GetEmpty());
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SInputFlowSettingsPanel::MakePanelMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	auto AddPanelEntry = [&](const FText& Label, FName PropertyName)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SInputFlowSettingsPanel::OnTogglePanel, PropertyName),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SInputFlowSettingsPanel::IsPanelChecked, PropertyName)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	};

	AddPanelEntry(INVTEXT("Event Log"), "bShowLogPanel");
	AddPanelEntry(INVTEXT("Status Dashboard"), "bShowDashboardPanel");
#if WITH_PLUGIN_COMMONUI
	AddPanelEntry(INVTEXT("CommonUI Hierarchy"), "bShowHierarchyPanel");
#endif
#if WITH_PLUGIN_ENHANCEDINPUT
	AddPanelEntry(INVTEXT("Enhanced Input"), "bShowEnhancedInputPanel");
#endif

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SInputFlowSettingsPanel::MakeFilterMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	auto AddFilterEntry = [&](const FText& Label, FName PropertyName, const FText& Tooltip)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SInputFlowSettingsPanel::OnToggleFilter, PropertyName),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SInputFlowSettingsPanel::IsFilterChecked, PropertyName)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	};

	MenuBuilder.BeginSection("Events", INVTEXT("Event Types"));
	AddFilterEntry(INVTEXT("Log Clicks"), "bCaptureClicks", INVTEXT("Log Mouse Down/Up/DoubleClick"));
	AddFilterEntry(INVTEXT("Log Keys"), "bCaptureKeyEvents", INVTEXT("Log Key Down/Up"));
	AddFilterEntry(INVTEXT("Log Analog"), "bCaptureAnalog", INVTEXT("Log Analog Axis inputs"));
	AddFilterEntry(INVTEXT("Log Focus"), "bCaptureFocus", INVTEXT("Log Focus change events"));
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Noise", INVTEXT("High Frequency"));
	AddFilterEntry(INVTEXT("Log Hover"), "bCaptureHover", INVTEXT("Log Hover Enter/Leave (Noisy)"));
	AddFilterEntry(INVTEXT("Log Mouse Move"), "bCaptureMouseMove", INVTEXT("Log raw Mouse Move (Very Noisy)"));
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Handled", INVTEXT("Status"));
	AddFilterEntry(INVTEXT("Show Handled"), "bShowHandledEvents", INVTEXT("Show events handled by Slate widgets"));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SInputFlowSettingsPanel::OnToggleFilter(FName PropertyName)
{
	UInputFlowSettings* Settings = GetMutableDefault<UInputFlowSettings>();
	if (FProperty* Prop = Settings->GetClass()->FindPropertyByName(PropertyName))
	{
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			const bool bCurrent = BoolProp->GetPropertyValue_InContainer(Settings);
			BoolProp->SetPropertyValue_InContainer(Settings, !bCurrent);
			Settings->SaveConfig();
			Settings->GetOnSettingsChanged().Broadcast();
		}
	}
}

bool SInputFlowSettingsPanel::IsFilterChecked(FName PropertyName) const
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (FProperty* Prop = Settings->GetClass()->FindPropertyByName(PropertyName))
	{
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			return BoolProp->GetPropertyValue_InContainer(Settings);
		}
	}
	return false;
}

void SInputFlowSettingsPanel::OnTogglePanel(FName PropertyName)
{
	UInputFlowSettings* Settings = GetMutableDefault<UInputFlowSettings>();
	if (FProperty* Prop = Settings->GetClass()->FindPropertyByName(PropertyName))
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			bool bCurrent = BoolProp->GetPropertyValue_InContainer(Settings);
			BoolProp->SetPropertyValue_InContainer(Settings, !bCurrent);
			Settings->SaveConfig();
			Settings->GetOnSettingsChanged().Broadcast();
		}
	}
}

bool SInputFlowSettingsPanel::IsPanelChecked(FName PropertyName) const
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (FProperty* Prop = Settings->GetClass()->FindPropertyByName(PropertyName))
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			return BoolProp->GetPropertyValue_InContainer(Settings);
		}
	}
	return false;
}

void SInputFlowSettingsPanel::OnToggleOverlay()
{
	UInputFlowSettings* S = GetMutableDefault<UInputFlowSettings>();
	S->SetEnableOverlay(!S->IsOverlayEnabled());
}

void SInputFlowSettingsPanel::OnToggleHitTestGrid()
{
	UInputFlowSettings* S = GetMutableDefault<UInputFlowSettings>();
	S->SetShowHitTestGrid(!S->IsHitTestGridShown());
}

UInputDebugSubsystem* SInputFlowSettingsPanel::GetSubsystem() const
{
	return WeakSubsystem.Get();
}

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