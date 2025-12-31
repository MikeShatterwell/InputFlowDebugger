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
#include "InputFlowSettings.h"
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "SCommonUIHierarchyView.h"
#include "SEnhancedInputInspector.h"
#include "SInputFlowLogView.h"
#include "SInputFlowSettingsPanel.h"
#include "SInputFlowStatusDashboard.h"

// --- CONSTANTS FOR STYLING ---
namespace InputFlowStyle
{
	constexpr FLinearColor Color_Focus = FLinearColor(0.2f, 0.8f, 0.2f);
	constexpr FLinearColor Color_NavNormal = FLinearColor(1.0f, 0.7f, 0.2f);
	constexpr FLinearColor Color_NavExplicit = FLinearColor(0.9f, 0.7f, 0.8f);
	constexpr FLinearColor Color_NavHandled = FLinearColor(0.2f, 0.6f, 1.0f);
	constexpr FLinearColor Color_NavBlocked = FLinearColor(1.0f, 0.3f, 0.3f);
	constexpr FLinearColor Color_Void = FLinearColor(0.5f, 0.5f, 0.5f);
	constexpr FLinearColor Color_LabelBg = FLinearColor(0.05f, 0.05f, 0.05f, 0.85f); // Dark Overlay

	static const FSlateBrush* GetBrush(FName BrushName)
	{
		return FCoreStyle::Get().GetBrush(BrushName);
	}
}

namespace InputFlowPanelConstants
{
	constexpr float ResizeBorderThickness = 4.0f;
	constexpr float MinPanelWidth = 200.0f;
	constexpr float MinPanelHeight = 100.0f;
	constexpr float SnapThreshold = 12.0f;
	constexpr float HeaderHeight = 28.0f;
}

// --------------------------------------------------------------------
// DRAGGABLE PANEL WITH SNAPPING, ANCHORING & MINIMIZE
// --------------------------------------------------------------------

DECLARE_DELEGATE_RetVal_OneParam(TArray<FSlateRect>, FOnGetSnapTargets, const SWidget* /*Requestor*/);
DECLARE_DELEGATE(FOnClosePanel);

class SInputFlowDraggablePanel : public SCompoundWidget
{
public:
	enum EResizeDirection
	{
		None = 0,
		Left = 1 << 0,
		Right = 1 << 1,
		Top = 1 << 2,
		Bottom = 1 << 3,

		TopLeft = Top | Left,
		TopRight = Top | Right,
		BottomLeft = Bottom | Left,
		BottomRight = Bottom | Right
	};

	SLATE_BEGIN_ARGS(SInputFlowDraggablePanel)
			: _InitialPosition(FVector2D(100, 100))
			  , _InitialSize(FVector2D(400, 300))
			  , _Title(TEXT("Panel"))
			  , _CanClose(true)
			  , _CanMinimize(true)
		{
		}

