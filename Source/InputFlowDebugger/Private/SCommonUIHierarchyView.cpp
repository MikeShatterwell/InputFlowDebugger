// Copyright Mike Desrosiers, All Rights Reserved.

#include "SCommonUIHierarchyView.h"

// CommonUI
#include <CommonActivatableWidget.h>
#include <Input/CommonUIActionRouterBase.h>
#include <Widgets/CommonActivatableWidgetContainer.h>

// Core
#include <UObject/UObjectIterator.h>

// Engine
#include <Engine/GameInstance.h>

// Slate
#include <Styling/AppStyle.h>
#include <Widgets/Input/SHyperlink.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Views/SExpanderArrow.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"

// -----------------------------------------------------------------------------
// Row Widget
// -----------------------------------------------------------------------------

class SCommonUITreeRow : public STableRow<TSharedPtr<FCommonUITreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SCommonUITreeRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FCommonUITreeItem> InItem, bool bInOverlay)
	{
		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
		
		STableRow<TSharedPtr<FCommonUITreeItem>>::Construct(
			STableRow<TSharedPtr<FCommonUITreeItem>>::FArguments()
			.ShowSelection(!bInOverlay),
			InOwnerTableView
		);

		FString DisplayName = InItem->Name;
		FLinearColor TextColor = FLinearColor::White;
		FString BadgeText;
		FLinearColor BadgeColor = FLinearColor::Transparent;

		if (InItem->bIsFocused)
		{
			BadgeText = TEXT("[LEAF] ");
			BadgeColor = FLinearColor(0.2f, 1.0f, 0.4f);
			TextColor = FLinearColor(0.2f, 1.0f, 0.4f);
		}
		else if (InItem->bIsInActivePath)
		{
			BadgeText = TEXT("[ROUTE] ");
			BadgeColor = FLinearColor(1.0f, 0.8f, 0.2f);
			TextColor = FLinearColor(1.0f, 0.95f, 0.8f);
		}
		else if (!InItem->bIsActive)
		{
			TextColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.35f);
		}

		if (bInOverlay)
		{
			SetBorderBackgroundColor(FLinearColor::Transparent);
		}

		TSharedRef<SWidget> NameWidget = SNullWidget::NullWidget;
		
		if (InItem->Widget.IsValid() && !bInOverlay)
		{
			NameWidget = SNew(SHyperlink)
				.Text(FText::FromString(DisplayName))
				.Style(FCoreStyle::Get(), "Hyperlink")
				.OnNavigate_Lambda([InItem]() { InputFlowHelpers::TryOpenAsset(InItem->Widget); });
		}
		else
		{
			NameWidget = SNew(STextBlock)
				.Text(FText::FromString(DisplayName))
				.ColorAndOpacity(TextColor)
				.Font(FCoreStyle::GetDefaultFontStyle(InItem->bIsFocused ? "Bold" : "Regular", 9));
		}

		// --- Build Secondary Info String ---
		FString SubText = InItem->InputConfig;
		
		if (!InItem->DesiredFocusTarget.IsEmpty())
		{
			SubText += FString::Printf(TEXT(" | DesiredFocusTarget: %s"), *InItem->DesiredFocusTarget);
		}
		
		if (!InItem->ContainerInfo.IsEmpty())
		{
			SubText += FString::Printf(TEXT(" | %s"), *InItem->ContainerInfo);
		}
		// -----------------------------------

		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this)).IndentAmount(12) 
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Text(FText::FromString(BadgeText)).ColorAndOpacity(BadgeColor).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.Visibility(BadgeText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						NameWidget
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(SubText)).ColorAndOpacity(FLinearColor(0.4f, 0.6f, 1.0f)).Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.Visibility(SubText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]
		];
	}
};

// -----------------------------------------------------------------------------
// Main View Implementation
// -----------------------------------------------------------------------------

void SCommonUIHierarchyView::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
	DebugSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;

	// 1. Hook into static creation event (Catches new widgets being made)
	RebuildingHandle = UCommonActivatableWidget::OnRebuilding.AddLambda([this](UCommonActivatableWidget&)
	{
		RequestRefresh();
	});
    
	// 2. Try binding to router immediately
	BindRouterDelegates();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(bIsOverlay))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(STextBlock)
				.Text(FText::FromString("Activatable Tree"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(bIsOverlay ? FLinearColor(1.0f, 0.8f, 0.2f) : FLinearColor::White)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FCommonUITreeItem>>)
				.TreeItemsSource(&Roots)
				.OnGenerateRow(this, &SCommonUIHierarchyView::GenerateRow)
				.OnGetChildren(this, &SCommonUIHierarchyView::OnGetChildren)
				.SelectionMode(bIsOverlay ? ESelectionMode::None : ESelectionMode::Single)
			]
		]
	];
}

