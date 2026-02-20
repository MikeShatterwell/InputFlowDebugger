// Copyright Mike Desrosiers, All Rights Reserved.

#include "SInputFlowOverlay.h"

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Input/HittestGrid.h>
#include <Styling/AppStyle.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Images/SImage.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/SCanvas.h>
#include <Widgets/Layout/SDPIScaler.h>

// Internal
#include "InputFlowSettings.h"
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "SCommonUIHierarchyView.h"
#include "SEnhancedInputInspector.h"
#include "SInputFlowLogView.h"
#include "SInputFlowSettingsPanel.h"
#include "SInputFlowStatusDashboard.h"
#include "SMVVMInspectorPanel.h"

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
		bCanSupportFocus = false;

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
				.IsFocusable(false)
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
				.IsFocusable(false)
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
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f))
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
							InArgs
							._Content
							.Widget
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
					// Clamp Initial Position so we don't spawn off-screen
					const float ActualHeight = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;
					FVector2D ClampedPos = PendingInitialPosition;
					ClampedPos.X = FMath::Clamp(ClampedPos.X, 0.0f, FMath::Max(0.0f, ParentSize.X - CurrentSize.X));
					ClampedPos.Y = FMath::Clamp(ClampedPos.Y, 0.0f, FMath::Max(0.0f, ParentSize.Y - ActualHeight));

					UpdateAnchorAndOffset(ClampedPos, ParentSize);
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
			bool bOverHeader = HeaderRow.IsValid() && HeaderRow->GetCachedGeometry().IsUnderLocation(
				MouseEvent.GetScreenSpacePosition());
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

			const TSharedPtr<SWidget> Parent = GetParentWidget();
			if (Parent.IsValid())
			{
				const FVector2D ParentSize = Parent->GetPaintSpaceGeometry().GetLocalSize();
				constexpr float SnapDist = 15.0f;

				if (GetSnapTargetsDelegate.IsBound())
				{
					TArray<FSlateRect> Targets = GetSnapTargetsDelegate.Execute(this);
					const float SnapHeight = bIsMinimized ? InputFlowPanelConstants::HeaderHeight : CurrentSize.Y;
					const float SnapWidth = CurrentSize.X;

					for (const FSlateRect& Target : Targets)
					{
						// Horizontal
						if (FMath::IsNearlyEqual(NewVisualPos.X + SnapWidth, Target.Left, SnapDist))
							NewVisualPos.X = Target.Left - SnapWidth;
						else if (FMath::IsNearlyEqual(NewVisualPos.X, Target.Right, SnapDist))
							NewVisualPos.X = Target.Right;
						else if (FMath::IsNearlyEqual(NewVisualPos.X, Target.Left, SnapDist))
							NewVisualPos.X = Target.Left;
						else if (FMath::IsNearlyEqual(NewVisualPos.X + SnapWidth, Target.Right, SnapDist))
							NewVisualPos.X = Target.Right - SnapWidth;

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

				// Ensures panel never leaves the viewport entirely
				NewVisualPos.X = FMath::Clamp(NewVisualPos.X, 0.0f, FMath::Max(0.0f, ParentSize.X - CurrentSize.X));
				NewVisualPos.Y = FMath::Clamp(NewVisualPos.Y, 0.0f,
											  FMath::Max(0.0f, ParentSize.Y - InputFlowPanelConstants::HeaderHeight));
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
		const EHorizontalAlignment NewHorizontalAlignment = (Center.X > ParentSize.X * 0.5f)
																? HAlign_Right
																: HAlign_Left;
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
// LABEL IMPLEMENTATION
// --------------------------------------------------------------------
void SInputFlowLabel::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	ChildSlot
	[
		SAssignNew(BackgroundBorder, SBorder)
		.Padding(FMargin(12.0f, 6.0f))
		.BorderImage(InputFlowStyle::GetBrush("WhiteBrush"))
		[
			SAssignNew(TextBlock, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ShadowOffset(FVector2D(1, 1))
			.ColorAndOpacity(FLinearColor::White)
			.LineHeightPercentage(1.25f)
		]
	];
}

void SInputFlowLabel::SetData(const FString& Text, const FLinearColor& Color)
{
	if (TextBlock.IsValid())
	{
		TextBlock->SetText(FText::FromString(Text));
		TextBlock->SetColorAndOpacity(Color);
	}
	if (BackgroundBorder.IsValid())
	{
		// Make background a darker version of the label color
		BackgroundBorder->SetBorderBackgroundColor((Color * 0.1f).CopyWithNewOpacity(0.7f));
	}
}

// --------------------------------------------------------------------
// MAIN OVERLAY IMPLEMENTATION
// --------------------------------------------------------------------

void SInputFlowOverlay::Construct(const FArguments& InArgs)
{
	DebugSubsystem = InArgs._Subsystem;
	UInputDebugSubsystem* Sub = DebugSubsystem.Get();
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	QueuedLabels.Reserve(64);

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
		SAssignNew(MainDPIScaler, SDPIScaler)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SAssignNew(RootOverlay, SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(LabelCanvas, SCanvas)
			]
		]
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

#if WITH_PLUGIN_MODELVIEWVIEWMODEL
	AddPanel(
		SNew(SInputFlowDraggablePanel)
		.Title(TEXT("MVVM Inspector"))
		.InitialPosition(FVector2D(100, 300))
		.InitialSize(FVector2D(1000, 500))
		.OnGetSnapTargets_Lambda(GetSnapTargets)
		.OnClose_Lambda([]() { GetMutableDefault<UInputFlowSettings>()->SetShowMVVMInspectorPanel(false); })
		.Visibility_Lambda([this]() -> EVisibility
		{
			return UInputFlowSettings::Get()->IsMVVMInspectorPanelShown()
					   ? EVisibility::SelfHitTestInvisible
					   : EVisibility::Collapsed;
		})
		[
			SAssignNew(MVVMInspectorView, SMVVMInspectorPanel)
		]
	);
#endif
}

void SInputFlowOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!UInputFlowSettings::Get()->IsOverlayEnabled())
	{
		LabelCanvas->SetVisibility(EVisibility::Collapsed);
		return;
	}
	LabelCanvas->SetVisibility(EVisibility::SelfHitTestInvisible);

	// Gather new labels
	QueuedLabels.Reset();
	GatherLabelsFromSubsystem(AllottedGeometry); // Populates QueuedLabels

	// Update Canvas
	UpdateLabelCanvas(AllottedGeometry);
}