		SLATE_ARGUMENT(FVector2D, InitialPosition)
		SLATE_ARGUMENT(FVector2D, InitialSize)
		SLATE_ARGUMENT(FString, Title)
		SLATE_ARGUMENT(bool, CanClose)
		SLATE_ARGUMENT(bool, CanMinimize)
		SLATE_EVENT(FOnGetSnapTargets, OnGetSnapTargets)
		SLATE_EVENT(FOnClosePanel, OnClose)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

void Construct(const FArguments& InArgs)
	{
		CurrentSize = InArgs._InitialSize;
		GetSnapTargetsDelegate = InArgs._OnGetSnapTargets;
		PendingInitialPosition = InArgs._InitialPosition;

		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
		
		const FWindowStyle& WindowStyle = FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window");

		// Build Title Bar Conditionally
		const TSharedRef<SHorizontalBox> TitleBarBox = SNew(SHorizontalBox);

		// Title Text
		TitleBarBox->AddSlot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InArgs._Title))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f))
				.Visibility(EVisibility::HitTestInvisible) // Pass clicks to drag header
			];

		// Minimize Button
		if (InArgs._CanMinimize)
		{
			TitleBarBox->AddSlot()
				.AutoWidth()
				.Padding(4, 0, 0, 0)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Cursor(EMouseCursor::Hand)
					.ButtonStyle(&WindowStyle.MinimizeButtonStyle)
					.ContentPadding(0.f)
					.OnClicked(this, &SInputFlowDraggablePanel::OnToggleMinimize)
					.ToolTipText(FText::FromString("Minimize"))
					[
						SNew(SBox).WidthOverride(20.f).HeightOverride(20.f)
					]
				];
		}

		// Close Button
		if (InArgs._CanClose)
		{
			TitleBarBox->AddSlot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Cursor(EMouseCursor::Hand)
					.ButtonStyle(&WindowStyle.CloseButtonStyle)
					.ContentPadding(0.f)
					.OnClicked_Lambda([Close = InArgs._OnClose]()
					{
						if (Close.ExecuteIfBound()) return FReply::Handled();
						return FReply::Unhandled();
					})
					.ToolTipText(FText::FromString("Close"))
					[
						SNew(SBox).WidthOverride(20.f).HeightOverride(20.f)
					]
				];
		}

		ChildSlot
		[
			SAssignNew(SizeBox, SBox)
			// NOTE: No Width/Height overrides here. We wait for Tick to measure content.
			[
				SAssignNew(PanelBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f))
				.Padding(0)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				[
					SNew(SVerticalBox)
					
					// Header
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(InputFlowPanelConstants::HeaderHeight)
						[	
							SAssignNew(HeaderRow, SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f, 0.8f))
							// Right padding (8.0f) ensures buttons aren't covered by Resize Handles
							.Padding(FMargin(4, 2, 8, 2))
							[
								TitleBarBox
							]
						]
					]

					// Content
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(ContentBox, SBox)
						.Padding(InputFlowPanelConstants::ResizeBorderThickness)
						[
							InArgs._Content.Widget
						]
					]
				]
			]
		];
	}
	
	void SetOverlaySlot(SOverlay::FOverlaySlot* InSlot)
	{
		OverlaySlot = InSlot;
	}

	FSlateRect GetComputedRect(const FVector2D& ParentSize) const
	{
		const FVector2D LayoutPos = GetLayoutPosition(
			ParentSize, OverlaySlot ? OverlaySlot->GetHorizontalAlignment() : HAlign_Left,
			OverlaySlot ? OverlaySlot->GetVerticalAlignment() : VAlign_Top);
		float Height = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;
		if (GetRenderTransform().IsSet())
		{
			const FVector2D VisualPos = LayoutPos + GetRenderTransform()->GetTranslation();
			return FSlateRect(VisualPos.X, VisualPos.Y, VisualPos.X + CurrentSize.X, VisualPos.Y + Height);
		}
		return FSlateRect(LayoutPos.X, LayoutPos.Y, LayoutPos.X + CurrentSize.X, LayoutPos.Y + Height);
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (!UInputFlowSettings::Get()->IsOverlayEnabled())
		{
			return;
		}
		if (!bHasSetInitialSize && SizeBox.IsValid())
		{
			SlatePrepass(AllottedGeometry.GetAccumulatedLayoutTransform().GetScale());
			const FVector2D NaturalSize = SizeBox->GetDesiredSize();

			CurrentSize.X = FMath::Max(CurrentSize.X, NaturalSize.X);
			CurrentSize.Y = FMath::Max(CurrentSize.Y, NaturalSize.Y);

			SizeBox->SetWidthOverride(CurrentSize.X);
			if (!bIsMinimized)
			{
				SizeBox->SetHeightOverride(CurrentSize.Y);
			}

			bHasSetInitialSize = true;
		}

		// Initial Anchoring
		if (!bAnchorInitialized && OverlaySlot && bHasSetInitialSize)
		{
			const TSharedPtr<SWidget> Parent = GetParentWidget();
			if (Parent.IsValid())
			{
				const FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
				if (ParentSize.X > 1.0f)
				{
					UpdateAnchorAndOffset(PendingInitialPosition, ParentSize);
					bAnchorInitialized = true;
				}
			}
		}
	}

	FReply OnToggleMinimize()
	{
		// Capture current visual location (Top-Left) BEFORE state change
		FVector2D SavedVisualLocation = FVector2D::ZeroVector;
		const TSharedPtr<SWidget> Parent = GetParentWidget();
		FVector2D ParentSize = FVector2D::ZeroVector;

		if (Parent.IsValid())
		{
			ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
			// Calculate where we currently appear to be
			SavedVisualLocation = GetComputedRect(ParentSize).GetTopLeft();
		}

		// Change State
		bIsMinimized = !bIsMinimized;

		if (ContentBox.IsValid())
		{
			ContentBox->SetVisibility(bIsMinimized ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
		}

		// Update Height Restrictions
		if (SizeBox.IsValid())
		{
			if (bIsMinimized)
			{
				SizeBox->SetHeightOverride(InputFlowPanelConstants::HeaderHeight);
			}
			else
			{
				SizeBox->SetHeightOverride(CurrentSize.Y);
			}
		}

		// If we are Bottom-Aligned, the Layout Position just jumped down. 
		// This function will update the RenderTransform to counteract that jump.
		if (Parent.IsValid() && ParentSize.X > 1.0f)
		{
			UpdateAnchorAndOffset(SavedVisualLocation, ParentSize);
		}
		return FReply::Handled();
	}

	uint8 CheckResizeDirection(const FVector2D& LocalMouse, const FVector2D& LocalSize) const
	{
		if (bIsMinimized) return None;

		uint8 Direction = None;

		if (LocalMouse.X < InputFlowPanelConstants::ResizeBorderThickness) Direction |= Left;
		if (LocalMouse.X > LocalSize.X - InputFlowPanelConstants::ResizeBorderThickness) Direction |= Right;
		if (LocalMouse.Y < InputFlowPanelConstants::ResizeBorderThickness) Direction |= Top;
		if (LocalMouse.Y > LocalSize.Y - InputFlowPanelConstants::ResizeBorderThickness) Direction |= Bottom;

		return Direction;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			const FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			bool bOverHeader = HeaderRow.IsValid() && HeaderRow->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition());
			const FVector2D Size = MyGeometry.GetLocalSize();
			
			// Check resize only if we aren't minimized
			if (!bIsMinimized)
			{
				const uint8 ResizeDir = CheckResizeDirection(LocalMouse, Size);
				
				// Only allow resize if we are NOT over the header (unless it's top/side edge, but header takes priority for usability)
				if (ResizeDir != None && !bOverHeader)
				{
					bResizing = true;
					CurrentResizeDir = ResizeDir;
					DragStartMousePos = MouseEvent.GetScreenSpacePosition();
					InitialDragSize = CurrentSize;

					const TSharedPtr<SWidget> Parent = GetParentWidget();
					if (Parent && OverlaySlot)
					{
						const FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
						const FVector2D LayoutPos = GetLayoutPosition(ParentSize, OverlaySlot->GetHorizontalAlignment(),
																	  OverlaySlot->GetVerticalAlignment());

						if (GetRenderTransform().IsSet())
						{
							DragStartVisualPos = LayoutPos + GetRenderTransform()->GetTranslation();
						}
						else
						{
							DragStartVisualPos = LayoutPos;
						}
						return FReply::Handled().CaptureMouse(AsShared());
					}
				}
			}

			// Drag Zone is the header only
			if (bOverHeader)
			{
				bDragging = true;
				DragStartMousePos = MouseEvent.GetScreenSpacePosition();

				if (const TSharedPtr<SWidget> Parent = GetParentWidget())
				{
					const FGeometry ParentGeo = Parent->GetPaintSpaceGeometry();
					const FVector2D ParentSize = ParentGeo.GetLocalSize();

					const FVector2D LayoutPos = GetLayoutPosition(ParentSize, OverlaySlot->GetHorizontalAlignment(), OverlaySlot->GetVerticalAlignment());
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
			CurrentResizeDir = None;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging)
		{
			const float Scale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
			const FVector2D MouseDelta = (MouseEvent.GetScreenSpacePosition() - DragStartMousePos) / Scale;
			FVector2D NewVisualPos = DragStartVisualPos + MouseDelta;

			TSharedPtr<SWidget> Parent = GetParentWidget();
			if (Parent.IsValid())
			{
				FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
				const float SnapDist = 15.0f;

				if (GetSnapTargetsDelegate.IsBound())
				{
					TArray<FSlateRect> Targets = GetSnapTargetsDelegate.Execute(this);
					float SnapHeight = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;

					for (const FSlateRect& Target : Targets)
					{
						// Horizontal
						if (FMath::IsNearlyEqual(NewVisualPos.X + SnapHeight, Target.Left, SnapDist))
							NewVisualPos.X = Target.Left - SnapHeight;
						else if (FMath::IsNearlyEqual(NewVisualPos.X, Target.Right, SnapDist))
							NewVisualPos.X = Target.Right;
						else if (FMath::IsNearlyEqual(NewVisualPos.X, Target.Left, SnapDist))
							NewVisualPos.X = Target.Left;
						else if (FMath::IsNearlyEqual(NewVisualPos.X + SnapHeight, Target.Right, SnapDist))
							NewVisualPos.X = Target.Right - SnapHeight;

						// Vertical
						if (FMath::IsNearlyEqual(NewVisualPos.Y + SnapHeight, Target.Top, SnapDist))
							NewVisualPos.Y = Target.Top - SnapHeight;
						else if (FMath::IsNearlyEqual(NewVisualPos.Y, Target.Bottom, SnapDist))
							NewVisualPos.Y = Target.Bottom;
						else if (FMath::IsNearlyEqual(NewVisualPos.Y, Target.Top, SnapDist))
							NewVisualPos.Y = Target.Top;
						else if (FMath::IsNearlyEqual(NewVisualPos.Y + SnapHeight, Target.Bottom, SnapDist))
							NewVisualPos.Y = Target.Bottom - SnapHeight;
					}
				}

				// Parent Edges
				const float ActualHeight = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;
				if (FMath::Abs(NewVisualPos.X) < SnapDist) NewVisualPos.X = 0.0f;
				if (FMath::Abs(NewVisualPos.Y) < SnapDist) NewVisualPos.Y = 0.0f;
				if (FMath::Abs((NewVisualPos.X + CurrentSize.X) - ParentSize.X) < SnapDist)
					NewVisualPos.X = ParentSize.X - CurrentSize.X;
				if (FMath::Abs((NewVisualPos.Y + ActualHeight) - ParentSize.Y) < SnapDist)
					NewVisualPos.Y = ParentSize.Y - ActualHeight;

				UpdateAnchorAndOffset(NewVisualPos, ParentSize);
			}
			return FReply::Handled();
		}

		if (bResizing && CurrentResizeDir != None)
		{
			const float Scale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
			const FVector2D MouseDelta = (MouseEvent.GetScreenSpacePosition() - DragStartMousePos) / Scale;

			FVector2D NewSize = InitialDragSize;
			FVector2D TargetVisualPos = DragStartVisualPos;

			if (CurrentResizeDir & Right) NewSize.X += MouseDelta.X;
			if (CurrentResizeDir & Bottom) NewSize.Y += MouseDelta.Y;
			if (CurrentResizeDir & Left)
			{
				NewSize.X -= MouseDelta.X;
				TargetVisualPos.X += MouseDelta.X;
			}
			if (CurrentResizeDir & Top)
			{
				NewSize.Y -= MouseDelta.Y;
				TargetVisualPos.Y += MouseDelta.Y;
			}

			const FVector2D MinSize(InputFlowPanelConstants::MinPanelWidth, InputFlowPanelConstants::MinPanelHeight);

			if (NewSize.X < MinSize.X)
			{
				if (CurrentResizeDir & Left) TargetVisualPos.X -= (MinSize.X - NewSize.X);
				NewSize.X = MinSize.X;
			}
			if (NewSize.Y < MinSize.Y)
			{
				if (CurrentResizeDir & Top) TargetVisualPos.Y -= (MinSize.Y - NewSize.Y);
				NewSize.Y = MinSize.Y;
			}

			CurrentSize = NewSize;
			SizeBox->SetWidthOverride(CurrentSize.X);
			SizeBox->SetHeightOverride(CurrentSize.Y);

			TSharedPtr<SWidget> Parent = GetParentWidget();
			if (Parent.IsValid() && OverlaySlot)
			{
				const FVector2D& ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
				const FVector2D& NewLayoutPos = GetLayoutPosition(ParentSize, OverlaySlot->GetHorizontalAlignment(),
																  OverlaySlot->GetVerticalAlignment());
				const FVector2D NewOffset = TargetVisualPos - NewLayoutPos;
				SetRenderTransform(FSlateRenderTransform(NewOffset));
			}

			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		if (bDragging) return FCursorReply::Cursor(EMouseCursor::CardinalCross);

		const FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
		const FVector2D Size = MyGeometry.GetLocalSize();

		const uint8 Dir = bResizing ? CurrentResizeDir : CheckResizeDirection(LocalMouse, Size);

		if (Dir == TopLeft || Dir == BottomRight) return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		if (Dir == TopRight || Dir == BottomLeft) return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
		if (Dir & Top || Dir & Bottom) return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		if (Dir & Left || Dir & Right) return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);

		if (HeaderRow.IsValid() && HeaderRow->GetCachedGeometry().IsUnderLocation(CursorEvent.GetScreenSpacePosition()))
		{
			return FCursorReply::Cursor(EMouseCursor::CardinalCross);
		}
		return FCursorReply::Unhandled();
	}

private:
	FVector2D GetLayoutPosition(const FVector2D& ParentSize, const EHorizontalAlignment HAlign,
								const EVerticalAlignment VAlign) const
	{
		FVector2D Pos(0, 0);
		const float EffectiveHeight = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;
		
		if (HAlign == HAlign_Right) Pos.X = ParentSize.X - CurrentSize.X;
		else if (HAlign == HAlign_Center) Pos.X = (ParentSize.X - CurrentSize.X) / 2.0f;

		if (VAlign == VAlign_Bottom) Pos.Y = ParentSize.Y - EffectiveHeight;
		else if (VAlign == VAlign_Center) Pos.Y = (ParentSize.Y - EffectiveHeight) / 2.0f;

		return Pos;
	}

	void UpdateAnchorAndOffset(const FVector2D& VisualTopLeft, const FVector2D& ParentSize)
	{
		if (!OverlaySlot) return;

		const float ActualHeight = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;
		const FVector2D Center = VisualTopLeft + (FVector2D(CurrentSize.X, ActualHeight) * 0.5f);

		// Determine new anchor based on viewport
		const EHorizontalAlignment NewHorizontalAlignment = (Center.X > ParentSize.X * 0.5f) ? HAlign_Right : HAlign_Left;
		const EVerticalAlignment NewVerticalAlignment = (Center.Y > ParentSize.Y * 0.5f) ? VAlign_Bottom : VAlign_Top;

		OverlaySlot->SetHorizontalAlignment(NewHorizontalAlignment);
		OverlaySlot->SetVerticalAlignment(NewVerticalAlignment);

		FVector2D NewLayoutPos = GetLayoutPosition(ParentSize, NewHorizontalAlignment, NewVerticalAlignment);
		FVector2D NewOffset = VisualTopLeft - NewLayoutPos;

		SetRenderTransform(FSlateRenderTransform(NewOffset));
	}

private:
	SOverlay::FOverlaySlot* OverlaySlot = nullptr;
	FVector2D CurrentSize = FVector2D::ZeroVector;
	FVector2D PendingInitialPosition = FVector2D::ZeroVector;

	bool bHasSetInitialSize = false;
	bool bAnchorInitialized = false;
	bool bIsMinimized = false;

	bool bDragging = false;
	bool bResizing = false;
	uint8 CurrentResizeDir = None;

	FVector2D DragStartMousePos = FVector2D::ZeroVector;
	FVector2D DragStartVisualPos = FVector2D::ZeroVector;
	FVector2D InitialDragSize = FVector2D::ZeroVector;

	TSharedPtr<SBox> SizeBox;
	TSharedPtr<SBox> ContentBox;
	TSharedPtr<SBorder> HeaderRow;
	TSharedPtr<SBorder> PanelBorder;
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
						TSharedPtr<SInputFlowDraggablePanel> Panel = StaticCastSharedRef<SInputFlowDraggablePanel>(
							Child);
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
		Slot[Panel];
		Panel->SetOverlaySlot(Slot.GetSlot());
	};

	// Settings Panel
	const FString SettingsPanelTitle = TEXT("Settings");
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(SettingsPanelTitle)
		.InitialPosition(FVector2D(20, 20))
		.InitialSize(FVector2D(400, 100))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.CanClose(false)
		[
			SNew(SInputFlowSettingsPanel, Sub).IsOverlay(true)
		]
	);

	// Status Dashboard
	AddPanel(
			SNew(SInputFlowDraggablePanel)
			.Title(TEXT("Status Dashboard"))
			.InitialPosition(FVector2D(1400, 20))
			.InitialSize(FVector2D(600, 220))
			.OnGetSnapTargets_Lambda(GetSnapTargets)
			.OnClose_Lambda([]() 
			{ 
				GetMutableDefault<UInputFlowSettings>()->SetShowDashboardPanel(false);
			})
			.Visibility_Lambda([]() -> EVisibility
			{
				return UInputFlowSettings::Get()->IsDashboardPanelShown()
					? EVisibility::SelfHitTestInvisible
					: EVisibility::Collapsed;
			})
			[
				SNew(SInputFlowStatusDashboard, Sub)
			]
		);

	// Log View
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("Input Event Log"))
		.InitialPosition(FVector2D(20, 700))
		.InitialSize(FVector2D(1200, 300))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.OnClose_Lambda([]() 
		{ 
			GetMutableDefault<UInputFlowSettings>()->SetShowLogPanel(false);
		})
		.Visibility_Lambda([]() -> EVisibility
		{
			return UInputFlowSettings::Get()->IsLogPanelShown() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SAssignNew(LogView, SInputFlowLogView, Sub).IsOverlay(true)
		]
	);

	// CommonUI Hierarchy
