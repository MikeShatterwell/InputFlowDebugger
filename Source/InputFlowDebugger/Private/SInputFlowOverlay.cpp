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

class SInputFlowDraggablePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowDraggablePanel)
		{
		}

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			InArgs
			._Content
			.Widget
		];
		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
		SetVisibility(EVisibility::Visible);
	}

	virtual bool SupportsKeyboardFocus() const override { return false; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bDragging = true;
			DragOffset = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			return FReply::Handled().CaptureMouse(AsShared());
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bDragging = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging)
		{
			FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			FVector2D Delta = LocalMouse - DragOffset;
			CurrentOffset += Delta;
			SetRenderTransform(FSlateRenderTransform(CurrentOffset));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	bool bDragging = false;
	FVector2D DragOffset = FVector2D::ZeroVector;
	FVector2D CurrentOffset = FVector2D::ZeroVector;
};

void SInputFlowOverlay::Construct(const FArguments& InArgs)
{
	DebugSubsystem = InArgs._Subsystem;
	UInputDebugSubsystem* Sub = DebugSubsystem.Get();
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	SetVisibility(EVisibility::SelfHitTestInvisible);

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
			[
				SNew(SBox)
				.WidthOverride(500.0f)
				.HeightOverride(300.0f)
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
		]

		// --- RIGHT PANEL: ANALYZER DATA ---
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SInputFlowDraggablePanel)
			[
				SNew(SBox)
				.WidthOverride(400.0f)
				[
					SNew(SBorder)
					.BorderImage(InputFlowStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
					.Padding(10.0f)
					[
						SNew(SVerticalBox)

						// Hierarchy
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
						[
							SNew(SBox)
							.MaxDesiredHeight(300.0f)
							[
								SAssignNew(HierarchyView, SCommonUIHierarchyView, Sub)
								.IsOverlay(true)
							]
						]

						// Enhanced Input
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
							.MaxDesiredHeight(200.0f)
							[
								SAssignNew(InspectorView, SEnhancedInputInspector, Sub)
								.IsOverlay(true)
							]
						]
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
	// Paint the UI child (the log boxes) normally
	LayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
	                                   bParentEnabled);

	PaintNavigationSimulation(AllottedGeometry, OutDrawElements, LayerId);
	PaintFocusHistory(AllottedGeometry, OutDrawElements, LayerId);
	PaintHitTestGrid(AllottedGeometry, OutDrawElements, LayerId);

	return LayerId;
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
			FString StatusLabel = FString::Printf(TEXT("%s\n%s"), bIsStopped ? TEXT("BLOCKED By") : TEXT("HANDLED By"),
			                                      *BlockerName);

			// 1. Calculate Exit Geometry on the SOURCE widget
			const FGeometry& StartGeo = Start->GetPaintSpaceGeometry();
			FVector2D LocalExitCoord; // UV Space (0-1)
			FVector2D DirectionVec; // Screen Space Direction

			switch (Link.Direction)
			{
			case EUINavigation::Up:
				LocalExitCoord = FVector2D(0.5f, 0.0f); // Top Center
				DirectionVec = FVector2D(0.0f, -1.0f);
				break;
			case EUINavigation::Down:
				LocalExitCoord = FVector2D(0.5f, 1.0f); // Bottom Center
				DirectionVec = FVector2D(0.0f, 1.0f);
				break;
			case EUINavigation::Left:
				LocalExitCoord = FVector2D(0.0f, 0.5f); // Left Center
				DirectionVec = FVector2D(-1.0f, 0.0f);
				break;
			case EUINavigation::Right:
				LocalExitCoord = FVector2D(1.0f, 0.5f); // Right Center
				DirectionVec = FVector2D(1.0f, 0.0f);
				break;
			default:
				LocalExitCoord = FVector2D(0.5f, 0.5f);
				DirectionVec = FVector2D(0.0f, 0.0f);
			}

			// Transform Exit Point to Overlay Space
			FVector2D StartPoint = AllottedGeometry.AbsoluteToLocal(
				StartGeo.GetAbsolutePositionAtCoordinates(LocalExitCoord)
			);

			// Calculate "Stub" End Point (Short distance out)
			constexpr float StubLength = 40.0f;
			FVector2D EndPoint = StartPoint + (DirectionVec * StubLength);

			// 2. Draw the Line
			TArray<FVector2D> LinePoints;
			LinePoints.Add(StartPoint);
			LinePoints.Add(EndPoint);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				Color,
				true,
				2.0f
			);

			// 3. Draw a "T-Bar" at the end to represent the wall
			FVector2D Perpendicular = FVector2D(DirectionVec.Y, -DirectionVec.X); // Rotate 90 deg
			constexpr float BarSize = 10.0f;

			TArray<FVector2D> BarPoints;
			BarPoints.Add(EndPoint + (Perpendicular * BarSize));
			BarPoints.Add(EndPoint - (Perpendicular * BarSize));

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				BarPoints,
				ESlateDrawEffect::None,
				Color,
				true,
				4.0f // Thicker bar
			);

			// 4. Draw the Label at the stub tip
			const FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Regular", 9);
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->
				GetFontMeasureService();
			FVector2D TextSize = FontMeasure->Measure(StatusLabel, FontInfo);

			// Offset text slightly so it doesn't overlap the bar
			FVector2D TextPos = EndPoint + (DirectionVec * 5.0f);

			// Center alignment adjustment
			if (Link.Direction == EUINavigation::Up) TextPos -= FVector2D(TextSize.X * 0.5f, TextSize.Y);
			else if (Link.Direction == EUINavigation::Down) TextPos -= FVector2D(TextSize.X * 0.5f, 0.0f);
			else if (Link.Direction == EUINavigation::Left) TextPos -= FVector2D(TextSize.X, TextSize.Y * 0.5f);
			else if (Link.Direction == EUINavigation::Right) TextPos -= FVector2D(0.0f, TextSize.Y * 0.5f);

			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(
					UE::Slate::CastToVector2f(TextSize),
					FSlateLayoutTransform(UE::Slate::CastToVector2f(TextPos))
				),
				StatusLabel,
				FontInfo,
				ESlateDrawEffect::None,
				Color
			);
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
	FVector2D TextSize = FontMeasure->Measure(Text, FontInfo);
	FVector2D Padding(8.0f, 4.0f);
	FVector2D BoxSize = TextSize + (Padding * 2.0f);

	FPaintGeometry BoxGeo = AllottedGeometry.ToPaintGeometry(
		UE::Slate::CastToVector2f(BoxSize),
		FSlateLayoutTransform(UE::Slate::CastToVector2f(Position))
	);

	// Rounded Box Background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 5,
		BoxGeo,
		InputFlowStyle::GetBrush("ChildWindow.Background"),
		ESlateDrawEffect::None,
		InputFlowStyle::Color_LabelBg
	);

	// Text
	FPaintGeometry TextGeo = AllottedGeometry.ToPaintGeometry(
		UE::Slate::CastToVector2f(TextSize),
		FSlateLayoutTransform(UE::Slate::CastToVector2f(Position + Padding))
	);

	// Drop Shadow Text
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 6,
		TextGeo,
		Text,
		FontInfo,
		ESlateDrawEffect::None,
		FLinearColor::Black
	);

	// Main Text
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 7,
		TextGeo,
		Text,
		FontInfo,
		ESlateDrawEffect::None,
		Color
	);
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