UInputDebugSubsystem* SInputFlowOverlay::GetSubsystem() const
{
	return DebugSubsystem.Get();
}

int32 SInputFlowOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
								 const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
								 int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!UInputFlowSettings::Get()->IsOverlayEnabled())
	{
		return LayerId;
	}

	// Apply the UI scale from settings to the ScaleBox
	if (MainDPIScaler.IsValid())
	{
		const float Scale = UInputFlowSettings::Get()->GetOverlayScale();
		MainDPIScaler->SetDPIScale(Scale);
	}

	PaintFocusedWidget(AllottedGeometry, OutDrawElements, LayerId);
	PaintNavigationSimulation(AllottedGeometry, OutDrawElements, LayerId);
	PaintFocusHistory(AllottedGeometry, OutDrawElements, LayerId);
	PaintHitTestGrid(AllottedGeometry, OutDrawElements, LayerId);

	const int32 PanelLayerId = LayerId + 100;
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, PanelLayerId, InWidgetStyle,
									bParentEnabled);
}

void SInputFlowOverlay::PaintFocusedWidget(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
										   int32& LayerId) const
{
	const UInputDebugSubsystem* Sub = GetSubsystem();
	if (!IsValid(Sub) || !UInputFlowSettings::Get()->IsFocusHighlightEnabled()) return;

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
			const float Scale = UInputFlowSettings::Get()->GetOverlayScale();

			FVector2D Center = AllottedGeometry.AbsoluteToLocal(
				FocusGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

			// Draw Crosshair/Ring
			TArray<FVector2D> RingPoints;
			constexpr int32 Segments = 16;
			const float RingRadius = 6.0f * Scale;
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float Angle = ((float)i / (float)Segments) * 2.0f * PI;
				RingPoints.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RingRadius);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 10, AllottedGeometry.ToPaintGeometry(), RingPoints,
										 ESlateDrawEffect::None, InputFlowStyle::Color_Focus, true, 3.f * Scale);
		}
	}
}

