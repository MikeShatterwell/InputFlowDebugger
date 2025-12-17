// Copyright Mike Desrosiers, All Rights Reserved.

#include "SInputFlowOverlay.h"

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Input/HittestGrid.h>
#include <Styling/AppStyle.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>
#include <Fonts/FontMeasure.h>
#include <Widgets/Images/SImage.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "SCommonUIHierarchyView.h"
#include "SEnhancedInputInspector.h"
#include "SInputFlowLogView.h"

// --- CONSTANTS FOR STYLING ---
namespace InputFlowStyle
{
	constexpr FLinearColor Color_Focus = FLinearColor(0.2f, 0.8f, 0.2f); // Emerald
	constexpr FLinearColor Color_NavNormal = FLinearColor(1.0f, 0.7f, 0.2f); // Amber
	constexpr FLinearColor Color_NavHandled = FLinearColor(0.2f, 0.6f, 1.0f); // Soft Blue
	constexpr FLinearColor Color_NavBlocked = FLinearColor(1.0f, 0.3f, 0.3f); // Soft Red
	constexpr FLinearColor Color_Void = FLinearColor(0.5f, 0.5f, 0.5f); // Grey
	constexpr FLinearColor Color_LabelBg = FLinearColor(0.05f, 0.05f, 0.05f, 0.85f); // Dark Overlay

	static const FSlateBrush* GetBrush(FName BrushName)
	{
		return FCoreStyle::Get().GetBrush(BrushName);
	}
}

enum class EInputFlowPanelInteraction : uint8
{
	None,
	DragMove,
	ResizeRight,
	ResizeBottom,
	ResizeCorner
};

class SInputFlowDraggablePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowDraggablePanel)
		: _InitialSize(FVector2D(400, 300))
		, _VerticalAlignment(VAlign_Top)   // Default standard
		, _HorizontalAlignment(HAlign_Left) // Default standard
		{}
		SLATE_ARGUMENT(FVector2D, InitialSize)
		SLATE_ARGUMENT(EVerticalAlignment, VerticalAlignment)
		SLATE_ARGUMENT(EHorizontalAlignment, HorizontalAlignment)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CurrentSize = InArgs._InitialSize;
		VerticalAlignment = InArgs._VerticalAlignment;     // <--- Store It
		HorizontalAlignment = InArgs._HorizontalAlignment; // <--- Store It
		
		ChildSlot
		[
			SAssignNew(SizeBox, SBox)
			.WidthOverride(CurrentSize.X)
			.HeightOverride(CurrentSize.Y)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					InArgs._Content.Widget
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(0, 0, 2, 2)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("Window.ResizeHandle"))
					.ColorAndOpacity(FLinearColor(1, 1, 1, 0.5f))
					.Visibility(EVisibility::HitTestInvisible)
				]
			]
		];

		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			// Fix: Convert Absolute Screen Position to Local Widget Space for Hit Test
			FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			InteractionState = GetInteractionZone(MyGeometry, LocalPos);
			
			if (InteractionState != EInputFlowPanelInteraction::None)
			{
				DragStartAbsolute = MouseEvent.GetScreenSpacePosition();
				if (GetRenderTransform().IsSet())
				{
					InitialOffset = GetRenderTransform()->GetTranslation();
				}
				InitialSizeCapture = CurrentSize;
				return FReply::Handled().CaptureMouse(AsShared());
			}
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (InteractionState != EInputFlowPanelInteraction::None && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			InteractionState = EInputFlowPanelInteraction::None;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (InteractionState != EInputFlowPanelInteraction::None)
		{
			FVector2D DragDelta = MouseEvent.GetScreenSpacePosition() - DragStartAbsolute;
			float Scale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
			FVector2D LocalDelta = DragDelta / Scale;

			if (InteractionState == EInputFlowPanelInteraction::DragMove)
			{
				HandleMovement(LocalDelta);
			}
			else
			{
				HandleResize(LocalDelta);
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		// Fix: Cursor query also needs local space conversion
		FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
		EInputFlowPanelInteraction Zone = GetInteractionZone(MyGeometry, LocalPos);

		switch (Zone)
		{
		case EInputFlowPanelInteraction::ResizeCorner: return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		case EInputFlowPanelInteraction::ResizeRight:  return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		case EInputFlowPanelInteraction::ResizeBottom: return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		case EInputFlowPanelInteraction::DragMove:     return FCursorReply::Cursor(EMouseCursor::CardinalCross);
		default:                                       return FCursorReply::Unhandled();
		}
	}

private:
	void HandleMovement(const FVector2D& LocalDelta)
	{
		FVector2D NewOffset = InitialOffset + LocalDelta;
		NewOffset.X = FMath::Clamp(NewOffset.X, -3000.0f, 3000.0f);
		NewOffset.Y = FMath::Clamp(NewOffset.Y, -2000.0f, 2000.0f);

		CurrentOffset = NewOffset;
		SetRenderTransform(FSlateRenderTransform(FScale2D(1.0f), CurrentOffset));
	}

	void HandleResize(const FVector2D& LocalDelta)
	{
		FVector2D NewSize = InitialSizeCapture;

		// 1. Calculate New Size
		if (InteractionState == EInputFlowPanelInteraction::ResizeRight || InteractionState == EInputFlowPanelInteraction::ResizeCorner)
		{
			NewSize.X += LocalDelta.X;
		}
		if (InteractionState == EInputFlowPanelInteraction::ResizeBottom || InteractionState == EInputFlowPanelInteraction::ResizeCorner)
		{
			NewSize.Y += LocalDelta.Y;
		}

		NewSize.X = FMath::Max(NewSize.X, 200.0f);
		NewSize.Y = FMath::Max(NewSize.Y, 150.0f);

		// 2. Apply Size
		CurrentSize = NewSize;
		SizeBox->SetWidthOverride(CurrentSize.X);
		SizeBox->SetHeightOverride(CurrentSize.Y);

		// 3. Compensate for Layout Alignment
		// If we are bottom-aligned, growing height pushes the top UP. We want the top to stay fixed visually.
		// So we move the whole widget DOWN by the amount we grew.
		FVector2D SizeChange = CurrentSize - InitialSizeCapture;
		FVector2D Compensation = FVector2D::ZeroVector;

		// Vertical Compensation
		if (VerticalAlignment == VAlign_Bottom)
		{
			Compensation.Y = SizeChange.Y;
		}
		else if (VerticalAlignment == VAlign_Center)
		{
			Compensation.Y = SizeChange.Y * 0.5f;
		}

		// Horizontal Compensation (for right-aligned panel)
		if (HorizontalAlignment == HAlign_Right)
		{
			Compensation.X = SizeChange.X;
		}
		else if (HorizontalAlignment == HAlign_Center)
		{
			Compensation.X = SizeChange.X * 0.5f;
		}

		// 4. Apply New Transform (Initial + Compensation)
		CurrentOffset = InitialOffset + Compensation;
		SetRenderTransform(FSlateRenderTransform(FScale2D(1.0f), CurrentOffset));
	}

	EInputFlowPanelInteraction GetInteractionZone(const FGeometry& Geometry, const FVector2D& LocalMousePos) const
	{
		const float GripSize = 15.0f;
		const FVector2D Size = Geometry.GetLocalSize();
		
		// Expanded hit test slightly
		if (LocalMousePos.X < 0 || LocalMousePos.Y < 0 || LocalMousePos.X > Size.X + 5 || LocalMousePos.Y > Size.Y + 5)
		{
			return EInputFlowPanelInteraction::None;
		}

		bool bInRight = (LocalMousePos.X >= Size.X - GripSize);
		bool bInBottom = (LocalMousePos.Y >= Size.Y - GripSize);

		if (bInRight && bInBottom) return EInputFlowPanelInteraction::ResizeCorner;
		if (bInRight) return EInputFlowPanelInteraction::ResizeRight;
		if (bInBottom) return EInputFlowPanelInteraction::ResizeBottom;
		
		return EInputFlowPanelInteraction::DragMove;
	}

	EInputFlowPanelInteraction InteractionState = EInputFlowPanelInteraction::None;
	
	// Settings
	EVerticalAlignment VerticalAlignment = EVerticalAlignment::VAlign_Top;
	EHorizontalAlignment HorizontalAlignment = EHorizontalAlignment::HAlign_Left;

	// Transform Data
	FVector2D DragStartAbsolute = FVector2D::ZeroVector;
	FVector2D InitialOffset = FVector2D::ZeroVector;
	FVector2D CurrentOffset = FVector2D::ZeroVector;
	FVector2D InitialSizeCapture = FVector2D::ZeroVector;
	FVector2D CurrentSize = FVector2D::ZeroVector;

	TSharedPtr<SBox> SizeBox;
};

void SInputFlowOverlay::Construct(const FArguments& InArgs)
{
	DebugSubsystem = InArgs._Subsystem;
	UInputDebugSubsystem* Sub = DebugSubsystem.Get();
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	SetVisibility(EVisibility::SelfHitTestInvisible);

	// Visibility Delegate for Panels
	auto GetPanelVisibility = [this]() -> EVisibility
	{
		if (DebugSubsystem.IsValid() && DebugSubsystem->GetShowOverlayPanels())
		{
			return EVisibility::Visible;
		}
		return EVisibility::Collapsed;
	};

	ChildSlot
	[
		SNew(SOverlay)

		// --- LEFT PANEL: EVENT LOGS ---
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(10.0f, 10.0f, 10.0f, 50.0f)
		[
			SNew(SInputFlowDraggablePanel)
			.InitialSize(FVector2D(500, 300))
			.VerticalAlignment(VAlign_Bottom)
			.HorizontalAlignment(HAlign_Left)
			.Visibility_Lambda(GetPanelVisibility)
			[
				SNew(SBorder)
				.BorderImage(InputFlowStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
				.Padding(10.0f)
				[
					SAssignNew(LogView, SInputFlowLogView, Sub)
					.IsOverlay(true)
				]
			]
		]

		// --- RIGHT PANEL: ANALYZER DATA ---
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SInputFlowDraggablePanel)
			.InitialSize(FVector2D(400, 500))
			.VerticalAlignment(VAlign_Center)
			.HorizontalAlignment(HAlign_Right)
			.Visibility_Lambda(GetPanelVisibility)
			[
				SNew(SBorder)
				.BorderImage(InputFlowStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
				.Padding(10.0f)
				[
					SNew(SVerticalBox)

					// Hierarchy
					+ SVerticalBox::Slot().FillHeight(0.6f).Padding(0, 0, 0, 10)
					[
						SAssignNew(HierarchyView, SCommonUIHierarchyView, Sub)
						.IsOverlay(true)
					]

					// Enhanced Input
					+ SVerticalBox::Slot().FillHeight(0.4f)
					[
						SAssignNew(InspectorView, SEnhancedInputInspector, Sub)
						.IsOverlay(true)
					]
				]
			]
		]
	];
}

UInputDebugSubsystem* SInputFlowOverlay::GetSubsystem() const
{
	return DebugSubsystem.Get();
}

int32 SInputFlowOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
								 const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
								 int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Clear the label batch at the start of the frame
	LabelBatch.Reset();

	int32 ChildLayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	PaintNavigationSimulation(AllottedGeometry, OutDrawElements, LayerId);
	PaintFocusHistory(AllottedGeometry, OutDrawElements, LayerId);
	PaintHitTestGrid(AllottedGeometry, OutDrawElements, LayerId);

	// Resolve collisions, clamp to viewport, and draw the batch
	ResolveAndDrawLabels(AllottedGeometry, OutDrawElements, LayerId + 20); // Draw on top of everything

	return ChildLayerId;
}

