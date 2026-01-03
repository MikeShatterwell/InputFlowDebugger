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
	FString ModifierChainStr;
	FString TriggersStr;
	FString OwnerStr;
	
	FColor StateColor;
	bool bIsInputMappingContext = false; 
	int32 Priority = 0;
	TArray<TSharedPtr<FEnhancedInputInfoItem>> Children;
	
	// Decay properties (inputs can be noisy so we keep them visible for a short time)
	float CurrentDecayTime = 0.0f; 
	float MaxDecayTime = 3.0f;

	bool operator==(const FEnhancedInputInfoItem& Other) const
	{
		return Name == Other.Name &&
			ValueStr == Other.ValueStr && 
			TriggerState == Other.TriggerState && 
			ModifiersStr == Other.ModifiersStr &&
			ModifierChainStr == Other.ModifierChainStr &&
			TriggersStr == Other.TriggersStr &&
			OwnerStr == Other.OwnerStr;
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
	void UpdateData(float DeltaTime);
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FEnhancedInputInfoItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FEnhancedInputInfoItem> Item, TArray<TSharedPtr<FEnhancedInputInfoItem>>& OutChildren);

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem = nullptr;
	bool bIsOverlay = false;

	TSharedPtr<STreeView<TSharedPtr<FEnhancedInputInfoItem>>> TreeView;
	TArray<TSharedPtr<FEnhancedInputInfoItem>> SourceData;
};

#endif