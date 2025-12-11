// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// SlateCore
#include <Styling/SlateBrush.h>

// Internal
#include "InputDebugSubsystem.h"

class InputFlowHelpers
{
public:
	static const FSlateBrush* GetBackgroundBrush(bool bIsOverlay);
	static UInputDebugSubsystem* GetActiveDebugSubsystem();

	// Generates a human-readable name for a widget, including hierarchy context
	// e.g. "ConfirmButton (from PopupDialog)"
	static FString GetWidgetDisplayName(const TSharedPtr<SWidget>& Widget);
	
	// If bFindRootContext is true, it walks up the hierarchy to find the top-most container UserWidget (e.g. the Screen),
	// rather than the immediate UserWidget (e.g. the Button Component).
	static void TryOpenAsset(const TWeakObjectPtr<UObject>& ObjectWeak, const TWeakObjectPtr<UClass>& ClassWeak = nullptr, bool bFindRootContext = false);
	
	static bool IsGameWorldWidget(const TSharedPtr<SWidget>& Widget);
	static UWidget* GetOwnerUWidget(const TSharedPtr<SWidget>& Widget);
	static bool IsButtonWidget(const TSharedPtr<SWidget>& Widget);
	
	// Helper to convert Input Trigger Events to string/color
	static FString TriggerEventToString(int32 EventType);
	static FColor GetColorForTriggerEvent(int32 EventType);

	static const FName InputFlowAnalyzerTag;
};