void SInputFlowOverlay::ResolveAndDrawLabels(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (LabelBatch.IsEmpty()) return;

	const FVector2D ScreenSize = AllottedGeometry.GetLocalSize();
	const FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 9);

	// Hard padding between labels (The Gutter)
	const FVector2D Gutter(4.0f, 4.0f);

	// --- PHYSICS SOLVER ---
	constexpr int32 SolverIterations = 32;

	for (int32 Iter = 0; Iter < SolverIterations; ++Iter)
	{
		bool bAnyMoved = false;

		for (int32 i = 0; i < LabelBatch.Num(); ++i)
		{
			for (int32 j = i + 1; j < LabelBatch.Num(); ++j)
			{
				FPendingLabel& A = LabelBatch[i];
				FPendingLabel& B = LabelBatch[j];

				// Calculate effective bounds including Gutter
				// We use centers and half-extents for AABB intersection
				FVector2D SizeA = A.Size + Gutter;
				FVector2D SizeB = B.Size + Gutter;
				FVector2D CenterA = A.CurrentPos + (A.Size * 0.5f);
				FVector2D CenterB = B.CurrentPos + (B.Size * 0.5f);

				FVector2D Delta = CenterA - CenterB;
				FVector2D AbsDelta = FVector2D(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
				
				// Calculate Half Extents
				FVector2D HalfExtentsA = SizeA * 0.5f;
				FVector2D HalfExtentsB = SizeB * 0.5f;

				// Calculate Penetration (How much are we overlapping?)
				FVector2D Overlap;
				Overlap.X = (HalfExtentsA.X + HalfExtentsB.X) - AbsDelta.X;
				Overlap.Y = (HalfExtentsA.Y + HalfExtentsB.Y) - AbsDelta.Y;

				// If Overlap > 0 on BOTH axes, we have a collision
				if (Overlap.X > 0.0f && Overlap.Y > 0.0f)
				{
					// Resolve Collision
					// We want to push along the axis of *least* penetration (the shortest path out),
					// BUT we bias towards Y (Vertical) because text lists read better top-to-bottom.

					// Heuristic: Only push horizontally if the horizontal overlap is significantly 
					// smaller than vertical (meaning they are ALMOST side-by-side already).
					// Otherwise, force them to stack vertically.
					bool bResolveX = Overlap.X < (Overlap.Y * 0.5f); 

					FVector2D PushAmount = FVector2D::ZeroVector;

					if (bResolveX)
					{
						// Push horizontally
						// Determine direction based on where they are relative to each other
						float SignX = (Delta.X > 0.0f) ? 1.0f : -1.0f;
						PushAmount.X = Overlap.X * SignX; 
					}
					else
					{
						// Push vertically (Default behavior for UI)
						float SignY = (Delta.Y > 0.0f) ? 1.0f : -1.0f;
						
						// Fallback: If they are exactly on top of each other (Delta.Y == 0), push B down
						if (FMath::IsNearlyZero(Delta.Y)) SignY = 1.0f;
						
						PushAmount.Y = Overlap.Y * SignY;
					}

					// 3. Apply the forces
					// We split the move 50/50 between the two boxes
					FVector2D HalfPush = PushAmount * 0.5f;
					
					A.CurrentPos += HalfPush;
					B.CurrentPos -= HalfPush;
					
					bAnyMoved = true;
				}
			}
		}

		// --- CLAMPING PASS ---
		// We clamp immediately after every resolution pass. 
		// This effectively turns the screen edges into immovable walls.
		for (FPendingLabel& Label : LabelBatch)
		{
			Label.CurrentPos.X = FMath::Clamp(Label.CurrentPos.X, 0.0f, ScreenSize.X - Label.Size.X);
			Label.CurrentPos.Y = FMath::Clamp(Label.CurrentPos.Y, 0.0f, ScreenSize.Y - Label.Size.Y);
		}

		// Optimization: If nothing collided or moved this frame, we are stable.
		if (!bAnyMoved) break;
	}

	// --- DRAWING PASS ---
	for (const FPendingLabel& Label : LabelBatch)
	{
		// 1. Draw Connector Line if we moved significantly
		// Threshold: 10 pixels squared (100.0f) to avoid jittery lines for tiny adjustments
		if (FVector2D::DistSquared(Label.OriginalPos, Label.CurrentPos) > 100.0f)
		{
			TArray<FVector2D> LinePoints;
			
			// Start roughly near the widget
			LinePoints.Add(Label.OriginalPos + FVector2D(0.0f, Label.Size.Y * 0.5f)); 
			
			// Connect to the nearest side of the NEW label position
			FVector2D BoxCenter = Label.CurrentPos + (Label.Size * 0.5f);
			
			// Simple visual improvement: Curve the line slightly? 
			// For now, straight line to center is cleanest for debug.
			LinePoints.Add(BoxCenter);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				Label.Color.CopyWithNewOpacity(0.5f), // 50% opacity for connectors
				true,
				1.0f 
			);
			
			// Draw a small dot at the original location so we know exactly what is being labeled
			FPaintGeometry OriginDot = AllottedGeometry.ToPaintGeometry(
				UE::Slate::CastToVector2f(FVector2D(4, 4)),
				FSlateLayoutTransform(UE::Slate::CastToVector2f(Label.OriginalPos + FVector2D(-2, (Label.Size.Y * 0.5f) - 2)))
			);
			
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				OriginDot,
				InputFlowStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				Label.Color
			);
		}

		// 2. Draw Background Box
		FPaintGeometry BoxGeo = AllottedGeometry.ToPaintGeometry(
			UE::Slate::CastToVector2f(Label.Size),
			FSlateLayoutTransform(UE::Slate::CastToVector2f(Label.CurrentPos))
		);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			BoxGeo,
			InputFlowStyle::GetBrush("ChildWindow.Background"),
			ESlateDrawEffect::None,
			InputFlowStyle::Color_LabelBg
		);

		// 3. Draw Text
		FVector2D Padding(8.0f, 4.0f);
		FPaintGeometry TextGeo = AllottedGeometry.ToPaintGeometry(
			UE::Slate::CastToVector2f(Label.Size - (Padding * 2.0f)),
			FSlateLayoutTransform(UE::Slate::CastToVector2f(Label.CurrentPos + Padding))
		);

		// Drop Shadow
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			TextGeo,
			Label.Text,
			FontInfo,
			ESlateDrawEffect::None,
			FLinearColor::Black
		);

		// Main Text
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 3,
			TextGeo,
			Label.Text,
			FontInfo,
			ESlateDrawEffect::None,
			Label.Color
		);
	}
}
void SInputFlowOverlay::PaintNavigationSimulation(const FGeometry& AllottedGeometry,
												  FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	UInputDebugSubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->GetNavigationSimulationEnabled()) return;

	// --- Draw Active Focus ---
	if (TSharedPtr<SWidget> Focus = Sub->GetFocusedWidget())
	{
		constexpr float Thickness = 4.f;
		int32 FocusLayerId = LayerId + 10; // Draw on top of other highlights

		FString FocusLabel = FString::Printf(TEXT("FOCUS: %s"), *InputFlowHelpers::GetWidgetDisplayName(Focus));
		DrawWidgetHighlight(Focus, InputFlowStyle::Color_Focus, FocusLabel, AllottedGeometry, OutDrawElements, FocusLayerId, Thickness);

		const FGeometry& FocusGeo = Focus->GetPaintSpaceGeometry();
		// Ensure widget has valid size to avoid divide-by-zero or nan issues
		if (FocusGeo.GetAbsoluteSize().GetMin() > 0.0f)
		{
			// Calculate Center in Overlay Local Space
			FVector2D Center = AllottedGeometry.AbsoluteToLocal(
				FocusGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

			// A. Draw a filled "dot" (Technically a box, but small enough to look like a point)
			constexpr float DotRadius = 3.0f;
			FPaintGeometry DotGeo = AllottedGeometry.ToPaintGeometry(
				UE::Slate::CastToVector2f(FVector2D(DotRadius * 2, DotRadius * 2)),
				FSlateLayoutTransform(UE::Slate::CastToVector2f(Center - FVector2D(DotRadius, DotRadius)))
			);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 10, // Draw on top of labels
				DotGeo,
				InputFlowStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				InputFlowStyle::Color_Focus
			);

			// B. Draw a Ring around it (To make it actually look like a Circle)
			TArray<FVector2D> RingPoints;
			constexpr int32 Segments = 16;
			constexpr float RingRadius = 6.0f;

			for (int32 i = 0; i <= Segments; ++i)
			{
				float Angle = ((float)i / (float)Segments) * 2.0f * PI;
				RingPoints.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RingRadius);
			}

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 10,
				AllottedGeometry.ToPaintGeometry(),
				RingPoints,
				ESlateDrawEffect::None,
				InputFlowStyle::Color_Focus,
				true, // Anti-alias
				3.f // Thickness
			);
		}
	}

	// --- Draw Navigation Links ---
	const TArray<FNavigationLink>& Links = Sub->GetNavigationLinks();
	const int32 MaxDepth = Sub->GetNavigationDepth();

	for (const FNavigationLink& Link : Links)
	{
		TSharedPtr<SWidget> Start = Link.StartWidget.Pin();
		TSharedPtr<SWidget> End = Link.EndWidget.Pin();

		if (!Start) continue;

		// Calculate Opacity based on depth
		float Alpha = (MaxDepth == 1) ? 1.0f : FMath::Clamp(1.5f - ((float)Link.DepthStep / MaxDepth), 0.2f, 1.0f);

		// --- CASE 1: SUCCESS (Normal Navigation) ---
		if (Link.ResultType == ENavSimResult::Normal && End.IsValid())
		{
			FLinearColor LinkColor = InputFlowStyle::Color_NavNormal.CopyWithNewOpacity(Alpha);
			FString EndName = InputFlowHelpers::GetWidgetDisplayName(End);
			FString Label = FString::Printf(TEXT("%s Target\n%s"), *UEnum::GetValueAsString(Link.Direction), *EndName);

			// Highlight the Target
			DrawWidgetHighlight(End, LinkColor, Label, AllottedGeometry, OutDrawElements, LayerId);

			// Draw full spline connection
			DrawConnectionSpline(AllottedGeometry, Start, End, Link.Direction, LinkColor, OutDrawElements, LayerId);
		}

		// --- CASE 2: BLOCKED / HANDLED (Draw Stub) ---
		else
		{
			bool bIsStopped = (Link.ResultType == ENavSimResult::Stopped);
			FLinearColor Color = (bIsStopped ? InputFlowStyle::Color_NavBlocked : InputFlowStyle::Color_NavHandled).
				CopyWithNewOpacity(Alpha);
			FString BlockerName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");
			FString StatusLabel = FString::Printf(TEXT("%s %s\n%s"), *UEnum::GetValueAsString(Link.Direction), bIsStopped ? TEXT("BLOCKED By") : TEXT("HANDLED By"),
											  *BlockerName);

			const FGeometry& StartGeo = Start->GetPaintSpaceGeometry();
			DrawDirectionalIndicator(AllottedGeometry, StartGeo, Link.Direction, StatusLabel, Color, OutDrawElements,
				LayerId);
		}
	}
}

