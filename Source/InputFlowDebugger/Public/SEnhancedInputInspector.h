// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/STreeView.h>

#if WITH_PLUGIN_ENHANCEDINPUT

class UInputDebugSubsystem;
class UEnhancedPlayerInput;
class UInputMappingContext;

struct FEnhancedInputInfoItem
{
	FString Name;
	FString ValueStr;
	FString TriggerState;
	FString ModifiersStr;
	FColor StateColor;
	bool bIsInputMappingContext = false; 
	int32 Priority = 0;
	TArray<TSharedPtr<FEnhancedInputInfoItem>> Children;

	bool operator==(const FEnhancedInputInfoItem& Other) const
	{
		return Name == Other.Name && ValueStr == Other.ValueStr && TriggerState == Other.TriggerState && ModifiersStr == Other.ModifiersStr;
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
	void OnGetChildren(TSharedPtr<FEnhancedInputInfoItem> Item, TArray<TSharedPtr<FEnhancedInputInfoItem>>& OutChildren);

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem = nullptr;
	bool bIsOverlay = false;

	TSharedPtr<STreeView<TSharedPtr<FEnhancedInputInfoItem>>> TreeView;
	TArray<TSharedPtr<FEnhancedInputInfoItem>> SourceData;
};

#endif