#if WITH_PLUGIN_COMMONUI
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("CommonUI Hierarchy"))
		.InitialPosition(FVector2D(1400, 300))
		.InitialSize(FVector2D(400, 500))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.OnClose_Lambda([Sub]() { GetMutableDefault<UInputFlowSettings>()->SetShowHierarchyPanel(false); })
		.Visibility_Lambda([this]() -> EVisibility
		{
			return UInputFlowSettings::Get()->IsHierarchyPanelShown()
					   ? EVisibility::SelfHitTestInvisible
					   : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(0.6f).Padding(0, 0, 0, 4)
			[
				SAssignNew(CommonUIHierarchyView, SCommonUIHierarchyView, Sub).IsOverlay(true)
			]
		]
	);
#endif

	// Enhanced Input Analyzer
#if WITH_PLUGIN_ENHANCEDINPUT
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("Enhanced Input Inspector"))
		.InitialPosition(FVector2D(1800, 400))
		.InitialSize(FVector2D(400, 500))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.OnClose_Lambda([Sub]() { GetMutableDefault<UInputFlowSettings>()->SetShowEnhancedInputPanel(false); })
		.Visibility_Lambda([this]() -> EVisibility
		{
			return UInputFlowSettings::Get()->IsEnhancedInputPanelShown()
					   ? EVisibility::SelfHitTestInvisible
					   : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(0.6f).Padding(0, 0, 0, 4)
			[
				SAssignNew(EnhancedInputInspectorView, SEnhancedInputInspector, Sub).IsOverlay(true)
			]
		]
	);
