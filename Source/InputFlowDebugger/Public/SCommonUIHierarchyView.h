// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/STreeView.h>

#if WITH_PLUGIN_COMMONUI

class UWidget;
class ULocalPlayer;
class UCommonUIActionRouterBase;
class UInputDebugSubsystem;
class UCommonActivatableWidget;

struct FCommonUITreeItem : public TSharedFromThis<FCommonUITreeItem>
{
	TWeakObjectPtr<UWidget> Widget;
	FString Name = TEXT("");
	FString InputConfig = TEXT("");
	FString DesiredFocusTarget = TEXT("");
	FString ContainerInfo = TEXT("");
	FString ActionDomain = TEXT("");
	
	// State flags
	bool bIsContainer = false;     // UCommonActivatableWidgetContainerBase Stack/Queue
	bool bIsRoot = false;          
	bool bIsFocused = false;       
	bool bIsInActivePath = false;  // Widget is ancestor of focused/leaf
	bool bIsActive = false;        
	bool bIsLeaf = false;          // This is the leafmost active node
	bool bIsActiveRoot = false;    // Active root receiving input
	bool bIsInActiveRoot = false;  // Widget is a child within the currently active root
	bool bIsModal = false;         // Widget is modal (acts as input root regardless of parentage)
	bool bSupportsActivationFocus = true; // Widget participates in focus/input routing

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

	/**
	 * Points the view at a different client's subsystem (each PIE client owns its own).
	 * Drops the cached items and rebinds the action router delegates for the new target.
	 */
	void SetDebugSubsystem(UInputDebugSubsystem* InSubsystem);

private:
	/** The local player whose activatable tree we render, or null if none is resolvable. */
	ULocalPlayer* ResolveTargetLocalPlayer() const;

	void UpdateTree();
	void BindRouterDelegates();
	void RequestRefresh();
	
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FCommonUITreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FCommonUITreeItem> Item, TArray<TSharedPtr<FCommonUITreeItem>>& OutChildren);
	static FString GetInputConfigString(const UCommonActivatableWidget* Widget);

	TWeakObjectPtr<UInputDebugSubsystem> DebugSubsystem = nullptr;
	TWeakObjectPtr<UCommonUIActionRouterBase> BoundRouter = nullptr;

	// The player BoundRouter belongs to, so a target switch can be detected and rebound.
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer = nullptr;

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
	TMap<TWeakObjectPtr<UWidget>, TSharedPtr<FCommonUITreeItem>> WidgetItemCache;
};

#endif // WITH_PLUGIN_COMMONUI