void SInputFlowOverlay::PaintNavigationSimulation(const FGeometry& AllottedGeometry,
												  FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	UInputDebugSubsystem* Sub = GetSubsystem();
	if (!Sub || !UInputFlowSettings::Get()->IsNavSimulationEnabled())
	{
		return;
	}

	const TArray<FNavigationLink>& Links = Sub->GetNavigationLinks();
	const int32 MaxDepth = UInputFlowSettings::Get()->GetNavigationSearchDepth();

	for (const FNavigationLink& Link : Links)
	{
		TSharedPtr<SWidget> Start = Link.StartWidget.Pin();
		TSharedPtr<SWidget> End = Link.EndWidget.Pin();

		if (!Start)
		{
			continue;
		}

		const float Alpha = FMath::Lerp(1.0f, 0.65f,
										static_cast<float>(Link.DepthStep - 1) / static_cast<float>(FMath::Max(
											MaxDepth - 1, 1)));
		const FString DirectionStr = UEnum::GetValueAsString(Link.Direction);
		const FString DepthStr = FString::Printf(TEXT("NAV DEPTH [%d/%d]"), Link.DepthStep, MaxDepth);
		const FString TargetName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");
		const bool bDrawNavLabels = UInputFlowSettings::Get()->IsNavLabelsEnabled();
		FString NavLabel;

		auto DrawIndicatorWithLabel = [&](const FLinearColor& BaseColor, const FString& StatusLabel)
		{
			const FLinearColor Color = BaseColor.CopyWithNewOpacity(Alpha);
			DrawDirectionalIndicator(AllottedGeometry, Start->GetPaintSpaceGeometry(), Link.Direction, StatusLabel,
									 Color, OutDrawElements, LayerId);
		};

		switch (Link.ResultType)
		{
		case ENavSimResult::Normal:
			if (End.IsValid())
			{
				const FLinearColor LinkColor = InputFlowStyle::Color_NavNormal.CopyWithNewOpacity(Alpha);
				if (bDrawNavLabels)
				{
					NavLabel = FString::Printf(TEXT("%s\n%s\nTarget: %s"), *DirectionStr, *DepthStr, *TargetName);
				}

				DrawWidgetHighlight(End, LinkColor, NavLabel, AllottedGeometry, OutDrawElements, LayerId);
				DrawConnectionSpline(AllottedGeometry, Start, End, FString(), Link.Direction, LinkColor,
									 OutDrawElements, LayerId);
			}
			break;

		case ENavSimResult::Handled:
			{
				if (bDrawNavLabels)
				{
					NavLabel = FString::Printf(TEXT("%s\n%s\nHANDLED by %s"), *DirectionStr, *DepthStr, *TargetName);
				}
				DrawIndicatorWithLabel(
					InputFlowStyle::Color_NavHandled,
					NavLabel);
				break;
			}


		case ENavSimResult::Stopped:
			{
				if (bDrawNavLabels)
				{
					NavLabel = FString::Printf(TEXT("%s\n%s\nSTOPPED by %s"), *DirectionStr, *DepthStr, *TargetName);
				}
				DrawIndicatorWithLabel(
					InputFlowStyle::Color_NavBlocked,
					NavLabel);
				break;
			}


		case ENavSimResult::Explicit:
			{
				const FLinearColor Color = InputFlowStyle::Color_NavExplicit.CopyWithNewOpacity(Alpha);
				if (bDrawNavLabels)
				{
					NavLabel = FString::Printf(TEXT("%s\n%s\nEXPLICIT to %s"), *DirectionStr, *DepthStr, *TargetName);
				}

				DrawDirectionalIndicator(AllottedGeometry, Start->GetPaintSpaceGeometry(), Link.Direction, NavLabel,
										 Color, OutDrawElements, LayerId);
				DrawConnectionSpline(AllottedGeometry, Start, End, FString(), Link.Direction,
									 Color.CopyWithNewOpacity(0.5f), OutDrawElements, LayerId);
				DrawWidgetHighlight(End, Color, FString(), AllottedGeometry, OutDrawElements, LayerId);
			}
			break;
		}

		// Draw Rejected Navigations
		for (const FRejectedNavigation& Rej : Link.RejectedWidgets)
		{
			TSharedPtr<SWidget> RejWidget = Rej.Widget.Pin();
			if (RejWidget.IsValid())
			{
				const FGeometry& RejGeo = RejWidget->GetPaintSpaceGeometry();
				if (RejGeo.GetAbsoluteSize().IsZero()) continue;

				// Use a faint blocked/red color
				const FLinearColor RejColor = InputFlowStyle::Color_NavBlocked.CopyWithNewOpacity(Alpha * 0.4f);

				const FVector2D StartCenter = AllottedGeometry.AbsoluteToLocal(
					Start->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
				const FVector2D EndCenter = AllottedGeometry.AbsoluteToLocal(
					RejGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

				// Draw faint straight line to indicate the attempt
				TArray<FVector2D> LinePts = {StartCenter, EndCenter};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePts,
											 ESlateDrawEffect::None, RejColor, true, 1.5f);

				// Highlight the rejected widget
				DrawWidgetHighlight(RejWidget, RejColor, Rej.Reason, AllottedGeometry, OutDrawElements, LayerId, 4.0f);
			}
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

void SInputFlowOverlay::PaintHitTestGrid(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
										 int32& LayerId) const
{
	if (!UInputFlowSettings::Get()->IsHitTestGridShown()) return;

#if WITH_SLATE_DEBUGGING
	const TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (!MyWindow.IsValid()) return;

	const FHittestGrid& Grid = MyWindow->GetHittestGrid();
	TArray<FHittestGrid::FWidgetSortData> SortData = Grid.GetAllWidgetSortDatas();

	// Reference area: use the overlay's allotted size (or window size, both are fine).
	const FVector2D OverlaySize = AllottedGeometry.GetAbsoluteSize();
	const float OverlayArea = FMath::Max(OverlaySize.X * OverlaySize.Y, 1.0f);

	// Alpha tuning knobs.
	constexpr float FillAlphaMin = 0.004f; // very faint for huge widgets
	constexpr float FillAlphaMax = 0.050f; // more visible for small widgets
	constexpr float BorderAlphaMin = 0.8f;
	constexpr float BorderAlphaMax = 1.f;

	for (const FHittestGrid::FWidgetSortData& Item : SortData)
	{
		TSharedPtr<SWidget> Widget = Item.WeakWidget.Pin();
		if (!Widget.IsValid() || !Widget->GetVisibility().IsHitTestVisible())
		{
			continue;
		}

		const FGeometry& WidgetGeo = Widget->GetPaintSpaceGeometry();
		if (WidgetGeo.GetAbsoluteSize().IsZero())
		{
			continue;
		}

		const FVector2D AbsPos = WidgetGeo.GetAbsolutePosition();
		const FVector2D AbsSize = WidgetGeo.GetAbsoluteSize();

		// Convert to local space of our overlay so drawing aligns.
		const FVector2D TopLeft = AllottedGeometry.AbsoluteToLocal(AbsPos);
		const FVector2D BottomRight =
			AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f)));
		const FVector2D Size = BottomRight - TopLeft;

		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			continue;
		}

		// ----- Size-based translucency -----
		// Ratio in [0..1+] where 1 ~ "fills overlay". Clamp to [0..1] for mapping.
		const float WidgetArea = FMath::Max(AbsSize.X * AbsSize.Y, 1.0f);
		const float AreaRatio = FMath::Clamp(WidgetArea / OverlayArea, 0.0f, 1.0f);

		// We want: bigger widgets => lower alpha.
		// Invert ratio, then apply a curve to emphasize differences among small widgets.
		const float Smallness = 1.0f - AreaRatio; // 1 for tiny, 0 for huge
		const float Curve = FMath::Pow(Smallness, 1.35f); // tweak exponent to taste

		const float FillAlpha = FMath::Lerp(FillAlphaMin, FillAlphaMax, Curve);
		const float BorderAlpha = FMath::Lerp(BorderAlphaMin, BorderAlphaMax, Curve);

		// ----- Paint-layer tint -----
		// Use a stable hue step so different layers show up as different colors.
		// (Hue in degrees; wrap at 360.)
		const int32 PaintLayer = Item.PrimarySort;
		const float Hue = FMath::Fmod(165.0f + float(PaintLayer) * 27.0f, 360.0f);

		// Keep saturation/value constant, only hue changes by layer.
		const FLinearColor LayerTint = FLinearColor::MakeFromHSV8(
			(uint8)(Hue / 360.0f * 255.0f),
			200, // saturation
			255 // value
		);

		const FLinearColor FillColor = LayerTint.CopyWithNewOpacity(FillAlpha);
		const FLinearColor BorderColor = LayerTint.CopyWithNewOpacity(BorderAlpha);

		const FPaintGeometry PaintGeo =
			AllottedGeometry.ToPaintGeometry(
				UE::Slate::CastToVector2f(Size),
				FSlateLayoutTransform(UE::Slate::CastToVector2f(TopLeft)));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			PaintGeo,
			InputFlowStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FillColor);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			PaintGeo,
			InputFlowStyle::GetBrush("Debug.Border"),
			ESlateDrawEffect::None,
			BorderColor);
	}