#endif
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

	if (!UInputFlowSettings::Get()->IsOverlayEnabled())
	{
		return LayerId;
	}

	PaintFocusedWidget(AllottedGeometry, OutDrawElements, LayerId);
	PaintNavigationSimulation(AllottedGeometry, OutDrawElements, LayerId);
	PaintFocusHistory(AllottedGeometry, OutDrawElements, LayerId);
	PaintHitTestGrid(AllottedGeometry, OutDrawElements, LayerId);

	ResolveAndDrawLabels(AllottedGeometry, OutDrawElements, LayerId + 50);

	int32 PanelLayerId = LayerId + 100;
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, PanelLayerId, InWidgetStyle,
									bParentEnabled);
}

void SInputFlowOverlay::ResolveAndDrawLabels(const FGeometry& AllottedGeometry,
											 FSlateWindowElementList& OutDrawElements, int32 LayerId) const
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

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints,
										 ESlateDrawEffect::None, Label.Color.CopyWithNewOpacity(0.5f), true, 1.0f);

			FPaintGeometry OriginDot = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(FVector2D(4, 4)),
																		FSlateLayoutTransform(
																			UE::Slate::CastToVector2f(
																				Label.OriginalPos + FVector2D(
																					-2, (Label.Size.Y * 0.5f) - 2))));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, OriginDot, InputFlowStyle::GetBrush("WhiteBrush"),
									   ESlateDrawEffect::None, Label.Color);
		}

		// Draw Box & Text
		FPaintGeometry BoxGeo = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Label.Size),
																 FSlateLayoutTransform(
																	 UE::Slate::CastToVector2f(FinalPos)));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, BoxGeo,
								   InputFlowStyle::GetBrush("ChildWindow.Background"), ESlateDrawEffect::None,
								   InputFlowStyle::Color_LabelBg);

		FVector2D Padding(8.0f, 4.0f);
		FPaintGeometry TextGeo = AllottedGeometry.ToPaintGeometry(
			UE::Slate::CastToVector2f(Label.Size - (Padding * 2.0f)),
			FSlateLayoutTransform(UE::Slate::CastToVector2f(FinalPos + Padding)));

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, TextGeo, Label.Text, FontInfo, ESlateDrawEffect::None,
									FLinearColor::Black); // Shadow
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3, TextGeo, Label.Text, FontInfo, ESlateDrawEffect::None,
									Label.Color); // Main
	}
}