void SInputFlowOverlay::PaintFocusHistory(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
										  int32& LayerId) const
{
	UInputDebugSubsystem* Subsystem = GetSubsystem();
	if (!IsValid(Subsystem)) return;

	const TArray<FFocusHistoryEntry>& History = Subsystem->GetFocusHistory();
	if (History.IsEmpty()) return;

	double Now = FPlatformTime::Seconds();
	constexpr double MaxAge = 1.0f;

	TSharedPtr<SWidget> LastCenterWidget = nullptr;
	FVector2D LastCenterPos = FVector2D::ZeroVector;

	// Iterate from oldest to newest
	for (const FFocusHistoryEntry& Entry : History)
	{
		double Age = Now - Entry.Timestamp;
		if (Age > MaxAge) continue;

		TSharedPtr<SWidget> Widget = Entry.Widget.Pin();
		if (!Widget.IsValid()) continue;
		
		// Alpha calculation
		float NormalizedAge = (float)(Age / MaxAge);
		float Alpha = 1.0f - NormalizedAge;
		FLinearColor Color = InputFlowStyle::Color_Focus.CopyWithNewOpacity(Alpha * 0.5f);

		const FGeometry& WidgetGeo = Widget->GetPaintSpaceGeometry();
		if (WidgetGeo.GetAbsoluteSize().IsZero()) continue;

		// Draw subtle highlight box
		DrawWidgetHighlight(Widget, Color, FString(), AllottedGeometry, OutDrawElements, LayerId);

		// Center calculations
		FVector2D CurrentCenter = AllottedGeometry.AbsoluteToLocal(
			WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

		// Draw Path
		if (LastCenterWidget.IsValid())
		{
			TArray<FVector2D> Points;
			Points.Add(LastCenterPos);
			Points.Add(CurrentCenter);

			// Draw Dashed Line for history
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				Color,
				true,
				2.0f
			);
		}

		// Draw small dot at center
		const float CircleRadius = 3.0f;
		FPaintGeometry CircleGeo = AllottedGeometry.ToPaintGeometry(
			FVector2D(CircleRadius * 2, CircleRadius * 2),
			FSlateLayoutTransform(UE::Slate::CastToVector2f(CurrentCenter - FVector2D(CircleRadius, CircleRadius)))
		);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			CircleGeo,
			InputFlowStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			InputFlowStyle::Color_Focus.CopyWithNewOpacity(Alpha)
		);

		LastCenterWidget = Widget;
		LastCenterPos = CurrentCenter;
	}
}