void SCommonUIHierarchyView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Check for Router validity periodically (in case Player Controller changes)
	if (!BoundRouter.IsValid())
	{
		BindRouterDelegates();
	}

	// Throttle Logic
	const bool bEnoughTimePassed = (InCurrentTime - LastUpdateTime) > UpdateThrottle;
	const bool bSlowPollTime = (InCurrentTime - LastUpdateTime) > SlowPollInterval;

	if ((bTreeDirty && bEnoughTimePassed) || bSlowPollTime)
	{
		UpdateTree();
		LastUpdateTime = InCurrentTime;
		bTreeDirty = false;
	}
}

SCommonUIHierarchyView::~SCommonUIHierarchyView()
{
	if (RebuildingHandle.IsValid())
	{
		UCommonActivatableWidget::OnRebuilding.Remove(RebuildingHandle);
		RebuildingHandle.Reset();
	}
}

FString SCommonUIHierarchyView::GetInputConfigString(const UCommonActivatableWidget* Widget)
{
	if (!Widget) return "";
	if (!Widget->GetDesiredInputConfig().IsSet()) return "";

	FUIInputConfig Config = Widget->GetDesiredInputConfig().GetValue();
	FString ModeStr;
	switch (Config.GetInputMode())
	{
	case ECommonInputMode::Game: ModeStr = "Game"; break;
	case ECommonInputMode::Menu: ModeStr = "Menu"; break;
	case ECommonInputMode::All:  ModeStr = "All";  break;
	default: ModeStr = "Def"; break;
	}
	FString MouseStr = (Config.GetMouseCaptureMode() == EMouseCaptureMode::CapturePermanently) ? "Perm" : "Free";
	return FString::Printf(TEXT("[%s | %s]"), *ModeStr, *MouseStr);
}

void SCommonUIHierarchyView::UpdateTree()
{
	// Recover subsystem if lost (re-PIE)
	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (!DebugSubsystem.IsValid()) return;

	UGameInstance* GameInstance = DebugSubsystem->GetGameInstance();
	if (!IsValid(GameInstance)) return;

	ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
	if (!IsValid(LocalPlayer)) return;

	// 1. Gather Widgets
	TArray<UCommonActivatableWidget*> PlayerWidgets;
	for (TObjectIterator<UCommonActivatableWidget> It; It; ++It)
	{
		if (It->GetWorld() == DebugSubsystem->GetWorld() && It->GetOwningLocalPlayer() == LocalPlayer)
		{
			PlayerWidgets.Add(*It);
		}
	}

	// 2. Build Cache & Working Set
	TSet<TSharedPtr<FCommonUITreeItem>> ActiveItems;
	TMap<UCommonActivatableWidget*, TSharedPtr<FCommonUITreeItem>> WorkingMap;

	for (UCommonActivatableWidget* Widget : PlayerWidgets)
	{
		TSharedPtr<FCommonUITreeItem> Item;
		TWeakObjectPtr<UCommonActivatableWidget> WeakWidget = Widget;

		if (WidgetItemCache.Contains(WeakWidget))
		{
			Item = WidgetItemCache[WeakWidget];
		}
		else
		{
			Item = MakeShared<FCommonUITreeItem>();
			Item->Widget = Widget;
			WidgetItemCache.Add(WeakWidget, Item);
		}

		Item->Name = Widget->GetName();
		Item->bIsActive = Widget->IsActivated();
		Item->Children.Empty();
		Item->bIsFocused = false;
		Item->bIsInActivePath = false;
		Item->InputConfig = GetInputConfigString(Widget);

		// Resolve Desired Focus Target
		Item->DesiredFocusTarget = TEXT("None");
		if (UWidget* DesiredFocusTarget = Widget->GetDesiredFocusTarget())
		{
			Item->DesiredFocusTarget = DesiredFocusTarget->GetName();
		}

		// Resolve Container Info
		Item->ContainerInfo.Empty();
		UObject* CurrOuter = Widget->GetOuter();
		while (CurrOuter)
		{
			if (UCommonActivatableWidgetContainerBase* Container = Cast<UCommonActivatableWidgetContainerBase>(CurrOuter))
			{
				FString ContainerType = TEXT("Container");
				if (Container->IsA<UCommonActivatableWidgetStack>())
				{
					ContainerType = TEXT("Stack");
				}
				else if (Container->IsA<UCommonActivatableWidgetQueue>())
				{
					ContainerType = TEXT("Queue");
				}
				
				Item->ContainerInfo = FString::Printf(TEXT("[%s: %s]"), *ContainerType, *Container->GetName());
				break;
			}
			CurrOuter = CurrOuter->GetOuter();
		}

		WorkingMap.Add(Widget, Item);
		ActiveItems.Add(Item);
	}

	// Clean Stale Cache
	TArray<TWeakObjectPtr<UCommonActivatableWidget>> KeysToRemove;
	for (auto& Pair : WidgetItemCache)
	{
		if (!ActiveItems.Contains(Pair.Value)) KeysToRemove.Add(Pair.Key);
	}
	for (TWeakObjectPtr<UCommonActivatableWidget>& Key : KeysToRemove) WidgetItemCache.Remove(Key);

	// 3. Link Tree
	TArray<TSharedPtr<FCommonUITreeItem>> NewRoots;
	for (auto& Pair : WorkingMap)
	{
		UCommonActivatableWidget* Widget = Pair.Key;
		TSharedPtr<FCommonUITreeItem> Item = Pair.Value;

		TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
		bool bLinked = false;

		if (CachedWidget.IsValid())
		{
			UCommonActivatableWidget* ParentWidget = UCommonUIActionRouterBase::FindOwningActivatable(CachedWidget, LocalPlayer);
			if (ParentWidget && WorkingMap.Contains(ParentWidget))
			{
				WorkingMap[ParentWidget]->Children.Add(Item);
				bLinked = true;
			}
		}

		if (!bLinked)
		{
			Item->bIsRoot = true;
			NewRoots.Add(Item);
		}
	}

	// 4. Identify Active Leaf using Subsystem Data (Source of Truth)
	const FInputOverlayState& State = DebugSubsystem->GetOverlayState();
	FString ActiveLeafName = State.ActiveCommonUILeaf;

	// Reset focus state
	for (auto& Pair : WorkingMap)
	{
		Pair.Value->bIsFocused = false;
		Pair.Value->bIsInActivePath = false;
	}

	if (!ActiveLeafName.IsEmpty())
	{
		UCommonActivatableWidget* FocusOwner = nullptr;
		
		// Find widget matching name
		for (auto& Pair : WorkingMap)
		{
			if (Pair.Key->GetName() == ActiveLeafName)
			{
				FocusOwner = Pair.Key;
				break;
			}
		}

		if (FocusOwner)
		{
			WorkingMap[FocusOwner]->bIsFocused = true;
			
			UCommonActivatableWidget* Current = FocusOwner;
			while (Current)
			{
				if (WorkingMap.Contains(Current)) WorkingMap[Current]->bIsInActivePath = true;
				TSharedPtr<SWidget> Cached = Current->GetCachedWidget();
				if (!Cached.IsValid()) break;
				Current = UCommonUIActionRouterBase::FindOwningActivatable(Cached, LocalPlayer);
			}
		}
	}

	// 5. Sort & Refresh
	auto SortFunc = [](const TSharedPtr<FCommonUITreeItem>& A, const TSharedPtr<FCommonUITreeItem>& B)
	{
		if (A->bIsInActivePath != B->bIsInActivePath) return A->bIsInActivePath > B->bIsInActivePath;
		return A->Name < B->Name;
	};

	NewRoots.Sort(SortFunc);
	for (auto& Pair : WorkingMap) Pair.Value->Children.Sort(SortFunc);

	Roots = NewRoots;
	TreeView->RequestTreeRefresh();

	// 6. Enforce Expansion
	for (auto& Pair : WorkingMap)
	{
		if (Pair.Value->bIsInActivePath || Pair.Value->bIsRoot || Pair.Value->bIsActive)
		{
			TreeView->SetItemExpansion(Pair.Value, true);
		}
	}
}

