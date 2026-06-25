// Copyright Mike Desrosiers, All Rights Reserved.

#include "SInputFlowLogView.h"

// ApplicationCore
#include <HAL/PlatformApplicationMisc.h>

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
#include <Widgets/Input/SComboButton.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "InputFlowSpy.h"
#include "InputFlowSettings.h"

// -----------------------------------------------------------------------------
// SInputLogTableRow Implementation
// -----------------------------------------------------------------------------

void SInputLogTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView,
								  TSharedPtr<FInputEventLog> InItem, SInputFlowLogView* InOwnerView, bool bInOverlay,
								  bool bIsSameFrameAsPrev)
{
	Item = InItem;
	OwnerView = InOwnerView;
	bIsOverlay = bInOverlay;
	bIsSameFrame = bIsSameFrameAsPrev;
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	const FTableRowStyle& RowStyle = bInOverlay
										 ? InputFlowHelpers::GetTranslucentRowStyle()
										 : FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");

	SMultiColumnTableRow<TSharedPtr<FInputEventLog>>::Construct(
		SMultiColumnTableRow<TSharedPtr<FInputEventLog>>::FArguments()
		.Style(&RowStyle)
		.ShowSelection(!bInOverlay)
		.Padding(FMargin(0, 2)),
		InOwnerTableView
	);

	if (bInOverlay)
	{
		SetBorderBackgroundColor(FLinearColor::Transparent);
	}
}

TSharedRef<SWidget> SInputLogTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (!Item.IsValid()) return SNullWidget::NullWidget;

	TSharedPtr<SWidget> CellContent = SNullWidget::NullWidget;

	// --- Time Column ---
	if (ColumnName == InputFlowLogColumns::Time)
	{
		if (!bIsSameFrame)
		{
			FString TimeStr = FString::Printf(TEXT("%s.%03d"),
											  *Item->CaptureTime.ToString(TEXT("%H:%M:%S")),
											  Item->CaptureTime.GetMillisecond());

			CellContent = SNew(STextBlock)
				.Text(FText::FromString(TimeStr))
				.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
		}
	}
	// --- Type Column ---
	else if (ColumnName == InputFlowLogColumns::Type)
	{
		CellContent = SNew(SBorder)
			.Padding(FMargin(4, 1))
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(Item->Color).CopyWithNewOpacity(0.2f))
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->EventType))
				.ColorAndOpacity(Item->Color)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			];
	}
	// --- Widget Column ---
	else if (ColumnName == InputFlowLogColumns::Widget)
	{
		// Rich Text / Link Handling
		if (Item->RichTextParts.Num() > 0 && !bIsOverlay)
		{
			TSharedPtr<SHorizontalBox> RowBox = SNew(SHorizontalBox);
			for (const FInputLogRichTextPart& Part : Item->RichTextParts)
			{
				if (Part.bIsLink)
				{
					RowBox->AddSlot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SHyperlink)
						.Text(FText::FromString(Part.Text))
						.Style(FCoreStyle::Get(), "Hyperlink")
						.OnNavigate_Lambda([Part]()
						{
							InputFlowHelpers::TryOpenAsset(Part.Object, Part.Class, Part.bOpenRootContext);
						})
					];
				}
				else
				{
					RowBox->AddSlot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Part.Text))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
					];
				}
			}
			CellContent = RowBox;
		}
		else
		{
			// Simple Text Fallback
			const FLinearColor NameColor = bIsOverlay
											   ? FLinearColor::White
											   : (Item->bIsButton
													  ? FLinearColor::Black
													  : FLinearColor(0.3f, 0.3f, 0.3f));
			const FString DisplayText = Item->WidgetName.IsEmpty() ? Item->WidgetType : Item->WidgetName;

			CellContent = SNew(STextBlock)
				.Text(FText::FromString(DisplayText))
				.Font(Item->bIsButton
						  ? FCoreStyle::GetDefaultFontStyle("Bold", 9)
						  : FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(NameColor);
		}
	}
	// --- State Column ---
	else if (ColumnName == InputFlowLogColumns::State)
	{
		CellContent = SNew(STextBlock)
			.Text(FText::FromString(Item->WidgetState))
			.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
	}
	// --- Details Column ---
	else if (ColumnName == InputFlowLogColumns::Details)
	{
		FString DisplayStr = Item->InputDetails;
		if (Item->Count > 1)
		{
			DisplayStr += FString::Printf(TEXT(" (%d)"), Item->Count);
		}

		CellContent = SNew(STextBlock)
			.Text(FText::FromString(DisplayStr))
			.ColorAndOpacity(FLinearColor::White)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
	}

	// Wrap the content in an SBorder that can detect Right Clicks.
	// Since we are overriding the cell content, the STableRow's internal logic won't receive the Right Click
	// if we handle it here, so we must manually ensure selection happens.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBrush"))
		.Padding(FMargin(4, 0))
		.VAlign(VAlign_Center)
		.OnMouseButtonUp(this, &SInputLogTableRow::OnCellRightClick, ColumnName)
		[
			CellContent.IsValid() ? CellContent.ToSharedRef() : SNullWidget::NullWidget
		];
}

