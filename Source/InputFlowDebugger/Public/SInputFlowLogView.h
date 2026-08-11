// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/SListView.h>
#include <Widgets/Views/STableRow.h>

class UInputDebugSubsystem;
struct FInputEventLog;

// -----------------------------------------------------------------------------
// Column Names
// -----------------------------------------------------------------------------
namespace InputFlowLogColumns
{
	static const FName Time("Time");
	static const FName Type("Type");
	static const FName Widget("Widget");
	static const FName State("State");
	static const FName Details("Details");
};

// -----------------------------------------------------------------------------
// Main View
// -----------------------------------------------------------------------------

class SInputFlowLogView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowLogView) : _IsOverlay(false) {}
		SLATE_ARGUMENT(bool, IsOverlay)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void ClearLog();

	/** Points the log at a different client's subsystem (each one has its own history). */
	void SetDebugSubsystem(UInputDebugSubsystem* InSubsystem);

	// Context Menu & Filtering API
	TSharedPtr<SWidget> MakeRowContextMenu(TSharedPtr<FInputEventLog> Item, FName ColumnId);
	void AddExclusionFilter(FName ColumnId, const FString& Value);
	void SetSearchFilter(const FString& Value);
	void ClearAllFilters();
	bool IsItemVisible(const TSharedPtr<FInputEventLog>& Item) const;
	
	void ToggleEventTypeFilter(FString EventType);
	bool IsEventTypeVisible(FString EventType) const;

private:
	void UpdateLogView();
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FInputEventLog> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<SWidget> MakeFilterMenu();

	TSharedRef<SWidget> MakeCaptureMenu();
	void OnToggleCapture(FName PropertyName);
	bool IsCaptureChecked(FName PropertyName) const;
	
	void OnSearchTextChanged(const FText& InText);
	void OnTogglePause(ECheckBoxState State);
	ECheckBoxState GetPauseState() const;

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem;
	bool bIsOverlay = false;
	bool bLogPaused = false;
	
	// Filtering State
	FString LogFilterText;
	TMap<FName, TSet<FString>> ExcludedColumnValues;
	TSharedPtr<class SSearchBox> SearchBoxWidget;
	TSet<FString> KnownEventTypes;      // Discovered from the log data
	TSet<FString> HiddenEventTypes;     // Unchecked in the filter menu

	uint32 LastObservedVersion = 0;

	TSharedPtr<SListView<TSharedPtr<FInputEventLog>>> ListView;
	TArray<TSharedPtr<FInputEventLog>> SourceData;
};

// -----------------------------------------------------------------------------
// Table Row
// -----------------------------------------------------------------------------

class SInputLogTableRow : public SMultiColumnTableRow<TSharedPtr<FInputEventLog>>
{
public:
	SLATE_BEGIN_ARGS(SInputLogTableRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FInputEventLog> InItem, SInputFlowLogView* InOwnerView, bool bInOverlay, bool bIsSameFrameAsPrev);

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	// Custom handler called by individual cells
	FReply OnCellRightClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FName ColumnId);

private:
	TSharedPtr<FInputEventLog> Item;
	SInputFlowLogView* OwnerView = nullptr;
	bool bIsOverlay = false;
	bool bIsSameFrame = false;
};
