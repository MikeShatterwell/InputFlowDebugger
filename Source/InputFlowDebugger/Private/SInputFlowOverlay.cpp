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
#include "SInputFlowAnalyzer.h" 

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

// --------------------------------------------------------------------
// ENHANCED DRAGGABLE PANEL WITH SNAPPING & ANCHORING
// --------------------------------------------------------------------

DECLARE_DELEGATE_RetVal_OneParam(TArray<FSlateRect>, FOnGetSnapTargets, const SWidget* /*Requestor*/);

class SInputFlowDraggablePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowDraggablePanel)
		: _InitialPosition(FVector2D(100, 100))
		, _InitialSize(FVector2D(400, 300))
		, _Title(TEXT("Panel"))
		{}
		SLATE_ARGUMENT(FVector2D, InitialPosition)
		SLATE_ARGUMENT(FVector2D, InitialSize)
		SLATE_ARGUMENT(FString, Title)
		SLATE_EVENT(FOnGetSnapTargets, OnGetSnapTargets)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CurrentSize = InArgs._InitialSize;
		GetSnapTargetsDelegate = InArgs._OnGetSnapTargets;
		
		// Store requested position; we apply it once we have geometry to determine the best anchor
		PendingInitialPosition = InArgs._InitialPosition;

		ChildSlot
		[
			SAssignNew(SizeBox, SBox)
			.WidthOverride(CurrentSize.X)
			.HeightOverride(CurrentSize.Y)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder")) 
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
				.Padding(0)
				[
					SNew(SVerticalBox)
					
					// Header / Title Bar (Draggable Area)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f, 1.0f))
						.Padding(FMargin(8, 4))
						[
							SNew(STextBlock)
							.Text(FText::FromString(InArgs._Title))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f))
						]
					]

					// Content
					+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4)
					[
						InArgs._Content.Widget
					]

					// Resize Handle
					+ SVerticalBox::Slot().AutoHeight().VAlign(VAlign_Bottom).HAlign(HAlign_Right).Padding(0, 0, 1, 1)
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("Window.ResizeHandle"))
						.ColorAndOpacity(FLinearColor(1, 1, 1, 0.5f))
						.Visibility(EVisibility::HitTestInvisible)
					]
				]
			]
		];

		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
	}

	void SetOverlaySlot(SOverlay::FOverlaySlot* InSlot)
	{
		OverlaySlot = InSlot;
	}

	// Helper to get the rect based on current visual state for OTHER panels to snap to
	FSlateRect GetComputedRect(const FVector2D& ParentSize) const
	{
		FVector2D LayoutPos = GetLayoutPosition(ParentSize, OverlaySlot ? OverlaySlot->GetHorizontalAlignment() : HAlign_Left, OverlaySlot ? OverlaySlot->GetVerticalAlignment() : VAlign_Top);
		if (GetRenderTransform().IsSet())
		{
			FVector2D VisualPos = LayoutPos + GetRenderTransform()->GetTranslation();
			return FSlateRect(VisualPos.X, VisualPos.Y, VisualPos.X + CurrentSize.X, VisualPos.Y + CurrentSize.Y);
		}
		return FSlateRect(LayoutPos.X, LayoutPos.Y, LayoutPos.X + CurrentSize.X, LayoutPos.Y + CurrentSize.Y);
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		// One-time initialization of anchor based on InitialPosition
		if (!bAnchorInitialized && OverlaySlot)
		{
			TSharedPtr<SWidget> Parent = GetParentWidget();
			if (Parent.IsValid())
			{
				FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
				if (ParentSize.X > 1.0f) // Wait for valid layout
				{
					UpdateAnchorAndOffset(PendingInitialPosition, ParentSize);
					bAnchorInitialized = true;
				}
			}
		}
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			FVector2D Size = MyGeometry.GetLocalSize();

			// Resize Zone (Bottom Right corner, 20x20)
			if (LocalMouse.X > Size.X - 20 && LocalMouse.Y > Size.Y - 20)
			{
				bResizing = true;
				DragStartMousePos = MouseEvent.GetScreenSpacePosition();
				InitialDragSize = CurrentSize;
				
				// Capture current visual state for resizing compensation
				// This ensures we know where the Top-Left was BEFORE resizing started
				TSharedPtr<SWidget> Parent = GetParentWidget();
				if (Parent && OverlaySlot)
				{
					FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
					FVector2D LayoutPos = GetLayoutPosition(ParentSize, OverlaySlot->GetHorizontalAlignment(), OverlaySlot->GetVerticalAlignment());
					if (GetRenderTransform().IsSet())
					{
						DragStartVisualPos = LayoutPos + GetRenderTransform()->GetTranslation();
						return FReply::Handled().CaptureMouse(AsShared());
					}
				}
			}
			
			// Drag Zone (Title Bar - Top 28px)
			if (LocalMouse.Y < 28)
			{
				bDragging = true;
				DragStartMousePos = MouseEvent.GetScreenSpacePosition();
				
				// Capture current visual state
				TSharedPtr<SWidget> Parent = GetParentWidget();
				if (Parent)
				{
					FGeometry ParentGeo = Parent->GetPaintSpaceGeometry();
					FVector2D ParentSize = ParentGeo.GetLocalSize();
					
					// Calculate current visual top-left relative to parent
					FVector2D LayoutPos = GetLayoutPosition(ParentSize, OverlaySlot->GetHorizontalAlignment(), OverlaySlot->GetVerticalAlignment());
					if (GetRenderTransform().IsSet())
					{
						DragStartVisualPos = LayoutPos + GetRenderTransform()->GetTranslation();
					}
					else
					{
						DragStartVisualPos = LayoutPos;
					}
				}
				
				return FReply::Handled().CaptureMouse(AsShared());
			}
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging || bResizing)
		{
			bDragging = false;
			bResizing = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging || bResizing)
		{
			if (bDragging)
			{
				const float Scale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
				const FVector2D MouseDelta = (MouseEvent.GetScreenSpacePosition() - DragStartMousePos) / Scale;
				
				// Raw new visual position
				FVector2D NewVisualPos = DragStartVisualPos + MouseDelta;

				TSharedPtr<SWidget> Parent = GetParentWidget();
				if (Parent.IsValid())
				{
					FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
					const float SnapDist = 15.0f; // px threshold

					// --- 1. SIBLING SNAPPING (Bidirectional) ---
					if (GetSnapTargetsDelegate.IsBound())
					{
						TArray<FSlateRect> Targets = GetSnapTargetsDelegate.Execute(this);
						for (const FSlateRect& Target : Targets)
						{
							// HORIZONTAL SNAPS
							// A. Adjacency (My Right -> Target Left)
							if (FMath::IsNearlyEqual(NewVisualPos.X + CurrentSize.X, Target.Left, SnapDist)) 
								NewVisualPos.X = Target.Left - CurrentSize.X;
							// B. Adjacency (My Left -> Target Right)
							else if (FMath::IsNearlyEqual(NewVisualPos.X, Target.Right, SnapDist)) 
								NewVisualPos.X = Target.Right;
							// C. Alignment (Left -> Left)
							else if (FMath::IsNearlyEqual(NewVisualPos.X, Target.Left, SnapDist)) 
								NewVisualPos.X = Target.Left;
							// D. Alignment (Right -> Right)
							else if (FMath::IsNearlyEqual(NewVisualPos.X + CurrentSize.X, Target.Right, SnapDist)) 
								NewVisualPos.X = Target.Right - CurrentSize.X;

							// VERTICAL SNAPS
							// A. Adjacency (My Bottom -> Target Top)
							if (FMath::IsNearlyEqual(NewVisualPos.Y + CurrentSize.Y, Target.Top, SnapDist)) 
								NewVisualPos.Y = Target.Top - CurrentSize.Y;
							// B. Adjacency (My Top -> Target Bottom)
							else if (FMath::IsNearlyEqual(NewVisualPos.Y, Target.Bottom, SnapDist)) 
								NewVisualPos.Y = Target.Bottom;
							// C. Alignment (Top -> Top)
							else if (FMath::IsNearlyEqual(NewVisualPos.Y, Target.Top, SnapDist)) 
								NewVisualPos.Y = Target.Top;
							// D. Alignment (Bottom -> Bottom)
							else if (FMath::IsNearlyEqual(NewVisualPos.Y + CurrentSize.Y, Target.Bottom, SnapDist)) 
								NewVisualPos.Y = Target.Bottom - CurrentSize.Y;
						}
					}

					// --- 2. PARENT EDGE SNAPPING ---
					// Snap Left/Top
					if (FMath::Abs(NewVisualPos.X) < SnapDist) NewVisualPos.X = 0.0f;
					if (FMath::Abs(NewVisualPos.Y) < SnapDist) NewVisualPos.Y = 0.0f;
					// Snap Right/Bottom
					if (FMath::Abs((NewVisualPos.X + CurrentSize.X) - ParentSize.X) < SnapDist) 
						NewVisualPos.X = ParentSize.X - CurrentSize.X;
					if (FMath::Abs((NewVisualPos.Y + CurrentSize.Y) - ParentSize.Y) < SnapDist) 
						NewVisualPos.Y = ParentSize.Y - CurrentSize.Y;

					// --- 3. HARD CLAMP (Keep inside) ---
					NewVisualPos.X = FMath::Clamp(NewVisualPos.X, 0.0f, ParentSize.X - CurrentSize.X);
					NewVisualPos.Y = FMath::Clamp(NewVisualPos.Y, 0.0f, ParentSize.Y - 20.0f);

					// --- 4. UPDATE ANCHOR & OFFSET ---
					// Now that we have the final "snapped" visual position, determine the best quadrant anchor
					UpdateAnchorAndOffset(NewVisualPos, ParentSize);
				}
			}
			else if (bResizing)
			{
				float Scale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
				FVector2D MouseDelta = (MouseEvent.GetScreenSpacePosition() - DragStartMousePos) / Scale;
				
				CurrentSize = InitialDragSize + MouseDelta;
				CurrentSize.X = FMath::Max(CurrentSize.X, 200.0f);
				CurrentSize.Y = FMath::Max(CurrentSize.Y, 100.0f);
				
				SizeBox->SetWidthOverride(CurrentSize.X);
				SizeBox->SetHeightOverride(CurrentSize.Y);

				// Compensate for alignment shifts so the top-left corner stays pinned visually
				// If we are anchored Right/Bottom, expanding size shifts the layout position Left/Top.
				// We offset the RenderTransform to negate this shift.
				TSharedPtr<SWidget> Parent = GetParentWidget();
				if (Parent.IsValid() && OverlaySlot)
				{
					const FVector2D& ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
					const FVector2D& NewLayoutPos = GetLayoutPosition(ParentSize, OverlaySlot->GetHorizontalAlignment(), OverlaySlot->GetVerticalAlignment());
					const FVector2D NewOffset = DragStartVisualPos - NewLayoutPos;
					SetRenderTransform(FSlateRenderTransform(NewOffset));
				}
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		if (bResizing) return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		if (bDragging) return FCursorReply::Cursor(EMouseCursor::CardinalCross);

		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
		FVector2D Size = MyGeometry.GetLocalSize();

		if (LocalMouse.X > Size.X - 20 && LocalMouse.Y > Size.Y - 20) return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		if (LocalMouse.Y < 28) return FCursorReply::Cursor(EMouseCursor::CardinalCross);

		return FCursorReply::Unhandled();
	}

private:
	// Determines the layout position (0,0 of the widget) based on current alignment settings
	FVector2D GetLayoutPosition(const FVector2D& ParentSize, EHorizontalAlignment HAlign, EVerticalAlignment VAlign) const
	{
		FVector2D Pos(0,0);
		if (HAlign == HAlign_Right) Pos.X = ParentSize.X - CurrentSize.X;
		else if (HAlign == HAlign_Center) Pos.X = (ParentSize.X - CurrentSize.X) / 2.0f;
		
		if (VAlign == VAlign_Bottom) Pos.Y = ParentSize.Y - CurrentSize.Y;
		else if (VAlign == VAlign_Center) Pos.Y = (ParentSize.Y - CurrentSize.Y) / 2.0f;
		
		return Pos;
	}

	// Calculates the best anchor (Quadrant) based on position center vs parent center
	// avoiding visual jumps by updating RenderTransform Offset
	void UpdateAnchorAndOffset(const FVector2D& VisualTopLeft, const FVector2D& ParentSize)
	{
		if (!OverlaySlot) return;

		FVector2D Center = VisualTopLeft + (CurrentSize * 0.5f);
		
		EHorizontalAlignment NewH = (Center.X > ParentSize.X * 0.5f) ? HAlign_Right : HAlign_Left;
		EVerticalAlignment NewV = (Center.Y > ParentSize.Y * 0.5f) ? VAlign_Bottom : VAlign_Top;

		// Update Slot Alignment
		OverlaySlot->SetHorizontalAlignment(NewH);
		OverlaySlot->SetVerticalAlignment(NewV);

		// Calculate new Render Transform so the widget stays visually static despite anchor change
		FVector2D NewLayoutPos = GetLayoutPosition(ParentSize, NewH, NewV);
		FVector2D NewOffset = VisualTopLeft - NewLayoutPos;

		SetRenderTransform(FSlateRenderTransform(NewOffset));
	}

private:
	SOverlay::FOverlaySlot* OverlaySlot = nullptr;
	FVector2D CurrentSize;
	FVector2D PendingInitialPosition;
	bool bAnchorInitialized = false;
	
	bool bDragging = false;
	bool bResizing = false;
	
	FVector2D DragStartMousePos;
	FVector2D DragStartVisualPos;
	FVector2D InitialDragSize;

	TSharedPtr<SBox> SizeBox;
	FOnGetSnapTargets GetSnapTargetsDelegate;
};