void SInputFlowOverlay::PaintHitTestGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
										 int32& LayerId) const
{
	UInputDebugSubsystem* Subsystem = GetSubsystem();
	if (!IsValid(Subsystem) || !Subsystem->GetShowHitTestGrid()) return;

#if WITH_SLATE_DEBUGGING
	TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!MyWindow.IsValid()) return;

	const FHittestGrid& Grid = MyWindow->GetHittestGrid();
	TArray<FHittestGrid::FWidgetSortData> SortData = Grid.GetAllWidgetSortDatas();

	const FSlateBrush* FillBrush = InputFlowStyle::GetBrush("WhiteBrush");
	const FSlateBrush* BorderBrush = InputFlowStyle::GetBrush("Debug.Border");

	// Nice Cyan/Teal
	const FLinearColor FillColor(0.0f, 1.0f, 0.9f, 0.03f);
	const FLinearColor BorderColor(0.0f, 1.0f, 0.9f, 0.3f);

	for (const FHittestGrid::FWidgetSortData& Item : SortData)
	{
		TSharedPtr<SWidget> Widget = Item.WeakWidget.Pin();
		if (Widget.IsValid())
		{
			if (!Widget->GetVisibility().IsHitTestVisible()) continue;

			const FGeometry& WidgetGeo = Widget->GetPaintSpaceGeometry();
			if (WidgetGeo.GetAbsoluteSize().IsZero()) continue;

			FVector2D TopLeft(AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePosition()));
			FVector2D BottomRight(
				AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f))));
			FVector2D Size = BottomRight - TopLeft;

			FPaintGeometry PaintGeo = AllottedGeometry.ToPaintGeometry(
				UE::Slate::CastToVector2f(Size),
				FSlateLayoutTransform(UE::Slate::CastToVector2f(TopLeft))
			);

			// Draw Fill
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				PaintGeo,
				FillBrush,
				ESlateDrawEffect::None,
				FillColor
			);

			// Draw Border
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				PaintGeo,
				BorderBrush,
				ESlateDrawEffect::None,
				BorderColor
			);
		}
	}
