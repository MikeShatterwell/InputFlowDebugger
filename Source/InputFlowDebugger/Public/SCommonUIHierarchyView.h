// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/STreeView.h>

#if WITH_PLUGIN_COMMONUI

class UCommonUIActionRouterBase;
class UInputDebugSubsystem;
class UCommonActivatableWidget;

struct FCommonUITreeItem : public TSharedFromThis<FCommonUITreeItem>
{
	TWeakObjectPtr<UCommonActivatableWidget> Widget;
	FString Name;
	FString InputConfig;
	FString DesiredFocusTarget;
	FString ContainerInfo;
	
	// State flags
	bool bIsRoot = false;
	bool bIsFocused = false;      
	bool bIsInActivePath = false; 
	bool bIsActive = false;
	bool bIsLeaf = false;

	TArray<TSharedPtr<FCommonUITreeItem>> Children;
};

class SCommonUIHierarchyView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCommonUIHierarchyView) : _IsOverlay(false) {}
	SLATE_ARGUMENT(bool, IsOverlay)
SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual ~SCommonUIHierarchyView();

private:
	void UpdateTree();
	void BindRouterDelegates();
	void RequestRefresh();
	
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FCommonUITreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FCommonUITreeItem> Item, TArray<TSharedPtr<FCommonUITreeItem>>& OutChildren);
	static FString GetInputConfigString(const UCommonActivatableWidget* Widget);

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem = nullptr;
	TWeakObjectPtr<UCommonUIActionRouterBase> BoundRouter = nullptr;
	
	// Delegate Handles
	FDelegateHandle RebuildingHandle;
	FDelegateHandle ActionRouterHandle;
	FDelegateHandle ConfigChangeHandle;

	bool bIsOverlay = false;
	
	// Optimization Flags
	bool bTreeDirty = true; 
	float LastUpdateTime = 0.0f;
	float UpdateThrottle = 0.1f; 

	TSharedPtr<STreeView<TSharedPtr<FCommonUITreeItem>>> TreeView;
	TArray<TSharedPtr<FCommonUITreeItem>> Roots;
	TMap<TWeakObjectPtr<UCommonActivatableWidget>, TSharedPtr<FCommonUITreeItem>> WidgetItemCache;
};

#endif // WITH_PLUGIN_COMMONUI