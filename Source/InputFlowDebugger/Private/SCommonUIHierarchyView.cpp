// Copyright Mike Desrosiers, All Rights Reserved.

#include "SCommonUIHierarchyView.h"

#if WITH_PLUGIN_COMMONUI

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
		Item = InItem;

		const FTableRowStyle& RowStyle = bInOverlay ?
			InputFlowHelpers::GetTranslucentRowStyle() :
			FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");

		STableRow<TSharedPtr<FCommonUITreeItem>>::Construct(
			STableRow<TSharedPtr<FCommonUITreeItem>>::FArguments()
			.ShowSelection(!bInOverlay).Style(&RowStyle),
			InOwnerTableView
		);

		if (bInOverlay)
		{
			SetBorderBackgroundColor(FLinearColor::Transparent);
		}

		// --- Attribute Lambdas for Dynamic Updates ---
		auto GetDisplayName = [InItem]() { return FText::FromString(InItem->Name); };
		
		auto GetTextColor = [InItem]() -> FSlateColor
		{
			// TODO: Don't like this hardcoding, but it's quick for now
			if (InItem->bIsLeaf) return FLinearColor(0.2f, 1.0f, 0.4f);
			if (InItem->bIsFocused) return FSlateColor(FLinearColor::White);
			if (InItem->bIsInActivePath) return FLinearColor(1.0f, 0.95f, 0.8f);
			if (!InItem->bIsActive) return FLinearColor(1.0f, 1.0f, 1.0f, 0.35f);
			return FLinearColor::White;
		};

		auto GetBadgeText = [InItem]() -> FText
		{
			if (InItem->bIsLeaf) return FText::FromString(TEXT("[LEAF] "));
			if (InItem->bIsFocused) return FText::FromString(TEXT("[FOCUS] "));
			if (InItem->bIsInActivePath) return FText::FromString(TEXT("[ROUTE] "));
			return FText::GetEmpty();
		};

		auto GetBadgeColor = [InItem]() -> FSlateColor
		{
			if (InItem->bIsLeaf) return FLinearColor(0.2f, 1.0f, 0.4f);
			if (InItem->bIsInActivePath) return FLinearColor(1.0f, 0.8f, 0.2f);
			return FLinearColor::Transparent;
		};

		auto GetBadgeVisibility = [InItem]()
		{
			return (InItem->bIsLeaf || InItem->bIsInActivePath) ? EVisibility::Visible : EVisibility::Collapsed;
		};

		auto GetSubText = [InItem]() -> FText
		{
			FString SubText = InItem->InputConfig;
			if (!InItem->DesiredFocusTarget.IsEmpty()) SubText += FString::Printf(TEXT(" | DesiredFocusTarget: %s"), *InItem->DesiredFocusTarget);
			if (!InItem->ContainerInfo.IsEmpty()) SubText += FString::Printf(TEXT(" | %s"), *InItem->ContainerInfo);
			return FText::FromString(SubText);
		};

		TSharedRef<SWidget> NameWidget = SNullWidget::NullWidget;
		
		if (InItem->Widget.IsValid() && !bInOverlay)
		{
			NameWidget = SNew(SHyperlink)
				.Text_Lambda(GetDisplayName)
				.Style(FCoreStyle::Get(), "Hyperlink")
				.OnNavigate_Lambda([InItem]() { InputFlowHelpers::TryOpenAsset(InItem->Widget); });
		}
		else
		{
			NameWidget = SNew(STextBlock)
				.Text_Lambda(GetDisplayName)
				.ColorAndOpacity_Lambda(GetTextColor)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
		}

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
						SNew(STextBlock)
						.Text_Lambda(GetBadgeText)
						.ColorAndOpacity_Lambda(GetBadgeColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.Visibility_Lambda(GetBadgeVisibility)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						NameWidget
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda(GetSubText)
					.ColorAndOpacity(FLinearColor(0.4f, 0.6f, 1.0f))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.Visibility_Lambda([GetSubText](){ return GetSubText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
			]
		];
	}

private:
	TSharedPtr<FCommonUITreeItem> Item;
};

// -----------------------------------------------------------------------------
// Main View Implementation
// -----------------------------------------------------------------------------

void SCommonUIHierarchyView::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
	DebugSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;

	RebuildingHandle = UCommonActivatableWidget::OnRebuilding.AddLambda([this](UCommonActivatableWidget&)
	{
		RequestRefresh();
	});

	BindRouterDelegates();
	
	const FTableViewStyle& TreeStyle = bIsOverlay 
		? InputFlowHelpers::GetTranslucentTableViewStyle() 
		: FCoreStyle::Get().GetWidgetStyle<FTableViewStyle>("TreeView");
	
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(bIsOverlay))
		.BorderBackgroundColor(bIsOverlay ? FLinearColor(0.0f, 0.0f, 0.0f, 0.6f) : FLinearColor::White)
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
				.TreeViewStyle(&TreeStyle)
				.SelectionMode(bIsOverlay ? ESelectionMode::None : ESelectionMode::Single)
			]
		]
	];
}

