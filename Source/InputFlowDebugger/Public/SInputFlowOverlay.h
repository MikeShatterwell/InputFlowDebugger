// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Types/SlateEnums.h>

class UInputDebugSubsystem;
class SInputFlowLogView;
class SCommonUIHierarchyView;
class SEnhancedInputInspector;
class SOverlay;
class SDPIScaler;
class SCanvas;
class STextBlock;
class SBorder;

/*
 * A simple label widget with a colored background for use in the overlay.
 */
class SInputFlowLabel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetData(const FString& Text, const FLinearColor& Color);

private:
	TSharedPtr<STextBlock> TextBlock;
	TSharedPtr<SBorder> BackgroundBorder;
};

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

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** SWidget Interface */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Helper to access the subsystem */
	UInputDebugSubsystem* GetSubsystem() const;

private:
	/** Renders the highlight ring/box for the currently focused widget. */
	void PaintFocusedWidget(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;
	
	/** Renders the predicted navigation paths (arrows/splines from start to target widgets). */
	void PaintNavigationSimulation(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	/** Renders the trail of previously focused widgets (ghost trail). */
	void PaintFocusHistory(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	/** Renders the hit-testable widget grid for debugging mouse clicks. */
	void PaintHitTestGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	// --- Visual Primitives ---

	static void DrawCircle(const FGeometry& AllottedGeometry, const FVector2D& Center, float Radius, const FLinearColor& Color,
							float Thickness, FSlateWindowElementList& OutDrawElements, int32 LayerId);

	/** Draws a box around a widget with a label. */
	void DrawWidgetHighlight(const TSharedPtr<SWidget>& Widget, const FLinearColor& Color, const FString& Label, const FGeometry& OverlayGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId, float Thickness = 2.f) const;
	
	/** Draws a smooth Bezier curve connecting two widgets. */
	void DrawConnectionSpline(const FGeometry& AllottedGeometry, TSharedPtr<SWidget> Start, TSharedPtr<SWidget> End, const FString& Label, EUINavigation Direction, FLinearColor Color, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;
	
	/** Draws a short directional indicator (stub) for blocked or void navigation. */
	void DrawDirectionalIndicator(const FGeometry& AllottedGeometry, const FGeometry& StartGeo, EUINavigation Direction, const FString& Label, const FLinearColor& Color, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	void QueueLabel(const FVector2D& Position, const FString& Text, const FLinearColor& Color, const FVector2D& Pivot = FVector2D::ZeroVector) const;
	void GatherLabelsFromSubsystem(const FGeometry& AllottedGeometry);
	void UpdateLabelCanvas(const FGeometry& AllottedGeometry);

private:
	/** Pointer to the debug subsystem providing data */
	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem;
	
	/** The root overlay container that holds the draggable panels */
	TSharedPtr<SOverlay> RootOverlay;
	TSharedPtr<SDPIScaler> MainDPIScaler;

	// --- Label Management ---
	TSharedPtr<SCanvas> LabelCanvas;
	TArray<TSharedPtr<SInputFlowLabel>> ActiveLabels;
	TArray<TSharedPtr<SInputFlowLabel>> LabelPool;

	struct FQueuedLabel
	{
		FVector2D OriginalPos;
		FString Text;
		FLinearColor Color;
		FVector2D Pivot; // Pivot for alignment (0.5, 1.0 means bottom-middle is at OriginalPos)
	};
	mutable TArray<FQueuedLabel> QueuedLabels; // Mutable because QueueLabel can be called from const methods. 
	
	// --- Panel Views ---

	/** Child widgets for the overlay panels */
	TSharedPtr<SInputFlowLogView> LogView;
	TSharedPtr<SCommonUIHierarchyView> CommonUIHierarchyView;
	TSharedPtr<SEnhancedInputInspector> EnhancedInputInspectorView;
};