void SInputFlowOverlay::PaintFocusedWidget(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32& LayerId) const
{
	const UInputDebugSubsystem* Sub = GetSubsystem();
	if (!Sub || !UInputFlowSettings::Get()->IsFocusHighlightEnabled()) return;

	// Draw Focus
	if (const TSharedPtr<SWidget> Focus = Sub->GetFocusedWidget())
	{
		int32 FocusLayerId = LayerId + 10;
		const FString FocusLabel = FString::Printf(TEXT("FOCUS: %s"), *InputFlowHelpers::GetWidgetDisplayName(Focus));
		DrawWidgetHighlight(Focus, InputFlowStyle::Color_Focus, FocusLabel, AllottedGeometry, OutDrawElements,
							FocusLayerId, 4.f);

		const FGeometry& FocusGeo = Focus->GetPaintSpaceGeometry();
		if (FocusGeo.GetAbsoluteSize().GetMin() > 0.0f)
		{
			FVector2D Center = AllottedGeometry.AbsoluteToLocal(
				FocusGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

			// Draw Crosshair/Ring
			TArray<FVector2D> RingPoints;
			constexpr int32 Segments = 16;
			constexpr float RingRadius = 6.0f;
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float Angle = ((float)i / (float)Segments) * 2.0f * PI;
				RingPoints.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RingRadius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(), RingPoints,
										 ESlateDrawEffect::None, InputFlowStyle::Color_Focus, true, 3.f);
		}
	}
}

