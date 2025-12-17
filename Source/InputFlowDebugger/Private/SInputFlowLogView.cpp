// Copyright Mike Desrosiers, All Rights Reserved.

#include "SInputFlowLogView.h"

// Slate
#include <Styling/AppStyle.h>
#include <Widgets/Images/SImage.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SHyperlink.h>
#include <Widgets/Input/SSearchBox.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "InputFlowSpy.h"

class SInputLogTableRow : public STableRow<TSharedPtr<FInputEventLog>>
{
public:
	SLATE_BEGIN_ARGS(SInputLogTableRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FInputEventLog> InItem, bool bInOverlay)
	{
		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
		
		FName StyleName = bInOverlay ? "TableView.Row" : "SimpleTableView.Row";

		STableRow<TSharedPtr<FInputEventLog>>::Construct(
			STableRow<TSharedPtr<FInputEventLog>>::FArguments()
			.Style(FAppStyle::Get(), StyleName)
			.ShowSelection(!bInOverlay),
			InOwnerTableView
		);
		
		if (bInOverlay)
		{
			SetBorderBackgroundColor(FLinearColor::Transparent);
		}

		Item = InItem;
		
		// Build the Row Widget
		FLinearColor BadgeColor = InItem->Color;
		FString TypeStr = InItem->EventType;
		
		FString TimeStr = FString::Printf(TEXT("%s.%03d"), 
			*InItem->CaptureTime.ToString(TEXT("%H:%M:%S")), 
			InItem->CaptureTime.GetMillisecond());

		TSharedRef<SWidget> NameWidget = SNullWidget::NullWidget;
		
		// Helper to configure text blocks for overlay wrapping
		auto ConfigureText = [&](TSharedRef<STextBlock> Block)
		{
			if (bInOverlay)
			{
				Block->SetAutoWrapText(true);
				Block->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
			}
		};

		// If we have rich parts, build a horizontal box of links/text
		if (InItem->RichTextParts.Num() > 0 && !bInOverlay)
		{
			TSharedPtr<SHorizontalBox> Box = SNew(SHorizontalBox);

			for (const FInputLogRichTextPart& Part : InItem->RichTextParts)
			{
				if (Part.bIsLink)
				{
					Box->AddSlot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SHyperlink)
						.Text(FText::FromString(Part.Text))
						.Style(FCoreStyle::Get(), "Hyperlink")
						.OnNavigate_Lambda([Part]() { InputFlowHelpers::TryOpenAsset(Part.Object, Part.Class, Part.bOpenRootContext); })
					];
				}
				else
				{
					Box->AddSlot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Part.Text))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
					];
				}
			}
			NameWidget = Box.ToSharedRef();
		}
		else
		{
			// Fallback / Overlay mode / Simple Log
			bool bCanClick = (InItem->SourceObject.IsValid() || InItem->SourceClass.IsValid()) && !bInOverlay;
			auto BaseNameFont = InItem->bIsButton ? FCoreStyle::GetDefaultFontStyle("Bold", 10) : FCoreStyle::GetDefaultFontStyle("Regular", 9);

			if (bCanClick)
			{
				NameWidget = SNew(SHyperlink)
					.Text(FText::FromString(InItem->WidgetName))
					.Style(FCoreStyle::Get(), "Hyperlink")
					.OnNavigate_Lambda([InItem]() { InputFlowHelpers::TryOpenAsset(InItem->SourceObject, InItem->SourceClass); });
			}
			else
			{
				FLinearColor NameColor = bInOverlay ? FLinearColor::White : (InItem->bIsButton ? FLinearColor::Black : FLinearColor(0.3f, 0.3f, 0.3f));
				FString DisplayText = InItem->WidgetName.IsEmpty() ? InItem->WidgetType : InItem->WidgetName;
				
				TSharedRef<STextBlock> TextBlock = SNew(STextBlock)
					.Text(FText::FromString(DisplayText))
					.Font(BaseNameFont)
					.ColorAndOpacity(NameColor);
				
				ConfigureText(TextBlock);
				NameWidget = TextBlock;
			}
		}

		TSharedRef<SWidget> MiddleContent = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			.HAlign(HAlign_Left)
			[
				NameWidget
			]
			+ SVerticalBox::Slot().AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InItem->WidgetState))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
			];

		FString CountStr = (InItem->Count > 1) ? FString::Printf(TEXT(" (%d)"), InItem->Count) : FString();
		
		TSharedRef<STextBlock> InputDetailsBlock = SNew(STextBlock)
			.Text(FText::FromString(InItem->InputDetails + CountStr))
			.ColorAndOpacity(bInOverlay ? FLinearColor::White : FLinearColor::Black)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.Justification(ETextJustify::Right);
		
		ConfigureText(InputDetailsBlock);

		this->ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([InItem, bInOverlay]()
			{
				double Age = FPlatformTime::Seconds() - InItem->TimeSeconds;
				
				if (bInOverlay)
				{
					// Overlay Logic: Start dark with opacity, fade to transparent
					float Alpha = FMath::Clamp(0.8f - (Age * 0.2f), 0.0f, 0.8f);
					return FLinearColor(0.0f, 0.0f, 0.0f, Alpha);
				}
				else
				{
					// Editor Logic: Start White (flash), fade to transparent (showing table row color)
					float Alpha = FMath::Clamp(1.0f - (Age / 1.0f), 0.0f, 0.4f);
					return FLinearColor(1.0f, 1.0f, 1.0f, Alpha);
				}
			})
			.Padding(4)
			[
				SNew(SHorizontalBox)
				// Time
				+ SHorizontalBox::Slot().AutoWidth().MinWidth(70).VAlign(VAlign_Top) // VAlign Top for multiline
				[
					SNew(STextBlock)
					.Text(FText::FromString(TimeStr))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
				]
				// Badge (Event Type)
				+ SHorizontalBox::Slot().AutoWidth().MinWidth(90).Padding(4, 0).VAlign(VAlign_Top)
				[
					SNew(SBorder)
					.Padding(FMargin(6, 1))
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(BadgeColor.CopyWithNewOpacity(0.2f))
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TypeStr))
						.ColorAndOpacity(BadgeColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					]
				]
				// Widget Info (Middle Content)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8, 0)
				.VAlign(VAlign_Top)
				[
					MiddleContent
				]
				// Input Details
				+ SHorizontalBox::Slot()
				.AutoWidth() // In Overlay this might need Fill if text is huge, but Auto usually works with wrapping enabled
				.MinWidth(80)
				.MaxWidth(200) // Constraint width in overlay to force wrap
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				.Padding(8, 0)
				[
					InputDetailsBlock
				]
			]
		];
	}