#endif
}

// --- VISUAL HELPERS ---

void SInputFlowOverlay::DrawWidgetHighlight(const TSharedPtr<SWidget>& Widget, const FLinearColor& Color,
											const FString& Label, const FGeometry& OverlayGeometry,
											FSlateWindowElementList& OutDrawElements, int32& LayerId,
											const float Thickness) const
{
	if (!Widget.IsValid() || !DebugSubsystem.IsValid()) return;

	const FGeometry& WidgetGeo = Widget->GetPaintSpaceGeometry();
	if (WidgetGeo.GetAbsoluteSize().IsZero()) return;

	FVector2D TopLeft(OverlayGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePosition()));
	FVector2D BottomRight(
		OverlayGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f))));
	FVector2D Size = BottomRight - TopLeft;

	FPaintGeometry PaintGeo = OverlayGeometry.ToPaintGeometry(
		UE::Slate::CastToVector2f(Size),
		FSlateLayoutTransform(UE::Slate::CastToVector2f(TopLeft))
	);

	// 1. Semi-transparent Fill
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		PaintGeo,
		InputFlowStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		Color.CopyWithNewOpacity(Color.A * 0.15f)
	);

	// 2. Strong Border
	TArray<FVector2D> LinePoints = {
		FVector2D::ZeroVector, FVector2D(Size.X, 0), FVector2D(Size.X, Size.Y), FVector2D(0, Size.Y),
		FVector2D::ZeroVector
	};

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 1,
		PaintGeo,
		LinePoints,
		ESlateDrawEffect::None,
		Color,
		true,
		Thickness
	);

	// 3. Draw Label with Background Pill
	if (!Label.IsEmpty())
	{
		DrawTextLabelWithBackground(OverlayGeometry, TopLeft + FVector2D(0, -22), Label, Color, OutDrawElements,
									LayerId);
	}
}