#endif
}

// --- VISUAL HELPERS ---

void SInputFlowOverlay::DrawCircle(
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const float Radius,
	const FLinearColor& Color,
	const float Thickness,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId)
{
	constexpr int32 NumSegments = 32;
	TArray<FVector2D> Points;
	Points.Reserve(NumSegments + 1);

	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const float Angle = (static_cast<float>(i) / static_cast<float>(NumSegments)) * 2.0f * UE_PI;
		Points.Add(FVector2D(
			Center.X + FMath::Cos(Angle) * Radius,
			Center.Y + FMath::Sin(Angle) * Radius
		));
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		Color,
		true,
		Thickness
	);
}

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

	const float Scale = UInputFlowSettings::Get()->GetOverlayScale();
	const TArray<FVector2D> LinePoints = {
		FVector2D::ZeroVector, FVector2D(Size.X, 0), FVector2D(Size.X, Size.Y), FVector2D(0, Size.Y),
		FVector2D::ZeroVector
	};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeo, LinePoints, ESlateDrawEffect::None, Color,
								 true, Thickness * Scale);

	if (!Label.IsEmpty())
	{
		QueueLabel(TopLeft, Label, Color);
	}
}

void SInputFlowOverlay::QueueLabel(const FVector2D& Position, const FString& Text, const FLinearColor& Color,
								   const FVector2D& Pivot) const
{
	FQueuedLabel& Item = QueuedLabels.AddDefaulted_GetRef();
	Item.OriginalPos = Position;
	Item.Text = Text;
	Item.Color = Color;
	Item.Pivot = Pivot;
}