private:
	TSharedPtr<FInputEventLog> Item;
};

void SInputFlowLogView::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	DebugSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;

	TSharedPtr<SWidget> ToolbarWidget;

	if (!bIsOverlay)
	{
		ToolbarWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0).VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
				.HintText(FText::FromString("Filter events..."))
				.OnTextChanged(this, &SInputFlowLogView::OnSearchTextChanged)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SInputFlowLogView::GetPauseState)
				.OnCheckStateChanged(this, &SInputFlowLogView::OnTogglePause)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[ SNew(SImage).Image(FAppStyle::GetBrush(TEXT("GenericPause"))) ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
					[ SNew(STextBlock).Text(FText::FromString("Freeze")) ]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.OnClicked_Lambda([this]() { ClearLog(); return FReply::Handled(); })
				.ContentPadding(FMargin(5, 2))
				[ SNew(STextBlock).Text(FText::FromString("Clear")) ]
			];
	}
	else
	{
		ToolbarWidget = SNew(STextBlock)
			.Text(FText::FromString("Input Event Log"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor::Green);
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(bIsOverlay))
		.Padding(bIsOverlay ? 0.0f : 2.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				ToolbarWidget.ToSharedRef()
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FInputEventLog>>)
				.ListItemsSource(&SourceData)
				.OnGenerateRow(this, &SInputFlowLogView::GenerateRow)
				.SelectionMode(bIsOverlay ? ESelectionMode::None : ESelectionMode::Single)
				.IsFocusable(!bIsOverlay)
			]
		]
	];
}

void SInputFlowLogView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UpdateLogView();
}

void SInputFlowLogView::ClearLog()
{
	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}
	
	if (DebugSubsystem.IsValid()) DebugSubsystem->ClearLogHistory();
	SourceData.Empty();
	ListView->RequestListRefresh();
}

void SInputFlowLogView::OnSearchTextChanged(const FText& InText)
{
	LogFilterText = InText.ToString();
	LastObservedVersion = 0; // Reset version to force update
}

void SInputFlowLogView::OnTogglePause(ECheckBoxState State)
{
	bLogPaused = (State == ECheckBoxState::Checked);
}

ECheckBoxState SInputFlowLogView::GetPauseState() const
{
	return bLogPaused ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SInputFlowLogView::UpdateLogView()
{
	constexpr int32 OverlayMaxEntries = 64;
	
	// 1. Recover Subsystem if lost (e.g. PIE restart)
	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (!DebugSubsystem.IsValid()) return;

	// If paused, do not update view
	if (bLogPaused) return;

	const uint32 CurrentVersion = DebugSubsystem->GetLogVersion();
	if (CurrentVersion == LastObservedVersion) return;
	
	LastObservedVersion = CurrentVersion;

	const TArray<TSharedPtr<FInputEventLog>>& Buffer = DebugSubsystem->GetLogHistory();
	int32 WriteIdx = DebugSubsystem->GetLogHistoryWriteIndex();
	bool bWrapped = DebugSubsystem->IsLogHistoryWrapped();

	SourceData.Reset();
	const bool bFilter = !LogFilterText.IsEmpty();

	// Iterate Ring Buffer Chronologically
	// If wrapped: [WriteIdx -> End] then [0 -> WriteIdx-1]
	// If not wrapped: [0 -> WriteIdx-1]
	
	int32 Start = bWrapped ? WriteIdx : 0;
	int32 TotalItems = bWrapped ? Buffer.Num() : WriteIdx;

	if (bIsOverlay && TotalItems > OverlayMaxEntries)
	{
		Start = (Start + TotalItems - OverlayMaxEntries) % Buffer.Num();
		TotalItems = OverlayMaxEntries;
	}

	for (int32 i = 0; i < TotalItems; ++i)
	{
		int32 CurrIdx = (Start + i) % Buffer.Num();
		if (!Buffer.IsValidIndex(CurrIdx)) continue;
		
		const TSharedPtr<FInputEventLog>& Log = Buffer[CurrIdx];

		if (!Log.IsValid()) continue; 

		if (bFilter)
		{
			if (!Log->EventType.Contains(LogFilterText) && 
				!Log->WidgetName.Contains(LogFilterText) &&
				!Log->InputDetails.Contains(LogFilterText))
			{
				continue;
			}
		}
		SourceData.Add(Log);
	}

	ListView->RequestListRefresh();
	if (!bLogPaused && !bIsOverlay) ListView->ScrollToBottom();
	if (bIsOverlay) ListView->ScrollToBottom();
}

TSharedRef<ITableRow> SInputFlowLogView::GenerateRow(TSharedPtr<FInputEventLog> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SInputLogTableRow, OwnerTable, Item, bIsOverlay);
}