// --------------------------------------------------------------------
// MAIN OVERLAY IMPLEMENTATION
// --------------------------------------------------------------------

void SInputFlowOverlay::Construct(const FArguments& InArgs)
{
	DebugSubsystem = InArgs._Subsystem;
	UInputDebugSubsystem* Sub = DebugSubsystem.Get();
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	SetVisibility(EVisibility::SelfHitTestInvisible);

	auto GetPanelVisibility = [this]() -> EVisibility
	{
		if (DebugSubsystem.IsValid() && DebugSubsystem->GetShowOverlayPanels())
		{
			return EVisibility::Visible;
		}
		return EVisibility::Collapsed;
	};

	// Snapping Delegate: Returns rects of all sibling panels
	auto GetSnapTargets = [this](const SWidget* Requestor) -> TArray<FSlateRect>
	{
		TArray<FSlateRect> Rects;
		if (RootOverlay.IsValid())
		{
			FChildren* Children = RootOverlay->GetChildren();
			if (Children)
			{
				FVector2D ParentSize = GetPaintSpaceGeometry().GetLocalSize();
				
				for (int32 i = 0; i < Children->Num(); ++i)
				{
					TSharedRef<SWidget> Child = Children->GetChildAt(i);
					if (&Child.Get() != Requestor && Child->GetTag() == InputFlowHelpers::InputFlowAnalyzerTag)
					{
						TSharedPtr<SInputFlowDraggablePanel> Panel = StaticCastSharedRef<SInputFlowDraggablePanel>(Child);
						if (Panel.IsValid() && Panel->GetVisibility().IsVisible())
						{
							Rects.Add(Panel->GetComputedRect(ParentSize));
						}
					}
				}
			}
		}
		return Rects;
	};
	
	ChildSlot
	[
		SAssignNew(RootOverlay, SOverlay)
	];

	auto AddPanel = [&](TSharedRef<SInputFlowDraggablePanel> Panel)
	{
		SOverlay::FScopedWidgetSlotArguments Slot = RootOverlay->AddSlot();
		Slot.HAlign(HAlign_Left).VAlign(VAlign_Top); // Defaults; Panel will update this on first Tick
		Slot[ Panel ];
		Panel->SetOverlaySlot(Slot.GetSlot());
	};

	// 1. Settings Panel
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("Settings"))
		.InitialPosition(FVector2D(20, 20)) 
		.InitialSize(FVector2D(320, 150))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.Visibility_Lambda(GetPanelVisibility)
		[
			SNew(SInputFlowSettingsPanel, Sub).IsOverlay(true)
		]
	);

	// 2. Status Dashboard
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("Status Dashboard"))
		.InitialPosition(FVector2D(1400, 20))
		.InitialSize(FVector2D(400, 220))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.Visibility_Lambda(GetPanelVisibility)
		[
			SNew(SInputFlowStatusDashboard, Sub)
		]
	);

	// 3. Log View
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("Input Event Log"))
		.InitialPosition(FVector2D(20, 700)) 
		.InitialSize(FVector2D(500, 300))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.Visibility_Lambda(GetPanelVisibility)
		[
			SAssignNew(LogView, SInputFlowLogView, Sub).IsOverlay(true)
		]
	);

	// 4. Inspectors
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("Hierarchy & Actions"))
		.InitialPosition(FVector2D(1400, 300))
		.InitialSize(FVector2D(400, 500))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.Visibility_Lambda(GetPanelVisibility)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(0.6f).Padding(0, 0, 0, 4)
			[
				SAssignNew(HierarchyView, SCommonUIHierarchyView, Sub).IsOverlay(true)
			]
			+ SVerticalBox::Slot().FillHeight(0.4f)
			[
				SAssignNew(InspectorView, SEnhancedInputInspector, Sub).IsOverlay(true)
			]
		]
	);
}