void SInputFlowOverlay::GatherLabelsFromSubsystem(const FGeometry& AllottedGeometry)
{
	UInputDebugSubsystem* Sub = GetSubsystem();
	if (!IsValid(Sub)) return;

	// --- FOCUS ---
	if (UInputFlowSettings::Get()->IsFocusHighlightEnabled())
	{
		if (const TSharedPtr<SWidget> Focus = Sub->GetFocusedWidget())
		{
			const FGeometry& WidgetGeo = Focus->GetPaintSpaceGeometry();
			if (WidgetGeo.GetAbsoluteSize().GetMin() > 0.0f)
			{
				const FVector2D TopLeft = AllottedGeometry.AbsoluteToLocal(WidgetGeo.GetAbsolutePosition());
				const FString Label =
					FString::Printf(TEXT("FOCUS: %s"), *InputFlowHelpers::GetWidgetDisplayName(Focus));
				QueueLabel(TopLeft, Label, InputFlowStyle::Color_Focus);
			}
		}
	}

	// --- NAVIGATION ---
	if (UInputFlowSettings::Get()->IsNavSimulationEnabled())
	{
		const TArray<FNavigationLink>& Links = Sub->GetNavigationLinks();
		const int32 MaxDepth = UInputFlowSettings::Get()->GetNavigationSearchDepth();
		const bool bDrawNavLabels = UInputFlowSettings::Get()->IsNavLabelsEnabled();

		for (const FNavigationLink& Link : Links)
		{
			if (!bDrawNavLabels) continue;

			TSharedPtr<SWidget> Start = Link.StartWidget.Pin();
			TSharedPtr<SWidget> End = Link.EndWidget.Pin();
			if (!Start) continue;

			const FString DirectionStr = UEnum::GetValueAsString(Link.Direction);
			const FString DepthStr = FString::Printf(TEXT("[%d/%d]"), Link.DepthStep, MaxDepth);
			const FString TargetName = End.IsValid() ? InputFlowHelpers::GetWidgetDisplayName(End) : TEXT("Unknown");

			// Calculate Source Geometry
			const FGeometry& StartGeo = Start->GetPaintSpaceGeometry();
			const FVector2D StartTL = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePosition());
			const FVector2D StartBR = AllottedGeometry.AbsoluteToLocal(
				StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(1, 1)));
			const FVector2D StartCenter = (StartTL + StartBR) * 0.5f;

			// Determine Label Position & Alignment based on Source Geometry
			FVector2D LabelPos = StartCenter;
			FVector2D LabelPivot = FVector2D(0.5f, 0.5f); // Default Center
			constexpr float OffsetDist = 15.0f; // "A bit away"

			switch (Link.Direction)
			{
			case EUINavigation::Up:
				// Pos: Top Middle of Source - Offset
				LabelPos = FVector2D(StartCenter.X, StartTL.Y - OffsetDist);
			// Pivot: Bottom Middle of Label (0.5, 1.0)
				LabelPivot = FVector2D(0.5f, 1.0f);
				break;
			case EUINavigation::Down:
				// Pos: Bottom Middle of Source + Offset
				LabelPos = FVector2D(StartCenter.X, StartBR.Y + OffsetDist);
			// Pivot: Top Middle of Label (0.5, 0.0)
				LabelPivot = FVector2D(0.5f, 0.0f);
				break;
			case EUINavigation::Left:
				// Pos: Left Middle of Source - Offset
				LabelPos = FVector2D(StartTL.X - OffsetDist, StartCenter.Y);
			// Pivot: Right Middle of Label (1.0, 0.5)
				LabelPivot = FVector2D(1.0f, 0.5f);
				break;
			case EUINavigation::Right:
				// Pos: Right Middle of Source + Offset
				LabelPos = FVector2D(StartBR.X + OffsetDist, StartCenter.Y);
			// Pivot: Left Middle of Label (0.0, 0.5)
				LabelPivot = FVector2D(0.0f, 0.5f);
				break;
			default: break;
			}

			FString NavLabelText;
			FLinearColor LabelColor = InputFlowStyle::Color_NavNormal;

			switch (Link.ResultType)
			{
			case ENavSimResult::Normal:
				if (End.IsValid())
				{
					NavLabelText = FString::Printf(TEXT("%s %s\nTarget: %s"), *DirectionStr, *DepthStr, *TargetName);
					LabelColor = InputFlowStyle::Color_NavNormal;
				}
				break;
			case ENavSimResult::Handled:
				NavLabelText = FString::Printf(TEXT("%s %s\nHANDLED by %s"), *DirectionStr, *DepthStr, *TargetName);
				LabelColor = InputFlowStyle::Color_NavHandled;
				break;
			case ENavSimResult::Stopped:
				NavLabelText = FString::Printf(TEXT("%s %s\nSTOPPED by %s"), *DirectionStr, *DepthStr, *TargetName);
				LabelColor = InputFlowStyle::Color_NavBlocked;
				break;
			case ENavSimResult::Explicit:
				NavLabelText = FString::Printf(TEXT("%s %s\nEXPLICIT to %s"), *DirectionStr, *DepthStr, *TargetName);
				LabelColor = InputFlowStyle::Color_NavExplicit;
				break;
			}

			if (!NavLabelText.IsEmpty())
			{
				QueueLabel(LabelPos, NavLabelText, LabelColor, LabelPivot);
			}

			// Rejection Labels
			for (const FRejectedNavigation& Rej : Link.RejectedWidgets)
			{
				TSharedPtr<SWidget> RejWidget = Rej.Widget.Pin();
				if (RejWidget.IsValid())
				{
					const FGeometry& RejGeo = RejWidget->GetPaintSpaceGeometry();
					const FVector2D RejCenter = AllottedGeometry.AbsoluteToLocal(
						RejGeo.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));

					//QueueLabel(RejCenter, *Rej.Reason, InputFlowStyle::Color_NavBlocked.CopyWithNewOpacity(0.001f), FVector2D(0.5f, 0.5f));
				}
			}
		}
	}
}

