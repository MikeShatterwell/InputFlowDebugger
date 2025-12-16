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

	static TSharedPtr<SWidget> FindInteractiveDescendant(TSharedPtr<SWidget> Root);
	static bool IsDescendantOf(TSharedPtr<SWidget> Child, TSharedPtr<SWidget> PotentialParent);
	
	// If bFindRootContext is true, it walks up the hierarchy to find the top-most container UserWidget (e.g. the Screen),
	// rather than the immediate UserWidget (e.g. the Button Component).
	static void TryOpenAsset(const TWeakObjectPtr<UObject>& ObjectWeak, const TWeakObjectPtr<UClass>& ClassWeak = nullptr, bool bFindRootContext = false);
	
	static bool IsGameWorldWidget(const TSharedPtr<SWidget>& Widget);
	static UWidget* GetOwnerUWidget(const TSharedPtr<SWidget>& Widget);
	static bool IsButtonWidget(const TSharedPtr<SWidget>& Widget);
	
	// Helper to convert Input Trigger Events to string/color
	static FString TriggerEventToString(int32 EventType);
	static FColor GetColorForTriggerEvent(int32 EventType);
	
	static FString NavDirToString(EUINavigation Dir);
	static FString NavRuleToString(EUINavigationRule Rule);
	static FString NavSimResultToString(ENavSimResult Result);

	static FString WidgetDesc(const TSharedPtr<SWidget>& W);

	static bool IsTableViewWidget(const TSharedPtr<SWidget>& InWidget, FString InTypeStr = TEXT(""));

	static void LogWidgetPathVerbose(const FWidgetPath& Path, const TCHAR* Prefix);

	static int32 FindWidgetIndexInPath(const FWidgetPath& Path, const TSharedPtr<SWidget>& Widget);

	static FArrangedWidget ToWindowSpace(const FArrangedWidget& InArranged, const TSharedRef<SWindow>& Window);

	static TSharedPtr<SWidget> ResolveFocusableDescendant(TSharedPtr<SWidget> Root);

	static const FName InputFlowAnalyzerTag;
};