UInputDebugSubsystem* SInputFlowOverlay::GetSubsystem() const
{
	return DebugSubsystem.Get();
}

int32 SInputFlowOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
								 const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
								 int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LabelBatch.Reset();

	PaintNavigationSimulation(AllottedGeometry, OutDrawElements, LayerId);
	PaintFocusHistory(AllottedGeometry, OutDrawElements, LayerId);
	PaintHitTestGrid(AllottedGeometry, OutDrawElements, LayerId);

	ResolveAndDrawLabels(AllottedGeometry, OutDrawElements, LayerId + 50); 

	int32 PanelLayerId = LayerId + 100;
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, PanelLayerId, InWidgetStyle, bParentEnabled);
}

void SInputFlowOverlay::ResolveAndDrawLabels(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (LabelBatch.IsEmpty()) return;

	const FVector2D ScreenSize = AllottedGeometry.GetLocalSize();
	const FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 9);

	// 1. Convert Pending Labels to Physics Items
	TArray<FInputFlowPhysicsItem> PhysicsItems;
	PhysicsItems.Reserve(LabelBatch.Num());

	for (const FPendingLabel& Label : LabelBatch)
	{
		FInputFlowPhysicsItem Item;
		Item.Position = Label.CurrentPos;
		Item.Size = Label.Size;
		Item.TargetPosition = Label.OriginalPos;
		Item.bIsFixed = false;
		PhysicsItems.Add(Item);
	}

	// 2. Solve Collisions
	InputFlowHelpers::SolveAABBCollisions(PhysicsItems, ScreenSize, 20);

	// 3. Apply Results & Draw
	for (int32 i = 0; i < LabelBatch.Num(); ++i)
	{
		const FPendingLabel& Label = LabelBatch[i];
		const FInputFlowPhysicsItem& Resolved = PhysicsItems[i];
		FVector2D FinalPos = Resolved.Position;

		// Draw Connector
		if (FVector2D::DistSquared(Label.OriginalPos, FinalPos) > 100.0f)
		{
			TArray<FVector2D> LinePoints;
			LinePoints.Add(Label.OriginalPos + FVector2D(0.0f, Label.Size.Y * 0.5f)); 
			LinePoints.Add(FinalPos + (Label.Size * 0.5f));

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, Label.Color.CopyWithNewOpacity(0.5f), true, 1.0f);
			
			FPaintGeometry OriginDot = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(FVector2D(4, 4)), FSlateLayoutTransform(UE::Slate::CastToVector2f(Label.OriginalPos + FVector2D(-2, (Label.Size.Y * 0.5f) - 2))));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, OriginDot, InputFlowStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Label.Color);
		}

		// Draw Box & Text
		FPaintGeometry BoxGeo = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Label.Size), FSlateLayoutTransform(UE::Slate::CastToVector2f(FinalPos)));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, BoxGeo, InputFlowStyle::GetBrush("ChildWindow.Background"), ESlateDrawEffect::None, InputFlowStyle::Color_LabelBg);

		FVector2D Padding(8.0f, 4.0f);
		FPaintGeometry TextGeo = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Label.Size - (Padding * 2.0f)), FSlateLayoutTransform(UE::Slate::CastToVector2f(FinalPos + Padding)));

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, TextGeo, Label.Text, FontInfo, ESlateDrawEffect::None, FLinearColor::Black); // Shadow
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3, TextGeo, Label.Text, FontInfo, ESlateDrawEffect::None, Label.Color); // Main
	}
}