void SInputFlowOverlay::UpdateLabelCanvas(const FGeometry& AllottedGeometry)
{
	// Manage Pool Size
	if (ActiveLabels.Num() > QueuedLabels.Num())
	{
		// Deactivate excess
		for (int32 i = QueuedLabels.Num(); i < ActiveLabels.Num(); ++i)
		{
			LabelPool.Add(ActiveLabels[i]);
		}
		ActiveLabels.SetNum(QueuedLabels.Num());
	}
	else if (ActiveLabels.Num() < QueuedLabels.Num())
	{
		// Activate from pool or create
		const int32 Needed = QueuedLabels.Num() - ActiveLabels.Num();
		for (int32 i = 0; i < Needed; ++i)
		{
			TSharedPtr<SInputFlowLabel> Widget;
			if (LabelPool.Num() > 0)
			{
				Widget = LabelPool.Pop();
			}
			else
			{
				Widget = SNew(SInputFlowLabel);
			}
			ActiveLabels.Add(Widget);
		}
	}

	LabelCanvas->ClearChildren();

	// Prepare Physics Items
	TArray<FInputFlowPhysicsItem> PhysicsItems;
	PhysicsItems.Reserve(QueuedLabels.Num());

	const float Scale = UInputFlowSettings::Get()->GetOverlayScale();

	// Initialize Physics Items
	for (int32 i = 0; i < QueuedLabels.Num(); ++i)
	{
		// Update Content
		ActiveLabels[i]->SetData(QueuedLabels[i].Text, QueuedLabels[i].Color);

		// Get Size (SInputFlowLabel has fixed padding/font, so Prepass gives correct size)
		ActiveLabels[i]->SlatePrepass(AllottedGeometry.GetAccumulatedLayoutTransform().GetScale());

		const FVector2D WidgetSize = ActiveLabels[i]->GetDesiredSize();

		FInputFlowPhysicsItem Item;
		Item.Position = (QueuedLabels[i].OriginalPos / Scale) - (WidgetSize * QueuedLabels[i].Pivot);
		Item.TargetPosition = Item.Position;
		Item.Size = ActiveLabels[i]->GetDesiredSize();
		Item.bIsFixed = false;
		PhysicsItems.Add(Item);
	}

	// Solve Collisions (Logical Space)
	// We use the local size, as ScaleBox handles the visual scaling.
	InputFlowHelpers::SolveAABBCollisions(PhysicsItems, AllottedGeometry.GetLocalSize(), 20);

	// Add to Canvas
	for (int32 i = 0; i < ActiveLabels.Num(); ++i)
	{
		const FVector2D FinalPos = PhysicsItems[i].Position;

		LabelCanvas->AddSlot()
				   .Position(FinalPos)
				   .Size(PhysicsItems[i].Size)
		[
			ActiveLabels[i].ToSharedRef()
		];
	}
}