FReply SInputLogTableRow::OnCellRightClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FName ColumnId)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (OwnerView && Item.IsValid() && OwnerTablePtr.IsValid())
		{
			// Manually select this row (mimicking STableRow behavior since we intercepted the click)
			TSharedPtr<ITypedTableView<TSharedPtr<FInputEventLog>>> Table = OwnerTablePtr.Pin();
			if (Table.IsValid())
			{
				const bool bIsAlreadySelected = Table->Private_IsItemSelected(Item);

				// Clear existing selection if Control isn't held (standard behavior)
				if (!bIsAlreadySelected)
				{
					if (!MouseEvent.IsControlDown())
					{
						Table->Private_ClearSelection();
					}
					Table->Private_SetItemSelection(Item, true);
				}
			}

			// Spawn the Column-Aware Context Menu
			const TSharedPtr<SWidget> MenuContent = OwnerView->MakeRowContextMenu(Item, ColumnId);
			if (MenuContent.IsValid())
			{
				const FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr
												   ? *MouseEvent.GetEventPath()
												   : FWidgetPath();
				FSlateApplication::Get().PushMenu(
					AsShared(),
					WidgetPath,
					MenuContent.ToSharedRef(),
					MouseEvent.GetScreenSpacePosition(),
					FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
				);

				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

// -----------------------------------------------------------------------------
// SInputFlowLogView Implementation
// -----------------------------------------------------------------------------

void SInputFlowLogView::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	DebugSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;

	SetVisibility(EVisibility::SelfHitTestInvisible);

	const TSharedPtr<SWidget> ToolbarWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0).VAlign(VAlign_Center)
		[
			SAssignNew(SearchBoxWidget, SSearchBox)
			.HintText(FText::FromString("Search log..."))
			.OnTextChanged(this, &SInputFlowLogView::OnSearchTextChanged)
		]
		// Capture Settings Button
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "GenericFilters.ComboButtonStyle")
			.IsFocusable(false)
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(0)
			.ToolTipText(FText::FromString("Configure what events are recorded"))
			.OnGetMenuContent(this, &SInputFlowLogView::MakeCaptureMenu)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					// Using "Icons.Settings" to differentiate from the view filter
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings")) 
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString("Capture"))
				]
			]
		]
		// Filter Dropdown
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "GenericFilters.ComboButtonStyle")
			.IsFocusable(false)
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(0)
			.ToolTipText(FText::FromString("Filter Event Types"))
			.OnGetMenuContent(this, &SInputFlowLogView::MakeFilterMenu)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Filter"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString("Types"))
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsFocusable(false)
			.IsChecked(this, &SInputFlowLogView::GetPauseState)
			.OnCheckStateChanged(this, &SInputFlowLogView::OnTogglePause)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SImage).Image(FAppStyle::GetBrush(TEXT("GenericPause")))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
				[
					SNew(STextBlock).Text(FText::FromString("Freeze"))
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				ClearLog();
				return FReply::Handled();
			})
			.IsFocusable(false)
			.ContentPadding(FMargin(5, 2))
			[
				SNew(STextBlock).Text(FText::FromString("Clear"))
			]
		];

	const FTableViewStyle& ListStyle = bIsOverlay
										   ? InputFlowHelpers::GetTranslucentTableViewStyle()
										   : FCoreStyle::Get().GetWidgetStyle<FTableViewStyle>("ListView");

	// Header Construction
	const TSharedRef<SHeaderRow> HeaderRow = SNew(SHeaderRow)
		.ResizeMode(ESplitterResizeMode::Fill);

	HeaderRow->AddColumn(
		SHeaderRow::Column(InputFlowLogColumns::Time)
		.DefaultLabel(FText::FromString("Time"))
		.ManualWidth(100.0f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(InputFlowLogColumns::Type)
		.DefaultLabel(FText::FromString("Type"))
		.ManualWidth(100.0f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(InputFlowLogColumns::Widget)
		.DefaultLabel(FText::FromString("Widget"))
		.FillWidth(0.35f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(InputFlowLogColumns::State)
		.DefaultLabel(FText::FromString("State"))
		.FillWidth(0.25f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(InputFlowLogColumns::Details)
		.DefaultLabel(FText::FromString("Details"))
		.FillWidth(0.40f)
	);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(bIsOverlay))
		.Padding(bIsOverlay ? 0.0f : 2.0f)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.BorderBackgroundColor(bIsOverlay ? FLinearColor::Transparent : FLinearColor::White)
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
																		   .OnGenerateRow(
																			   this, &SInputFlowLogView::GenerateRow)
				//.SelectionMode(bIsOverlay ? ESelectionMode::None : ESelectionMode::Multi)
																		   .IsFocusable(false)
																		   .ListViewStyle(&ListStyle)
																		   .HeaderRow(HeaderRow)
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

bool SInputFlowLogView::IsItemVisible(const TSharedPtr<FInputEventLog>& Item) const
{
	if (!Item.IsValid()) return false;

	if (HiddenEventTypes.Contains(Item->EventType))
	{
		return false;
	}

	// Check Column Exclusion Filters
	if (ExcludedColumnValues.Contains(InputFlowLogColumns::Type) &&
		ExcludedColumnValues[InputFlowLogColumns::Type].Contains(Item->EventType))
	{
		return false;
	}
	if (ExcludedColumnValues.Contains(InputFlowLogColumns::Widget) &&
		ExcludedColumnValues[InputFlowLogColumns::Widget].Contains(Item->WidgetName))
	{
		return false;
	}

	// Check Text Filter
	if (!LogFilterText.IsEmpty())
	{
		if (!Item->EventType.Contains(LogFilterText) &&
			!Item->WidgetName.Contains(LogFilterText) &&
			!Item->InputDetails.Contains(LogFilterText) &&
			!Item->WidgetState.Contains(LogFilterText))
		{
			return false;
		}
	}

	return true;
}

void SInputFlowLogView::ToggleEventTypeFilter(FString EventType)
{
	if (HiddenEventTypes.Contains(EventType))
	{
		HiddenEventTypes.Remove(EventType);
	}
	else
	{
		HiddenEventTypes.Add(EventType);
	}
	LastObservedVersion = 0; // Force refresh
	ListView->RequestListRefresh();
}

bool SInputFlowLogView::IsEventTypeVisible(FString EventType) const
{
	return !HiddenEventTypes.Contains(EventType);
}

void SInputFlowLogView::UpdateLogView()
{
	constexpr int32 OverlayMaxEntries = 64;

	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (!DebugSubsystem.IsValid()) return;
	if (bLogPaused) return;

	const uint32 CurrentVersion = DebugSubsystem->GetLogVersion();

	// If version changed or we have pending filter change (reset version to 0)
	if (CurrentVersion == LastObservedVersion && LastObservedVersion != 0) return;

	LastObservedVersion = CurrentVersion;

	const TArray<TSharedPtr<FInputEventLog>>& Buffer = DebugSubsystem->GetLogHistory();
	const int32 WriteIdx = DebugSubsystem->GetLogHistoryWriteIndex();
	const bool bWrapped = DebugSubsystem->IsLogHistoryWrapped();

	SourceData.Reset();

	int32 Start = bWrapped ? WriteIdx : 0;
	int32 TotalItems = bWrapped ? Buffer.Num() : WriteIdx;

	// Optimization for overlay: only take last N entries
	if (bIsOverlay && TotalItems > OverlayMaxEntries)
	{
		Start = (Start + TotalItems - OverlayMaxEntries) % Buffer.Num();
		TotalItems = OverlayMaxEntries;
	}

	for (int32 i = 0; i < TotalItems; ++i)
	{
		const int32 CurrIdx = (Start + i) % Buffer.Num();
		if (!Buffer.IsValidIndex(CurrIdx)) continue;

		const TSharedPtr<FInputEventLog>& Log = Buffer[CurrIdx];
		if (!KnownEventTypes.Contains(Log->EventType))
		{
			KnownEventTypes.Add(Log->EventType);
		}
		if (IsItemVisible(Log))
		{
			SourceData.Add(Log);
		}
	}

	ListView->RequestListRefresh();
	ListView->ScrollToBottom();
}

TSharedRef<ITableRow> SInputFlowLogView::GenerateRow(TSharedPtr<FInputEventLog> Item,
													 const TSharedRef<STableViewBase>& OwnerTable)
{
	bool bIsSameFrame = false;

	const int32 Idx = SourceData.Find(Item);
	if (Idx > 0)
	{
		const TSharedPtr<FInputEventLog> Prev = SourceData[Idx - 1];
		if (Prev.IsValid())
		{
			if (FMath::Abs(Item->TimeSeconds - Prev->TimeSeconds) < 0.01)
			{
				bIsSameFrame = true;
			}
		}
	}

	return SNew(SInputLogTableRow, OwnerTable, Item, this, bIsOverlay, bIsSameFrame);
}

TSharedRef<SWidget> SInputFlowLogView::MakeFilterMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("EventTypes", FText::FromString("Event Types"));

	// Sort types alphabetically for the menu
	TArray<FString> SortedTypes = KnownEventTypes.Array();
	SortedTypes.Sort();

	if (SortedTypes.Num() == 0)
	{
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.Text(FText::FromString("No events recorded yet"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Margin(FMargin(10, 4)),
			FText()
		);
	}

	for (const FString& Type : SortedTypes)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Type),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SInputFlowLogView::ToggleEventTypeFilter, Type),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SInputFlowLogView::IsEventTypeVisible, Type)
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Tools");
	MenuBuilder.AddMenuEntry(
		FText::FromString("Show All"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			HiddenEventTypes.Empty();
			LastObservedVersion = 0;
			ListView->RequestListRefresh();
		}))
	);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SInputFlowLogView::MakeCaptureMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	auto AddFilterEntry = [&](const FText& Label, FName PropertyName, const FText& Tooltip)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SInputFlowLogView::OnToggleCapture, PropertyName),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SInputFlowLogView::IsCaptureChecked, PropertyName)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	};

	MenuBuilder.BeginSection("Events", INVTEXT("Event Types"));
	AddFilterEntry(INVTEXT("Log Clicks"), "bCaptureClicks", INVTEXT("Log Mouse Down/Up/DoubleClick"));
	AddFilterEntry(INVTEXT("Log Keys"), "bCaptureKeyEvents", INVTEXT("Log Key Down/Up"));
	AddFilterEntry(INVTEXT("Log Analog"), "bCaptureAnalog", INVTEXT("Log Analog Axis inputs"));
	AddFilterEntry(INVTEXT("Log Focus"), "bCaptureFocus", INVTEXT("Log Focus change events"));
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Noise", INVTEXT("High Frequency"));
	AddFilterEntry(INVTEXT("Log Hover"), "bCaptureHover", INVTEXT("Log Hover Enter/Leave (Noisy)"));
	AddFilterEntry(INVTEXT("Log Mouse Move"), "bCaptureMouseMove", INVTEXT("Log raw Mouse Move (Very Noisy)"));
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Handled", INVTEXT("Status"));
	AddFilterEntry(INVTEXT("Show Handled"), "bShowHandledEvents", INVTEXT("Show events handled by Slate widgets"));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SInputFlowLogView::OnToggleCapture(FName PropertyName)
{
	UInputFlowSettings* Settings = GetMutableDefault<UInputFlowSettings>();
	if (FProperty* Prop = Settings->GetClass()->FindPropertyByName(PropertyName))
	{
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			const bool bCurrent = BoolProp->GetPropertyValue_InContainer(Settings);
			BoolProp->SetPropertyValue_InContainer(Settings, !bCurrent);
			Settings->SaveConfig();
			Settings->GetOnSettingsChanged().Broadcast();
		}
	}
}