void SInputFlowOverlay::PaintNavigationSimulation(const FGeometry& AllottedGeometry,
												  FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	UInputDebugSubsystem* Sub = GetSubsystem();
	if (!Sub || !Sub->GetNavigationSimulationEnabled()) return;

	// Draw Focus
	if (TSharedPtr<SWidget> Focus = Sub->GetFocusedWidget())
	{
		int32 FocusLayerId = LayerId + 10;
		FString FocusLabel = FString::Printf(TEXT("FOCUS: %s"), *InputFlowHelpers::GetWidgetDisplayName(Focus));
		DrawWidgetHighlight(Focus, InputFlowStyle::Color_Focus, FocusLabel, AllottedGeometry, OutDrawElements, FocusLayerId, 4.f);
		
		const FGeometry& FocusGeo = Focus->GetPaintSpaceGeometry();
		if (FocusGeo.GetAbsoluteSize().GetMin() > 0.0f)
		{
			FVector2D Center = AllottedGeometry.AbsoluteToLocal(FocusGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
			
			// Draw Crosshair/Ring
			TArray<FVector2D> RingPoints;
			constexpr int32 Segments = 16;
			constexpr float RingRadius = 6.0f;
			for (int32 i = 0; i <= Segments; ++i) {
				float Angle = ((float)i / (float)Segments) * 2.0f * PI;
				RingPoints.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RingRadius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(), RingPoints, ESlateDrawEffect::None, InputFlowStyle::Color_Focus, true, 3.f);
		}
	}

	// Draw Links
	const TArray<FNavigationLink>& Links = Sub->GetNavigationLinks();
	const int32 MaxDepth = Sub->GetNavigationDepth();
	for (const FNavigationLink& Link : Links)
	{
		TSharedPtr<SWidget> Start = Link.StartWidget.Pin();
		TSharedPtr<SWidget> End = Link.EndWidget.Pin();
		if (!Start) continue;

		float Alpha = (MaxDepth == 1) ? 1.0f : FMath::Clamp(1.5f - ((float)Link.DepthStep / MaxDepth), 0.2f, 1.0f);

		if (Link.ResultType == ENavSimResult::Normal && End.IsValid())
		{
			FLinearColor LinkColor = InputFlowStyle::Color_NavNormal.CopyWithNewOpacity(Alpha);
			FString EndName = InputFlowHelpers::GetWidgetDisplayName(End);
			FString Label = FString::Printf(TEXT("%s Target\n%s"), *UEnum::GetValueAsString(Link.Direction), *EndName);

			DrawWidgetHighlight(End, LinkColor, Label, AllottedGeometry, OutDrawElements, LayerId);
			DrawConnectionSpline(AllottedGeometry, Start, End, Link.Direction, LinkColor, OutDrawElements, LayerId);
		}
		else
		{
			bool bIsStopped = (Link.ResultType == ENavSimResult::Stopped);
			FLinearColor Color = (bIsStopped ? InputFlowStyle::Color_NavBlocked : InputFlowStyle::Color_NavHandled).CopyWithNewOpacity(Alpha);
			FString BlockerName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");
			FString StatusLabel = FString::Printf(TEXT("%s %s\n%s"), *UEnum::GetValueAsString(Link.Direction), bIsStopped ? TEXT("BLOCKED By") : TEXT("HANDLED By"), *BlockerName);
			DrawDirectionalIndicator(AllottedGeometry, Start->GetPaintSpaceGeometry(), Link.Direction, StatusLabel, Color, OutDrawElements, LayerId);
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

	for (const FFocusHistoryEntry& Entry : History)
	{
		double Age = Now - Entry.Timestamp;
		if (Age > MaxAge) continue;

		TSharedPtr<SWidget> Widget = Entry.Widget.Pin();
		if (!Widget.IsValid()) continue;
		
		float NormalizedAge = (float)(Age / MaxAge);
		float Alpha = 1.0f - NormalizedAge;
		FLinearColor Color = InputFlowStyle::Color_Focus.CopyWithNewOpacity(Alpha * 0.5f);

		DrawWidgetHighlight(Widget, Color, FString(), AllottedGeometry, OutDrawElements, LayerId);

		FVector2D CurrentCenter = AllottedGeometry.AbsoluteToLocal(Widget->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
		if (LastCenterWidget.IsValid())
		{
			TArray<FVector2D> Points = {LastCenterPos, CurrentCenter};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 2.0f);
		}
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

	for (const FHittestGrid::FWidgetSortData& Item : SortData)
	{
		TSharedPtr<SWidget> Widget = Item.WeakWidget.Pin();
		if (Widget.IsValid() && Widget->GetVisibility().IsHitTestVisible())
		{
			const FGeometry& WidgetGeo = Widget->GetPaintSpaceGeometry();
			if (WidgetGeo.GetAbsoluteSize().IsZero()) continue;

			FVector2D TopLeft(AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePosition()));
			FVector2D BottomRight(AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f))));
			FVector2D Size = BottomRight - TopLeft;

			FPaintGeometry PaintGeo = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Size), FSlateLayoutTransform(UE::Slate::CastToVector2f(TopLeft)));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo, InputFlowStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.0f, 1.0f, 0.9f, 0.03f));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo, InputFlowStyle::GetBrush("Debug.Border"), ESlateDrawEffect::None, FLinearColor(0.0f, 1.0f, 0.9f, 0.3f));
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
	FVector2D BottomRight(OverlayGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f))));
	FVector2D Size = BottomRight - TopLeft;

	FPaintGeometry PaintGeo = OverlayGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Size), FSlateLayoutTransform(UE::Slate::CastToVector2f(TopLeft)));
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo, InputFlowStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color.CopyWithNewOpacity(Color.A * 0.15f));
	
	TArray<FVector2D> LinePoints = {FVector2D::ZeroVector, FVector2D(Size.X, 0), FVector2D(Size.X, Size.Y), FVector2D(0, Size.Y), FVector2D::ZeroVector};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeo, LinePoints, ESlateDrawEffect::None, Color, true, Thickness);

	if (!Label.IsEmpty()) DrawTextLabelWithBackground(OverlayGeometry, TopLeft + FVector2D(0, -22), Label, Color, OutDrawElements, LayerId);
}

