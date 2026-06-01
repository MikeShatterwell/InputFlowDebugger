// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// SlateCore
#include <Styling/SlateBrush.h>

// Internal
#include "InputDebugSubsystem.h"

#if WITH_PLUGIN_ENHANCEDINPUT
struct FLocStringData;
class UEnhancedPlayerInput;
class UInputMappingContext;
struct FAppliedInputContextData;
#endif

struct FInputFlowPhysicsItem
{
	FVector2D Position;
	FVector2D Size;
	FVector2D TargetPosition; // Where it wants to be
	FVector2D Velocity = FVector2D::ZeroVector;
	bool bIsFixed = false; // If true, acts as a wall (e.g. pinned panels)
};

class InputFlowHelpers
{
public:
	/** * iteratively resolves overlaps between items using a simple impulse solver.
	 * @param Items - List of items to resolve
	 * @param Boundary - The area to constrain items within (e.g. Viewport size)
	 * @param Iterations - Precision
	 */
	static void SolveAABBCollisions(TArray<FInputFlowPhysicsItem>& Items, const FVector2D& Boundary, int32 Iterations = 10);

	static const FSlateBrush* GetBackgroundBrush(bool bIsOverlay);
	static UInputDebugSubsystem* GetActiveDebugSubsystem();
	
	// Generates a human-readable name for a widget, including hierarchy context
	// e.g. "ConfirmButton (from PopupDialog)"
	static FString GetWidgetDisplayName(const TSharedPtr<SWidget>& Widget);
	
	static FString GetSimpleDirectionName(EUINavigation Direction);

	static TSharedPtr<SWidget> FindInteractiveDescendant(TSharedPtr<SWidget> Root);
	static bool IsDescendantOf(TSharedPtr<SWidget> Child, TSharedPtr<SWidget> PotentialParent);

	static const FTableRowStyle& GetTranslucentRowStyle();
	static const FTableViewStyle& GetTranslucentTableViewStyle();

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

	/**
	 * Attempts to pull the FText out of a Slate widget that displays text
	 * (STextBlock, SRichTextBlock, SEditableText, SMultiLineEditableText).
	 *
	 * Slate has no virtual GetText() on SWidget, so this dispatches by RTTI string.
	 * Falls back to UMG owner cast (UTextBlock / URichTextBlock / UEditableText* / etc.)
	 * for cases where a UMG widget uses a custom Slate widget under the hood.
	 *
	 * Returns true if extraction succeeded.
	 */
	static bool ExtractTextFromWidget(const TSharedPtr<SWidget>& Widget, FText& OutText);

	/**
	 * Walks the Slate widget tree under Root depth-first, invoking Callback for every
	 * widget where ExtractTextFromWidget succeeds.
	 *
	 * Subtrees are skipped if (a) the widget is collapsed, or (b) the widget's tag
	 * matches ExcludeTag (used to avoid scanning the debugger's own UI).
	 *
	 * This finds text widgets regardless of hit-test visibility.
	 */
	static void ForEachTextWidget(
		const TSharedPtr<SWidget>& Root,
		TFunctionRef<void(const TSharedPtr<SWidget>&)> Callback,
		FName ExcludeTag = NAME_None);
	
#if WITH_PLUGIN_ENHANCEDINPUT
	/** * Centralized accessor to reach into protected data of UEnhancedPlayerInput.
	 * Returns the internal Context Map containing priority and mappings.
	 */
	static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& GetInputContextData(const UEnhancedPlayerInput* PlayerInput);
#endif

	static const FName InputFlowAnalyzerTag;
};