void SInputFlowOverlay::PaintNavigationSimulation(const FGeometry& AllottedGeometry,
                                                  FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	UInputDebugSubsystem* Sub = GetSubsystem();
	if (!Sub || !UInputFlowSettings::Get()->IsNavSimulationEnabled()) return;

	// Draw Links
	const TArray<FNavigationLink>& Links = Sub->GetNavigationLinks();
	const int32 MaxDepth = UInputFlowSettings::Get()->GetNavigationSearchDepth();
	for (const FNavigationLink& Link : Links)
	{
		TSharedPtr<SWidget> Start = Link.StartWidget.Pin();
		TSharedPtr<SWidget> End = Link.EndWidget.Pin();
		if (!Start) continue;

		const bool bIsNormal = (Link.ResultType == ENavSimResult::Normal);
		const bool bIsStopped = (Link.ResultType == ENavSimResult::Stopped);
		const bool bIsHandled = (Link.ResultType == ENavSimResult::Handled);
		const bool bIsExplicit = (Link.ResultType == ENavSimResult::Explicit);

		const float Alpha = FMath::Lerp(1.0f, 0.65f, (float)(Link.DepthStep - 1) / (float)(FMath::Max(MaxDepth - 1, 1)));

		const FString DepthStr = FString::Printf(TEXT("NAV DEPTH [%d/%d]"), Link.DepthStep, MaxDepth);

		if (bIsNormal && End.IsValid())
		{
			FLinearColor LinkColor = InputFlowStyle::Color_NavNormal.CopyWithNewOpacity(Alpha);
			FString EndName = InputFlowHelpers::GetWidgetDisplayName(End);
			FString Label = FString::Printf(TEXT("%s\n%s\nTarget%s"), *DepthStr, *UEnum::GetValueAsString(Link.Direction), *EndName);

			DrawWidgetHighlight(End, LinkColor, Label, AllottedGeometry, OutDrawElements, LayerId);
			DrawConnectionSpline(AllottedGeometry, Start, End, Link.Direction, LinkColor, OutDrawElements, LayerId);
		}
		if (bIsHandled)
		{
			FLinearColor Color = InputFlowStyle::Color_NavHandled.CopyWithNewOpacity(Alpha);
			FString HandlerName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");
			FString StatusLabel = FString::Printf(TEXT("%s\n%s\nHANDLED By %s"), *DepthStr, *UEnum::GetValueAsString(Link.Direction), *HandlerName);
		
			DrawDirectionalIndicator(AllottedGeometry, Start->GetPaintSpaceGeometry(), Link.Direction, StatusLabel,
									 Color, OutDrawElements, LayerId);
			continue;
		}
		if (bIsStopped)
		{
			FLinearColor Color = InputFlowStyle::Color_NavBlocked.CopyWithNewOpacity(Alpha);
			FString BlockerName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");
			FString StatusLabel = FString::Printf(TEXT("%s\n%s\nSTOPPED By %s"), *DepthStr, *UEnum::GetValueAsString(Link.Direction), *BlockerName);

			DrawDirectionalIndicator(AllottedGeometry, Start->GetPaintSpaceGeometry(), Link.Direction, StatusLabel,
									 Color, OutDrawElements, LayerId);
			continue;
		}
		if (bIsExplicit)
		{
			FLinearColor Color = InputFlowStyle::Color_NavExplicit.CopyWithNewOpacity(Alpha);
			FString TargetName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");
			FString StatusLabel = FString::Printf(TEXT("%s\n%s\nEXPLICIT TO By %s"), *DepthStr, *UEnum::GetValueAsString(Link.Direction), *TargetName);

			DrawDirectionalIndicator(AllottedGeometry, Start->GetPaintSpaceGeometry(), Link.Direction, StatusLabel,
									 Color, OutDrawElements, LayerId);
			DrawConnectionSpline(AllottedGeometry, Start, End, Link.Direction, Color.CopyWithNewOpacity(0.5f),
								 OutDrawElements, LayerId);
			DrawWidgetHighlight(End, Color, TargetName, AllottedGeometry, OutDrawElements, LayerId);
			continue;
		}
	}
}