void SInputFlowOverlay::DrawTextLabelWithBackground(const FGeometry& AllottedGeometry, FVector2D Position,
													const FString& Text, const FLinearColor& Color,
													FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	const FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Measure the text and calculate box size
	FVector2D TextSize = FontMeasure->Measure(Text, FontInfo);
	FVector2D Padding(8.0f, 4.0f);
	FVector2D BoxSize = TextSize + (Padding * 2.0f);

	// Defer the drawing. Store all necessary data in the batch.
	FPendingLabel NewLabel;
	NewLabel.OriginalPos = Position;
	NewLabel.CurrentPos = Position;
	NewLabel.Size = BoxSize;
	NewLabel.Text = Text;
	NewLabel.Color = Color;
	NewLabel.LayerId = LayerId;

	LabelBatch.Add(NewLabel);
}

void SInputFlowOverlay::DrawConnectionSpline(const FGeometry& AllottedGeometry, TSharedPtr<SWidget> Start,
											 TSharedPtr<SWidget> End, EUINavigation Direction, FLinearColor Color,
											 FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	const FGeometry& StartGeo = Start->GetPaintSpaceGeometry();
	const FGeometry& EndGeo = End->GetPaintSpaceGeometry();
	if (StartGeo.GetAbsoluteSize().IsZero() || EndGeo.GetAbsoluteSize().IsZero()) return;

	FVector2D StartP = AllottedGeometry.AbsoluteToLocal(
		StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
	FVector2D EndP = AllottedGeometry.AbsoluteToLocal(EndGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

	// Calculate Control Points for Bezier Curve
	FVector2D Delta = EndP - StartP;
	float Dist = Delta.Size();
	float ControlDist = FMath::Max(Dist * 0.5f, 50.0f);

	FVector2D StartTangent(0, 0);

	switch (Direction)
	{
	case EUINavigation::Left: StartTangent = FVector2D(-1, 0);
		break;
	case EUINavigation::Right: StartTangent = FVector2D(1, 0);
		break;
	case EUINavigation::Up: StartTangent = FVector2D(0, -1);
		break;
	case EUINavigation::Down: StartTangent = FVector2D(0, 1);
		break;
	default: break;
	}

	FVector2D CP0 = StartP;
	FVector2D CP1 = StartP + (StartTangent * ControlDist);
	FVector2D CP3 = EndP;
	// End Tangent opposes entry usually looks best for UI flow
	FVector2D CP2 = EndP - (StartTangent * (ControlDist * 0.5f));

	// Generate Curve Points
	TArray<FVector2D> Points;
	const int32 Segments = 20;
	for (int32 i = 0; i <= Segments; ++i)
	{
		float T = (float)i / (float)Segments;
		float OneMinusT = 1.0f - T;

		// Cubic Bezier
		FVector2D P = (OneMinusT * OneMinusT * OneMinusT) * CP0 +
			(3.0f * OneMinusT * OneMinusT * T) * CP1 +
			(3.0f * OneMinusT * T * T) * CP2 +
			(T * T * T) * CP3;
		Points.Add(P);
	}

	// Draw Curve
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		Color,
		true,
		3.0f
	);

	// Draw Arrowhead at the end
	FVector2D Dir = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
	FVector2D Tangent = FVector2D(-Dir.Y, Dir.X);
	float ArrowSize = 12.0f;
	TArray<FVector2D> HeadPoints;
	HeadPoints.Add(EndP + Dir * 2.0f); // Slight nudge forward
	HeadPoints.Add(EndP - Dir * ArrowSize + Tangent * (ArrowSize * 0.6f));
	HeadPoints.Add(EndP - Dir * ArrowSize - Tangent * (ArrowSize * 0.6f));
	HeadPoints.Add(EndP + Dir * 2.0f); // Close loop

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 3,
		AllottedGeometry.ToPaintGeometry(),
		HeadPoints,
		ESlateDrawEffect::None,
		Color,
		true,
		3.0f
	);

	// Fill Arrowhead
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(FVector2D(1, 1)),
										 FSlateLayoutTransform(UE::Slate::CastToVector2f(EndP))), // Just to set state
		InputFlowStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		Color
	);
}

