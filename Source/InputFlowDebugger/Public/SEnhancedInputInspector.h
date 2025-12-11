// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/SListView.h>

class UInputDebugSubsystem;
class UEnhancedPlayerInput;
class UInputMappingContext;

struct FEnhancedInputInfoItem
{
	FString Name;
	FString ValueStr;
	FString TriggerState;
	FColor StateColor;
	bool bIsContext = false; 
	int32 Priority = 0;

	bool operator==(const FEnhancedInputInfoItem& Other) const
	{
		return Name == Other.Name && ValueStr == Other.ValueStr && TriggerState == Other.TriggerState;
	}
};

class SEnhancedInputInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEnhancedInputInspector) : _IsOverlay(false) {}
	SLATE_ARGUMENT(bool, IsOverlay)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void UpdateData();
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FEnhancedInputInfoItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem = nullptr;
	bool bIsOverlay = false;

	TSharedPtr<SListView<TSharedPtr<FEnhancedInputInfoItem>>> ListView;
	TArray<TSharedPtr<FEnhancedInputInfoItem>> SourceData;
};