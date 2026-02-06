// Copyright Mike Desrosiers, All Rights Reserved

#include "SInputFlowSettingsPanel.h"
#include "InputFlowHelpers.h"
#include "InputFlowSettings.h"

// Slate
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SComboButton.h>
#include <Widgets/Layout/SSeparator.h>
#include <Widgets/Images/SImage.h>

void SInputFlowSettingsPanel::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	WeakSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;
	
	// Enable Tick so we can keep the Subsystem pointer fresh across PIE restarts
	SetCanTick(true);
	bCanSupportFocus = false;

	TAttribute<bool> VizEnabledAttr = TAttribute<bool>::Create([this]()
	{
		return bIsOverlay || UInputFlowSettings::Get()->IsOverlayEnabled();
	});

	const TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);

	// Panel Visibility (Combo Button)
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.IsEnabled(VizEnabledAttr)
		.IsFocusable(false)
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

	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.IsEnabled(VizEnabledAttr)
		.IsFocusable(false)
		.OnGetMenuContent(this, &SInputFlowSettingsPanel::MakeScaleMenu)
		.Cursor(EMouseCursor::Default)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("ViewportToolbar.TransformScale")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(INVTEXT("Scale")) ]
		]
	];

	// Navigation Simulation
	Toolbar->AddSlot().AutoWidth().Padding(2, 0)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.IsEnabled(VizEnabledAttr)
		.IsFocusable(false)
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
		.IsFocusable(false)
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
		.IsEnabled(VizEnabledAttr)		.IsFocusable(false)
		.IsChecked_Lambda([this](){ return UInputFlowSettings::Get()->IsHitTestGridShown() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState){ OnToggleHitTestGrid(); })
		.Cursor(EMouseCursor::Default)
		.ToolTipText(INVTEXT("Show Hit Test Grid overlay"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
			[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Visible")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
			[ SNew(STextBlock).Text(INVTEXT("Hit Test X-Ray")) ]
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

TSharedRef<SWidget> SInputFlowSettingsPanel::MakeScaleMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	TSharedRef<SWidget> ScaleWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[ SNew(STextBlock).Text(INVTEXT("Overlay UI Scale")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SInputFlowSpinBox<float>)
			.MinValue(0.5f)
			.MaxValue(3.0f)
			.Value_Lambda([]() { return UInputFlowSettings::Get()->GetOverlayScale(); })
			.OnValueChanged_Lambda([](float NewVal) { 
				GetMutableDefault<UInputFlowSettings>()->SetOverlayScale(NewVal);
			})
		];

	MenuBuilder.AddWidget(ScaleWidget, FText::GetEmpty());
	return MenuBuilder.MakeWidget();
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
			SAssignNew(DepthSpinBox, SInputFlowSpinBox<int32>)
			.MinValue(1)
			.MaxValue(5)
			.Value_Lambda([this]() { return UInputFlowSettings::Get()->GetNavigationSearchDepth(); })
			.OnValueChanged_Lambda([this](int32 NewVal) { 
				GetMutableDefault<UInputFlowSettings>()->SetNavigationSearchDepth(NewVal);
			})
		];

	MenuBuilder.AddMenuEntry(
		INVTEXT("Show Labels"),
		INVTEXT("Show text labels on navigation simulation"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				UInputFlowSettings* S = GetMutableDefault<UInputFlowSettings>();
				S->SetShowNavLabels(!S->IsNavLabelsEnabled());
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() {
				return UInputFlowSettings::Get()->IsNavLabelsEnabled();
			})
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

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
#if WITH_PLUGIN_MODELVIEWVIEWMODEL
	AddPanelEntry(INVTEXT("MVVM Inspector"), "bShowMVVMInspectorPanel");
#endif

	return MenuBuilder.MakeWidget();
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