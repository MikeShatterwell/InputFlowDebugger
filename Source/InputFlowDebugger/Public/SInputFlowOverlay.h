// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Types/SlateEnums.h"

class UInputDebugSubsystem;
class SInputFlowLogView;
class SCommonUIHierarchyView;
class SEnhancedInputInspector;
class SOverlay;

/**
 * An overlay widget that renders debug visualization for the Input Flow Analyzer.
 * It draws navigation simulation paths, focus history, and hit-test grids directly
 * onto the HUD layer using Slate's OnPaint.
 */
class SInputFlowOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowOverlay)
		: _Subsystem(nullptr)
	{}
		/** The subsystem instance that provides debug data (Focus history, Nav links, etc). */
		SLATE_ARGUMENT(TWeakObjectPtr<UInputDebugSubsystem>, Subsystem)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** SWidget Interface */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Helper to access the subsystem */
	UInputDebugSubsystem* GetSubsystem() const;

private:
	struct FPendingLabel
	{
		FVector2D OriginalPos; // Where it wanted to be
		FVector2D CurrentPos;  // Where it is now
		FVector2D Size;        // Calculated size of the box
		FString Text;
		FLinearColor Color;
		int32 LayerId;
	};

	// Mutable because we accumulate these during OnPaint which is const
	mutable TArray<FPendingLabel> LabelBatch;

	/* Resolves collisions in the pending label batch */
	void ResolveAndDrawLabels(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	
	/** Renders the highlight ring/box for the currently focused widget. */
	void PaintFocusedWidget(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;
	
	/** Renders the predicted navigation paths (arrows/splines from start to target widgets). */
	void PaintNavigationSimulation(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	/** Renders the trail of previously focused widgets (ghost trail). */
	void PaintFocusHistory(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	/** Renders the hit-testable widget grid for debugging mouse clicks. */
	void PaintHitTestGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	// --- Visual Primitives ---

	/** Draws a box around a widget with a label. */
	void DrawWidgetHighlight(const TSharedPtr<SWidget>& Widget, const FLinearColor& Color, const FString& Label, const FGeometry& OverlayGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId, float Thickness = 2.f) const;
	
	/** Draws a smooth Bezier curve connecting two widgets. */
	void DrawConnectionSpline(const FGeometry& AllottedGeometry, TSharedPtr<SWidget> Start, TSharedPtr<SWidget> End, EUINavigation Direction, FLinearColor Color, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;
	
	/** Draws a short directional indicator (stub) for blocked or void navigation. */
	void DrawDirectionalIndicator(const FGeometry& AllottedGeometry, const FGeometry& StartGeo, EUINavigation Direction, const FString& Label, const FLinearColor& Color, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	/** Draws text with a rounded semi-transparent background pill for readability. */
	void DrawTextLabelWithBackground(const FGeometry& AllottedGeometry, FVector2D Position, const FString& Text, const FLinearColor& Color, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

private:
	/** Pointer to the debug subsystem providing data */
	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem;
	
	/** The root overlay container that holds the draggable panels */
	TSharedPtr<SOverlay> RootOverlay;

	/** Child widgets for the overlay panels */
	TSharedPtr<SInputFlowLogView> LogView;

	TSharedPtr<SCommonUIHierarchyView> CommonUIHierarchyView;
	TSharedPtr<SEnhancedInputInspector> EnhancedInputInspectorView;
};