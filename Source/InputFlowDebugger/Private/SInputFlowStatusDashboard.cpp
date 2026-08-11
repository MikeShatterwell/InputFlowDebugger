// Copyright Mike Desrosiers, All Rights Reserved

#include "SInputFlowStatusDashboard.h"
#include "InputFlowHelpers.h"

// Engine
#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>

// CommonUI
#if WITH_PLUGIN_COMMONUI
#include <CommonInputSubsystem.h>
#endif

// Slate
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Layout/SExpandableArea.h>
#include <Widgets/Layout/SScrollBox.h>
#include <Widgets/Text/STextBlock.h>

void SInputFlowStatusDashboard::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	WeakSubsystem = InSubsystem;
	auto LabelStyle = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	auto LabelColor = FLinearColor(0.6f, 0.6f, 0.6f);

	ChildSlot
	[
		SNew(SBorder)
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

void SInputFlowStatusDashboard::SetDebugSubsystem(UInputDebugSubsystem* InSubsystem)
{
	WeakSubsystem = InSubsystem;
}

void SInputFlowStatusDashboard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Refresh Subsystem reference if stale. Only fires when the current one is gone,
	// so it never overrides an explicit target pick.
	if (!WeakSubsystem.IsValid())
	{
		WeakSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (SlateFocusLabel.IsValid()) SlateFocusLabel->SetText(GetFocusWidgetName());

	UInputDebugSubsystem* DebugSub = WeakSubsystem.Get();
	if (IsValid(DebugSub))
	{
		if (CommonInputTypeLabel.IsValid()) CommonInputTypeLabel->SetText(GetCommonInputType(DebugSub));

		const FInputSnapshotStrings& State = DebugSub->GetInputSnapshotStrings();
		if (ActionRouterLeafLabel.IsValid()) ActionRouterLeafLabel->SetText(FText::FromString(State.ActiveCommonUILeaf));
		if (InputConfigLabel.IsValid()) InputConfigLabel->SetText(FText::FromString(State.InputConfig));
		if (MouseCaptureLabel.IsValid()) MouseCaptureLabel->SetText(FText::FromString(State.MouseCaptureMode));
		if (BoundActionsLabel.IsValid()) BoundActionsLabel->SetText(GetActiveBoundActions(DebugSub));
	}
}

FText SInputFlowStatusDashboard::GetFocusWidgetName() const
{
	// Slate tracks focus per user. Resolve the target player's user index so split-screen
	// reports that player's focus instead of whichever user happens to hold it.
	TSharedPtr<SWidget> FocusWidget;

	const UInputDebugSubsystem* Subsystem = WeakSubsystem.Get();
	const ULocalPlayer* LP = IsValid(Subsystem) ? Subsystem->GetDebugLocalPlayer() : nullptr;
	const int32 UserIndex = InputFlowHelpers::GetSlateUserIndexForLocalPlayer(LP);

	if (UserIndex != INDEX_NONE)
	{
		FocusWidget = FSlateApplication::Get().GetUserFocusedWidget(static_cast<uint32>(UserIndex));
	}
	else
	{
		FocusWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}

	if (!FocusWidget.IsValid()) return FText::FromString("Slate Focus: None");

	FString WidgetName = FocusWidget->ToString();
	int32 AddressIndex;
	if (WidgetName.FindChar('@', AddressIndex)) { WidgetName =  WidgetName.Left(AddressIndex); }
	return FText::FromString(FString::Printf(TEXT("%s"), *WidgetName));
}

FText SInputFlowStatusDashboard::GetCommonInputType(UInputDebugSubsystem* Subsystem) const
{
	if (!IsValid(Subsystem)) return FText::GetEmpty();

	// CommonInputSubsystem is per local player - gamepad vs mouse/KB can differ per viewport.
	if (ULocalPlayer* LP = Subsystem->GetDebugLocalPlayer())
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
	const FInputSnapshotStrings& State = Subsystem->GetInputSnapshotStrings();
	if (State.BoundActions.Num() == 0) return FText::FromString("No active bindings detected.");
	
	FString Combined;
	for (const FString& S : State.BoundActions)
	{
		Combined += S + TEXT("\n");
	}
	return FText::FromString(Combined);
}