void SInputFlowOverlay::DrawDirectionalIndicator(const FGeometry& AllottedGeometry, const FGeometry& StartGeo,
												 EUINavigation Direction, const FString& Label,
												 const FLinearColor& Color, FSlateWindowElementList& OutDrawElements,
												 int32& LayerId) const
{
	if (StartGeo.GetAbsoluteSize().IsZero()) return;

	FVector2D StartTL = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePosition());
	FVector2D StartBR = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(1, 1)));
	FVector2D Center = (StartTL + StartBR) * 0.5f;

	FVector2D Anchor, DirVec;

	switch (Direction)
	{
	case EUINavigation::Left: Anchor = FVector2D(StartTL.X, Center.Y);
		DirVec = FVector2D(-1, 0);
		break;
	case EUINavigation::Right: Anchor = FVector2D(StartBR.X, Center.Y);
		DirVec = FVector2D(1, 0);
		break;
	case EUINavigation::Up: Anchor = FVector2D(Center.X, StartTL.Y);
		DirVec = FVector2D(0, -1);
		break;
	case EUINavigation::Down: Anchor = FVector2D(Center.X, StartBR.Y);
		DirVec = FVector2D(0, 1);
		break;
	default: return;
	}

	float LineLength = 30.0f;
	FVector2D EndPos = Anchor + (DirVec * LineLength);

	// Draw Line
	TArray<FVector2D> Points = {Anchor, EndPos};
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		Color,
		true,
		3.0f
	);

	// Draw "Stop" bar
	FVector2D Tangent(-DirVec.Y, DirVec.X);
	FVector2D BarStart = EndPos + (Tangent * 8.0f);
	FVector2D BarEnd = EndPos - (Tangent * 8.0f);

	TArray<FVector2D> BarPoints = {BarStart, BarEnd};
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(),
		BarPoints,
		ESlateDrawEffect::None,
		Color,
		true,
		3.0f
	);

	// Draw Label with backing
	DrawTextLabelWithBackground(AllottedGeometry, EndPos + FVector2D(5, 5), Label, Color, OutDrawElements, LayerId);
}