void SInputFlowOverlay::PaintFocusHistory(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
										  int32& LayerId) const
{
	if (!UInputFlowSettings::Get()->IsFocusHighlightEnabled()) return;

	const UInputDebugSubsystem* Subsystem = GetSubsystem();
	if (!IsValid(Subsystem)) return;

	const TArray<FFocusHistoryEntry>& History = Subsystem->GetFocusHistory();
	if (History.IsEmpty()) return;

	const double Now = FPlatformTime::Seconds();
	constexpr double MaxAge = 2.0f;

	TSharedPtr<SWidget> LastCenterWidget = nullptr;
	FVector2D LastCenterPos = FVector2D::ZeroVector;

	for (const FFocusHistoryEntry& Entry : History)
	{
		const double Age = Now - Entry.Timestamp;
		if (Age > MaxAge) continue;

		TSharedPtr<SWidget> Widget = Entry.Widget.Pin();
		if (!Widget.IsValid()) continue;

		const float NormalizedAge = (float)(Age / MaxAge);
		const float Alpha = 1.0f - NormalizedAge;
		FLinearColor Color = InputFlowStyle::Color_Focus.CopyWithNewOpacity(Alpha * 0.5f);

		DrawWidgetHighlight(Widget, Color, FString(), AllottedGeometry, OutDrawElements, LayerId);

		FVector2D CurrentCenter = AllottedGeometry.AbsoluteToLocal(
			Widget->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
		if (LastCenterWidget.IsValid())
		{
			TArray<FVector2D> Points = {LastCenterPos, CurrentCenter};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points,
										 ESlateDrawEffect::None, Color, true, 2.0f);
		}
		LastCenterWidget = Widget;
		LastCenterPos = CurrentCenter;
	}
}

