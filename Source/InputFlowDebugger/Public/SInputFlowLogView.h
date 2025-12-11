// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/SListView.h>

class UInputDebugSubsystem;
struct FInputEventLog;

class SInputFlowLogView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowLogView) : _IsOverlay(false) {}
	SLATE_ARGUMENT(bool, IsOverlay)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void ClearLog();

private:
	void UpdateLogView();
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FInputEventLog> Item, const TSharedRef<STableViewBase>& OwnerTable);
	
	void OnSearchTextChanged(const FText& InText);
	void OnTogglePause(ECheckBoxState State);
	ECheckBoxState GetPauseState() const;

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem;
	bool bIsOverlay = false;
	bool bLogPaused = false;
	
	FString LogFilterText;
	uint32 LastObservedVersion = 0;

	TSharedPtr<SListView<TSharedPtr<FInputEventLog>>> ListView;
	TArray<TSharedPtr<FInputEventLog>> SourceData;
};