void SInputFlowOverlay::DrawConnectionSpline(const FGeometry& AllottedGeometry, TSharedPtr<SWidget> Start,
											 TSharedPtr<SWidget> End, const FString& Label,
											 const EUINavigation Direction, const FLinearColor Color,
											 FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	const FGeometry& StartGeo = Start->GetPaintSpaceGeometry();
	const FGeometry& EndGeo = End->GetPaintSpaceGeometry();
	if (StartGeo.GetAbsoluteSize().IsZero() || EndGeo.GetAbsoluteSize().IsZero()) return;

	// --- CALCULATE LOCAL BOUNDS ---
	const FVector2D StartTL = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePosition());
	const FVector2D StartBR = AllottedGeometry.AbsoluteToLocal(
		StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f)));
	const FVector2D StartCenter = (StartTL + StartBR) * 0.5f;

	const FVector2D EndTL = AllottedGeometry.AbsoluteToLocal(EndGeo.GetAbsolutePosition());
	const FVector2D EndBR = AllottedGeometry.AbsoluteToLocal(
		EndGeo.GetAbsolutePositionAtCoordinates(FVector2D(1.0f, 1.0f)));
	const FVector2D EndCenter = (EndTL + EndBR) * 0.5f;

	// --- DETERMINE ANCHOR POINTS ---
	FVector2D StartP = StartCenter;
	FVector2D EndP = EndCenter;
	FVector2D StartTangent(0, 0);

	// Determines which edge to leave from, and which edge to arrive at
	switch (Direction)
	{
	case EUINavigation::Left:
		StartP = FVector2D(StartTL.X, StartCenter.Y); // Leave Left Edge
		EndP = FVector2D(EndBR.X, EndCenter.Y); // Enter Right Edge
		StartTangent = FVector2D(-1, 0);
		break;
	case EUINavigation::Right:
		StartP = FVector2D(StartBR.X, StartCenter.Y); // Leave Right Edge
		EndP = FVector2D(EndTL.X, EndCenter.Y); // Enter Left Edge
		StartTangent = FVector2D(1, 0);
		break;
	case EUINavigation::Up:
		StartP = FVector2D(StartCenter.X, StartTL.Y); // Leave Top Edge
		EndP = FVector2D(EndCenter.X, EndBR.Y); // Enter Bottom Edge
		StartTangent = FVector2D(0, -1);
		break;
	case EUINavigation::Down:
		StartP = FVector2D(StartCenter.X, StartBR.Y); // Leave Bottom Edge
		EndP = FVector2D(EndCenter.X, EndTL.Y); // Enter Top Edge
		StartTangent = FVector2D(0, 1);
		break;
	default:
		// Fallback for Next/Previous/Num or custom types to use Center-to-Center
		// We can just set a default tangent (e.g. Right) for the curve math
		StartTangent = FVector2D(1, 0);
		break;
	}

	// --- COMPUTE SPLINE ---
	// Calculate distance between edges
	const float Dist = (EndP - StartP).Size();

	// Dynamic control distance: 
	// If widgets are very close, reduce the curve intensity to prevent looping.
	// If far, clamp to a reasonable max so lines don't swing wildly off-screen.
	const float ControlDist = FMath::Clamp(Dist * 0.5f, 20.0f, 150.0f);

	const FVector2D CP1 = StartP + (StartTangent * ControlDist);
	const FVector2D CP2 = EndP - (StartTangent * (ControlDist * 0.8f)); // Slightly tighter entry curve

	TArray<FVector2D> Points;
	// Increased segments slightly for smoother curves on long distances
	for (int32 i = 0; i <= 24; ++i)
	{
		const float T = (float)i / 24.0f;
		const float OneMinusT = 1.0f - T;
		Points.Add(
			(OneMinusT * OneMinusT * OneMinusT) * StartP +
			(3.0f * OneMinusT * OneMinusT * T) * CP1 +
			(3.0f * OneMinusT * T * T) * CP2 +
			(T * T * T) * EndP);
	}

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points,
								 ESlateDrawEffect::None, Color, true, 3.0f);

	// --- ARROWHEAD ---
	const FVector2D Dir = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
	const FVector2D Tangent = FVector2D(-Dir.Y, Dir.X);
	const TArray<FVector2D> HeadPoints = {
		EndP + Dir * 2.0f,
		EndP - Dir * 12.0f + Tangent * 7.2f,
		EndP - Dir * 12.0f - Tangent * 7.2f,
		EndP + Dir * 2.0f
	};

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(), HeadPoints,
								 ESlateDrawEffect::None, Color, true, 6.0f);

	// Small dot at target contact point
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
							   AllottedGeometry.ToPaintGeometry(UE::Slate::CastToVector2f(FVector2D(4, 4)),
																FSlateLayoutTransform(
																	UE::Slate::CastToVector2f(EndP - FVector2D(2, 2)))),
							   InputFlowStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);

	if (!Label.IsEmpty())
	{
		constexpr float T = 0.5f;
		constexpr float OneMinusT = 0.5f;
		const FVector2D CenterPos =
			(OneMinusT * OneMinusT * OneMinusT) * StartP +
			(3.0f * OneMinusT * OneMinusT * T) * CP1 +
			(3.0f * OneMinusT * T * T) * CP2 +
			(T * T * T) * EndP;

		QueueLabel(CenterPos + FVector2D(0, -10), Label, Color);
	}
}

void SInputFlowOverlay::DrawDirectionalIndicator(const FGeometry& AllottedGeometry, const FGeometry& StartGeo,
												 EUINavigation Direction, const FString& Label,
												 const FLinearColor& Color, FSlateWindowElementList& OutDrawElements,
												 int32& LayerId) const
{
	if (StartGeo.GetAbsoluteSize().IsZero()) return;

	const FVector2D StartTL = AllottedGeometry.AbsoluteToLocal(StartGeo.GetAbsolutePosition());
	const FVector2D StartBR = AllottedGeometry.AbsoluteToLocal(
		StartGeo.GetAbsolutePositionAtCoordinates(FVector2D(1, 1)));
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
}