void SCommonUIHierarchyView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!BoundRouter.IsValid())
	{
		BindRouterDelegates();
	}

	const bool bEnoughTimePassed = (InCurrentTime - LastUpdateTime) > UpdateThrottle;

	if (bTreeDirty || bEnoughTimePassed)
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

		// Update Mutable Properties (Polled by Row Widgets)
		Item->Name = Widget->GetName();
		Item->bIsActive = Widget->IsActivated();
		Item->InputConfig = GetInputConfigString(Widget);

		Item->DesiredFocusTarget = TEXT("None");
		if (UWidget* DesiredFocusTarget = Widget->GetDesiredFocusTarget())
		{
			Item->DesiredFocusTarget = DesiredFocusTarget->GetName();
		}

		Item->ContainerInfo.Empty();
		UObject* CurrOuter = Widget->GetOuter();
		while (CurrOuter)
		{
			if (UCommonActivatableWidgetContainerBase* Container = Cast<UCommonActivatableWidgetContainerBase>(CurrOuter))
			{
				FString ContainerType = TEXT("Container");
				if (Container->IsA<UCommonActivatableWidgetStack>()) ContainerType = TEXT("Stack");
				else if (Container->IsA<UCommonActivatableWidgetQueue>()) ContainerType = TEXT("Queue");
				
				Item->ContainerInfo = FString::Printf(TEXT("[%s: %s]"), *ContainerType, *Container->GetName());
				break;
			}
			CurrOuter = CurrOuter->GetOuter();
		}

		// Reset Flags (will be re-evaluated below)
		Item->Children.Empty();
		Item->bIsFocused = false;
		Item->bIsInActivePath = false;

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

	// 4. Update Leaf & Path Flags based on Overlay State (Source of Truth)
	const FInputOverlayState& State = DebugSubsystem->GetOverlayState();
	FString ActiveLeafName = State.ActiveCommonUILeaf;

	if (!ActiveLeafName.IsEmpty())
	{
		UCommonActivatableWidget* FocusOwner = nullptr;
		
		// Find matching widget
		for (auto& Pair : WorkingMap)
		{
			// Strict name matching ensures we respect the Router's authority
			if (Pair.Key->GetName() == ActiveLeafName)
			{
				FocusOwner = Pair.Key;
				break;
			}
		}

		if (FocusOwner)
		{
			WorkingMap[FocusOwner]->bIsFocused = true; // Sets "LEAF" Badge
			
			UCommonActivatableWidget* Current = FocusOwner;
			while (Current)
			{
				if (WorkingMap.Contains(Current)) WorkingMap[Current]->bIsInActivePath = true; // Sets "ROUTE" Badge
				
				TSharedPtr<SWidget> Cached = Current->GetCachedWidget();
				if (!Cached.IsValid()) break;
				Current = UCommonUIActionRouterBase::FindOwningActivatable(Cached, LocalPlayer);
			}
		}
	}

	// 5. Sort
	auto SortFunc = [](const TSharedPtr<FCommonUITreeItem>& A, const TSharedPtr<FCommonUITreeItem>& B)
	{
		if (A->bIsInActivePath != B->bIsInActivePath) return A->bIsInActivePath > B->bIsInActivePath;
		return A->Name < B->Name;
	};

	NewRoots.Sort(SortFunc);
	for (auto& Pair : WorkingMap) Pair.Value->Children.Sort(SortFunc);

	Roots = NewRoots;
	TreeView->RequestTreeRefresh();

	// 6. Auto Expand
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
	if (BoundRouter.IsValid())
	{
		BoundRouter->OnBoundActionsUpdated().Remove(ActionRouterHandle);
		BoundRouter->OnActiveInputConfigChanged().Remove(ConfigChangeHandle);
		BoundRouter.Reset();
	}

	if (!DebugSubsystem.IsValid()) return;

	if (const UGameInstance* GI = DebugSubsystem->GetGameInstance())
	{
		if (const ULocalPlayer* LP = GI->GetFirstGamePlayer())
		{
			if (UCommonUIActionRouterBase* Router = LP->GetSubsystem<UCommonUIActionRouterBase>())
			{
				BoundRouter = Router;
				ActionRouterHandle = Router->OnBoundActionsUpdated().AddSP(this, &SCommonUIHierarchyView::RequestRefresh);
				ConfigChangeHandle = Router->OnActiveInputConfigChanged().AddLambda([this](const FUIInputConfig&){ RequestRefresh(); });
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

#endif // WITH_PLUGIN_COMMONUI