void SCommonUIHierarchyView::BindRouterDelegates()
{
	// Clean up old bindings if router changed
	if (BoundRouter.IsValid())
	{
		BoundRouter->OnBoundActionsUpdated().Remove(ActionRouterHandle);
		BoundRouter->OnActiveInputConfigChanged().Remove(ConfigChangeHandle);
		BoundRouter.Reset();
	}

	if (!DebugSubsystem.IsValid()) return;

	// Find the Router via the Subsystem -> GameInstance -> LocalPlayer
	if (const UGameInstance* GI = DebugSubsystem->GetGameInstance())
	{
		if (const ULocalPlayer* LP = GI->GetFirstGamePlayer())
		{
			if (UCommonUIActionRouterBase* Router = LP->GetSubsystem<UCommonUIActionRouterBase>())
			{
				BoundRouter = Router;
                
				// Bind to the events that signal tree state changes
				ActionRouterHandle = Router->OnBoundActionsUpdated().AddSP(this, &SCommonUIHierarchyView::RequestRefresh);
				ConfigChangeHandle = Router->OnActiveInputConfigChanged().AddLambda([this](const FUIInputConfig&){ RequestRefresh(); });
                
				// Force an update now that we have data
				RequestRefresh();
			}
		}
	}
}

void SCommonUIHierarchyView::RequestRefresh()
{
	bTreeDirty = true;
}

TSharedRef<ITableRow> SCommonUIHierarchyView::GenerateRow(TSharedPtr<FCommonUITreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SCommonUITreeRow, OwnerTable, Item, bIsOverlay);
}

void SCommonUIHierarchyView::OnGetChildren(TSharedPtr<FCommonUITreeItem> Item, TArray<TSharedPtr<FCommonUITreeItem>>& OutChildren)
{
	OutChildren = Item->Children;
}