void SInputFlowOverlay::DrawTextLabelWithBackground(const FGeometry& AllottedGeometry, FVector2D Position,
													const FString& Text, const FLinearColor& Color,
													FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	const FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FVector2D BoxSize = FontMeasure->Measure(Text, FontInfo) + FVector2D(16.0f, 8.0f);
	
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

	FVector2D StartP = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
	FVector2D EndP = AllottedGeometry.AbsoluteToLocal(EndGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
	float ControlDist = FMath::Max((EndP - StartP).Size() * 0.5f, 50.0f);

	FVector2D StartTangent(0, 0);
	switch (Direction) {
	case EUINavigation::Left: StartTangent = FVector2D(-1, 0); break;
	case EUINavigation::Right: StartTangent = FVector2D(1, 0); break;
	case EUINavigation::Up: StartTangent = FVector2D(0, -1); break;
	case EUINavigation::Down: StartTangent = FVector2D(0, 1); break;
	default: break;
	}

	FVector2D CP1 = StartP + (StartTangent * ControlDist);
	FVector2D CP2 = EndP - (StartTangent * (ControlDist * 0.5f));

	TArray<FVector2D> Points;
	for (int32 i = 0; i <= 20; ++i) {
		float T = (float)i / 20.0f;
		float OneMinusT = 1.0f - T;
		Points.Add((OneMinusT * OneMinusT * OneMinusT) * StartP + (3.0f * OneMinusT * OneMinusT * T) * CP1 + (3.0f * OneMinusT * T * T) * CP2 + (T * T * T) * EndP);
	}

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 3.0f);

	// Arrowhead
	FVector2D Dir = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
	FVector2D Tangent = FVector2D(-Dir.Y, Dir.X);
	TArray<FVector2D> HeadPoints = {EndP + Dir * 2.0f, EndP - Dir * 12.0f + Tangent * 7.2f, EndP - Dir * 12.0f - Tangent * 7.2f, EndP + Dir * 2.0f};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(), HeadPoints, ESlateDrawEffect::None, Color, true, 3.0f);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(FVector2D(1, 1)), FSlateLayoutTransform(UE::Slate::CastToVector2f(EndP))), InputFlowStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
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
	switch (Direction) {
	case EUINavigation::Left: Anchor = FVector2D(StartTL.X, Center.Y); DirVec = FVector2D(-1, 0); break;
	case EUINavigation::Right: Anchor = FVector2D(StartBR.X, Center.Y); DirVec = FVector2D(1, 0); break;
	case EUINavigation::Up: Anchor = FVector2D(Center.X, StartTL.Y); DirVec = FVector2D(0, -1); break;
	case EUINavigation::Down: Anchor = FVector2D(Center.X, StartBR.Y); DirVec = FVector2D(0, 1); break;
	default: return;
	}

	FVector2D EndPos = Anchor + (DirVec * 30.0f);
	TArray<FVector2D> Points = {Anchor, EndPos};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 3.0f);

	FVector2D Tangent(-DirVec.Y, DirVec.X);
	TArray<FVector2D> BarPoints = {EndPos + (Tangent * 8.0f), EndPos - (Tangent * 8.0f)};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), BarPoints, ESlateDrawEffect::None, Color, true, 3.0f);

	DrawTextLabelWithBackground(AllottedGeometry, EndPos + FVector2D(5, 5), Label, Color, OutDrawElements, LayerId);
}