void SInputFlowOverlay::PaintHitTestGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	UInputDebugSubsystem* Subsystem = GetSubsystem();
	if (!IsValid(Subsystem) || !UInputFlowSettings::Get()->IsHitTestGridShown()) return;

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
			FVector2D BottomRight(
				AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f))));
			FVector2D Size = BottomRight - TopLeft;

			FPaintGeometry PaintGeo = AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Size),
																	   FSlateLayoutTransform(
																		   UE::Slate::CastToVector2f(TopLeft)));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo, InputFlowStyle::GetBrush("WhiteBrush"),
									   ESlateDrawEffect::None, FLinearColor(0.0f, 1.0f, 0.9f, 0.03f));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo, InputFlowStyle::GetBrush("Debug.Border"),
									   ESlateDrawEffect::None, FLinearColor(0.0f, 1.0f, 0.9f, 0.3f));
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

	const FVector2D TopLeft(OverlayGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePosition()));
	const FVector2D BottomRight(
		OverlayGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f))));
	const FVector2D Size = BottomRight - TopLeft;

	const FPaintGeometry PaintGeo = OverlayGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(Size),
															  FSlateLayoutTransform(
																  UE::Slate::CastToVector2f(TopLeft)));
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeo, InputFlowStyle::GetBrush("WhiteBrush"),
							   ESlateDrawEffect::None, Color.CopyWithNewOpacity(Color.A * 0.15f));

	const TArray<FVector2D> LinePoints = {
		FVector2D::ZeroVector, FVector2D(Size.X, 0), FVector2D(Size.X, Size.Y), FVector2D(0, Size.Y),
		FVector2D::ZeroVector
	};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeo, LinePoints, ESlateDrawEffect::None, Color,
								 true, Thickness);

	if (!Label.IsEmpty())
	{
		DrawTextLabelWithBackground(OverlayGeometry, TopLeft + FVector2D(0, -22), Label, Color,
									OutDrawElements, LayerId);
	}
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

	const FVector2D StartP = AllottedGeometry.AbsoluteToLocal(
		StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
	const FVector2D EndP = AllottedGeometry.AbsoluteToLocal(EndGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
	const float ControlDist = FMath::Max((EndP - StartP).Size() * 0.5f, 50.0f);

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

	const FVector2D CP1 = StartP + (StartTangent * ControlDist);
	const FVector2D CP2 = EndP - (StartTangent * (ControlDist * 0.5f));

	TArray<FVector2D> Points;
	for (int32 i = 0; i <= 20; ++i)
	{
		const float T = (float)i / 20.0f;
		const float OneMinusT = 1.0f - T;
		Points.Add(
			(OneMinusT * OneMinusT * OneMinusT) * StartP + (3.0f * OneMinusT * OneMinusT * T) * CP1 + (3.0f * OneMinusT
				* T * T) * CP2 + (T * T * T) * EndP);
	}

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points,
								 ESlateDrawEffect::None, Color, true, 3.0f);

	// Arrowhead
	const FVector2D Dir = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
	const FVector2D Tangent = FVector2D(-Dir.Y, Dir.X);
	const TArray<FVector2D> HeadPoints = {
		EndP + Dir * 2.0f, EndP - Dir * 12.0f + Tangent * 7.2f, EndP - Dir * 12.0f - Tangent * 7.2f, EndP + Dir * 2.0f
	};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(), HeadPoints,
								 ESlateDrawEffect::None, Color, true, 3.0f);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
							   AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(FVector2D(1, 1)),
																FSlateLayoutTransform(UE::Slate::CastToVector2f(EndP))),
							   InputFlowStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
}

void SInputFlowOverlay::DrawDirectionalIndicator(const FGeometry& AllottedGeometry, const FGeometry& StartGeo,
												 EUINavigation Direction, const FString& Label,
												 const FLinearColor& Color, FSlateWindowElementList& OutDrawElements,
												 int32& LayerId) const
{
	if (StartGeo.GetAbsoluteSize().IsZero()) return;

	const FVector2D StartTL = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePosition());
	const FVector2D StartBR = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(1, 1)));
	const FVector2D Center = (StartTL + StartBR) * 0.5f;

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

	const FVector2D EndPos = Anchor + (DirVec * 30.0f);
	const TArray<FVector2D> Points = {Anchor, EndPos};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points,
								 ESlateDrawEffect::None, Color, true, 3.0f);

	const FVector2D Tangent(-DirVec.Y, DirVec.X);
	const TArray<FVector2D> BarPoints = {EndPos + (Tangent * 8.0f), EndPos - (Tangent * 8.0f)};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), BarPoints,
								 ESlateDrawEffect::None, Color, true, 3.0f);

	DrawTextLabelWithBackground(AllottedGeometry, EndPos + FVector2D(5, 5), Label, Color, OutDrawElements, LayerId);
}