bool SInputFlowLogView::IsCaptureChecked(FName PropertyName) const
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (FProperty* Prop = Settings->GetClass()->FindPropertyByName(PropertyName))
	{
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			return BoolProp->GetPropertyValue_InContainer(Settings);
		}
	}
	return false;
}

// -----------------------------------------------------------------------------
// Context Menu & Filtering
// -----------------------------------------------------------------------------

void SInputFlowLogView::AddExclusionFilter(FName ColumnId, const FString& Value)
{
	ExcludedColumnValues.FindOrAdd(ColumnId).Add(Value);
	LastObservedVersion = 0; // Force Refresh
}

void SInputFlowLogView::SetSearchFilter(const FString& Value)
{
	if (SearchBoxWidget.IsValid())
	{
		SearchBoxWidget->SetText(FText::FromString(Value));
	}
	LogFilterText = Value;
	LastObservedVersion = 0;
}

void SInputFlowLogView::ClearAllFilters()
{
	ExcludedColumnValues.Empty();
	SetSearchFilter(TEXT(""));
}

TSharedPtr<SWidget> SInputFlowLogView::MakeRowContextMenu(TSharedPtr<FInputEventLog> Item, FName ColumnId)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("Filter", FText::FromString("Filtering"));
	{
		// -- Type Filters --
		if (ColumnId == InputFlowLogColumns::Type || ColumnId == NAME_None)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(FString::Printf(TEXT("Hide Type '%s'"), *Item->EventType)),
				FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
				FUIAction(FExecuteAction::CreateLambda([this, Type = Item->EventType]()
				{
					AddExclusionFilter(InputFlowLogColumns::Type, Type);
				}))
			);

			MenuBuilder.AddMenuEntry(
				FText::FromString(FString::Printf(TEXT("Show Only Type '%s'"), *Item->EventType)),
				FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Filter"),
				FUIAction(FExecuteAction::CreateLambda([this, Type = Item->EventType]()
				{
					SetSearchFilter(Type);
				}))
			);
		}

		// -- Widget Filters --
		if (!Item->WidgetName.IsEmpty() && (ColumnId == InputFlowLogColumns::Widget || ColumnId == NAME_None))
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(FString::Printf(TEXT("Hide Widget '%s'"), *Item->WidgetName)),
				FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
				FUIAction(FExecuteAction::CreateLambda([this, Name = Item->WidgetName]()
				{
					AddExclusionFilter(InputFlowLogColumns::Widget, Name);
				}))
			);

			MenuBuilder.AddMenuEntry(
				FText::FromString(FString::Printf(TEXT("Filter to Widget '%s'"), *Item->WidgetName)),
				FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Filter"),
				FUIAction(FExecuteAction::CreateLambda([this, Name = Item->WidgetName]()
				{
					SetSearchFilter(Name);
				}))
			);
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Copy", FText::FromString("Copy"));
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString("Copy Selected Rows"),
			FText::GetEmpty(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				// Get all selected items from the ListView
				TArray<TSharedPtr<FInputEventLog>> SelectedItems = ListView->GetSelectedItems();

				if (SelectedItems.Num() == 0) return;

				// Sort them by time
				SelectedItems.Sort([](const TSharedPtr<FInputEventLog>& A, const TSharedPtr<FInputEventLog>& B)
				{
					return A->TimeSeconds < B->TimeSeconds;
				});

				// Build string
#if ENGINE_MAJOR_VERSION < 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8)
				FStringBuilderBase OutputBuilder;
#else
				TStringBuilder<256> OutputBuilder;
#endif
				for (const TSharedPtr<FInputEventLog>& Log : SelectedItems)
				{
					if (!Log.IsValid()) continue;

					OutputBuilder.Appendf(TEXT("[%s] %s | %s | %s | %s\n"),
										  *Log->CaptureTime.ToString(TEXT("%H:%M:%S.%s")),
										  *Log->EventType,
										  *Log->WidgetName,
										  *Log->WidgetState,
										  *Log->InputDetails
					);
				}

				// Copy
				if (OutputBuilder.Len() > 0)
				{
					FPlatformApplicationMisc::ClipboardCopy(OutputBuilder.ToString());
				}
			}))
		);
	}
	MenuBuilder.EndSection();

	// Clear Option if any filters exist
	if (ExcludedColumnValues.Num() > 0 || !LogFilterText.IsEmpty())
	{
		MenuBuilder.AddSeparator();
		MenuBuilder.AddMenuEntry(
			FText::FromString("Clear All Filters"),
			FText::GetEmpty(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
			FUIAction(FExecuteAction::CreateSP(this, &SInputFlowLogView::ClearAllFilters))
		);
	}

	return MenuBuilder.MakeWidget();
}
