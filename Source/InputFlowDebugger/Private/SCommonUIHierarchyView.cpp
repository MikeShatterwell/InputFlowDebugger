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

		// Badge Helper - Uses TAttribute to ensure UI updates when data refreshes
		auto CreateBadge = [](const FString& Text, const FLinearColor Color, TAttribute<EVisibility> Visibility) -> TSharedRef<SWidget>
		{
			return SNew(SBorder)
				.Visibility(Visibility)
				.Padding(FMargin(4, 0))
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Color.CopyWithNewOpacity(0.15f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Text))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(Color)
				];
		};

		// Color Logic: Focus (Green) > Leaf (Pink) > Container (Blue) > Default
		auto GetNodeColor = [InItem]() -> FSlateColor
		{
			if (InItem->bIsFocused)   return FLinearColor(0.0f, 1.0f, 0.4f); // Actual Slate Focus
			if (InItem->bIsLeaf)      return FLinearColor(1.0f, 0.4f, 0.8f); // CommonUI Active Leaf
			if (InItem->bIsContainer) return FLinearColor(0.3f, 0.7f, 1.0f); // Structure
			return FLinearColor::White;
		};

		// Name Widget with Hyperlink support
		TSharedRef<SWidget> NameWidget = SNullWidget::NullWidget;
		if (InItem->Widget.IsValid() && !bInOverlay)
		{
			NameWidget = SNew(SHyperlink)
				.Text(FText::FromString(InItem->Name))
				.Style(FCoreStyle::Get(), "Hyperlink")
				.OnNavigate_Lambda([InItem]() { InputFlowHelpers::TryOpenAsset(InItem->Widget); });
		}
		else
		{
			NameWidget = SNew(STextBlock)
				.Text(FText::FromString(InItem->Name))
				.ColorAndOpacity_Lambda(GetNodeColor)
				.Font(FCoreStyle::GetDefaultFontStyle(InItem->bIsContainer ? "Bold" : "Regular", 9));
		}

		auto GetSubText = [InItem]() -> FText
		{
			TArray<FString> Parts;
			if (!InItem->InputConfig.IsEmpty()) Parts.Add(InItem->InputConfig);
			if (!InItem->DesiredFocusTarget.IsEmpty() && InItem->DesiredFocusTarget != TEXT("None"))
			{
				Parts.Add(FString::Printf(TEXT("Target: %s"), *InItem->DesiredFocusTarget));
			}
			return FText::FromString(FString::Join(Parts, TEXT(" | ")));
		};

		this->ChildSlot
		[
			SNew(SBox).Padding(FMargin(0, 2))
			[
				SNew(SHorizontalBox)
				.RenderOpacity(InItem->bIsActive ? 1.0f : 0.4f)
				
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
						// Badge: Container
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
						[
							CreateBadge(TEXT("STACK"), FLinearColor(0.2f, 0.6f, 1.0f), 
								TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsContainer ? EVisibility::Visible : EVisibility::Collapsed; }))
						]
						// Badge: Slate Focus
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
						[
							CreateBadge(TEXT("FOCUS"), FLinearColor(0.0f, 1.0f, 0.4f), 
								TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsFocused ? EVisibility::Visible : EVisibility::Collapsed; }))
						]
						// Badge: Leaf
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
						[
							CreateBadge(TEXT("INPUT LEAF"), FLinearColor(1.0f, 0.4f, 0.8f), 
								TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsLeaf ? EVisibility::Visible : EVisibility::Collapsed; }))
						]
						// Badge: Route (Only shown for ancestors, not the Focus/Leaf itself)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
						[
							CreateBadge(TEXT("ROUTE"), FLinearColor(0.8f, 0.8f, 0.8f), 
								TAttribute<EVisibility>::CreateLambda([InItem]{ return (InItem->bIsInActivePath && !InItem->bIsFocused && !InItem->bIsLeaf) ? EVisibility::Visible : EVisibility::Collapsed; }))
						]

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							NameWidget
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda(GetSubText)
						.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.Visibility_Lambda([GetSubText](){ return GetSubText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					]
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
	// Recover Subsystem if lost (e.g. PIE restart or opened before PIE)
	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}
	
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
	FString MouseStr;
	switch (Config.GetMouseCaptureMode())
	{
	case EMouseCaptureMode::NoCapture: MouseStr = "Capture";
		break;
	case EMouseCaptureMode::CapturePermanently: MouseStr = "Capture Perm";
		break;
	case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown: MouseStr = "Capture Perm+Initial";
		break;
	case EMouseCaptureMode::CaptureDuringMouseDown: MouseStr = "Capture Down";
		break;
	case EMouseCaptureMode::CaptureDuringRightMouseDown: MouseStr = "Capture RMB Down";
		break;
	}
	return FString::Printf(TEXT("[%s | %s]"), *ModeStr, *MouseStr);
}

void SCommonUIHierarchyView::UpdateTree()
{
	if (!DebugSubsystem.IsValid()) return;
	UGameInstance* GI = DebugSubsystem->GetGameInstance();
	ULocalPlayer* LP = GI ? GI->GetFirstGamePlayer() : nullptr;
	if (!LP) return;

	TSet<TSharedPtr<FCommonUITreeItem>> ActiveItems;
	TMap<UWidget*, TSharedPtr<FCommonUITreeItem>> WorkingMap;

	// Gather Nodes
	TArray<UWidget*> FoundWidgets;
	for (TObjectIterator<UWidget> It; It; ++It)
	{
		const bool bIsActivatable = It->IsA<UCommonActivatableWidget>();
		const bool bIsContainer = It->IsA<UCommonActivatableWidgetContainerBase>();
		if ((bIsActivatable || bIsContainer) && It->GetWorld() == DebugSubsystem->GetWorld())
		{
			FoundWidgets.Add(*It);
		}
	}

	// Build Cache and Reset Flags
	for (UWidget* Widget : FoundWidgets)
	{
		TWeakObjectPtr<UWidget> WeakWidget(Widget);
		TSharedPtr<FCommonUITreeItem> Item = WidgetItemCache.FindRef(WeakWidget);
		if (!Item.IsValid())
		{
			Item = MakeShared<FCommonUITreeItem>();
			Item->Widget = Widget;
			WidgetItemCache.Add(WeakWidget, Item);
		}

		Item->Name = Widget->GetName();
		Item->bIsContainer = Widget->IsA<UCommonActivatableWidgetContainerBase>();
		Item->bIsFocused = Widget->HasKeyboardFocus() || Widget->HasUserFocus(LP->GetPlayerController(LP->GetWorld()));
		Item->bIsLeaf = false;
		Item->bIsInActivePath = false;
		Item->Children.Empty();

		if (UCommonActivatableWidget* AW = Cast<UCommonActivatableWidget>(Widget))
		{
			Item->bIsActive = AW->IsActivated();
			Item->InputConfig = GetInputConfigString(AW);
			Item->DesiredFocusTarget = AW->GetDesiredFocusTarget() ? AW->GetDesiredFocusTarget()->GetName() : TEXT("None");
		}
		else
		{
			Item->bIsActive = true; 
			Item->InputConfig.Empty();
			Item->DesiredFocusTarget.Empty();
		}

		WorkingMap.Add(Widget, Item);
		ActiveItems.Add(Item);
	}

	// Clean Stale Cache
	TArray<TWeakObjectPtr<UWidget>> KeysToRemove;
	for (auto& Pair : WidgetItemCache) { if (!ActiveItems.Contains(Pair.Value)) KeysToRemove.Add(Pair.Key); }
	for (TWeakObjectPtr<UWidget>& Key : KeysToRemove) WidgetItemCache.Remove(Key);

	// Link Hierarchy and Identify Leaf
	const FInputOverlayState& OverlayState = DebugSubsystem->GetOverlayState();
	TArray<TSharedPtr<FCommonUITreeItem>> NewRoots;

	for (auto& Pair : WorkingMap)
	{
		UWidget* Widget = Pair.Key;
		TSharedPtr<FCommonUITreeItem> Item = Pair.Value;

		// Mark logic leaf from Subsystem snapshot
		if (Widget->GetName() == OverlayState.ActiveCommonUILeaf)
		{
			Item->bIsLeaf = true;
		}

		// Find parent via Slate tree
		UWidget* FoundParent = nullptr;
		TSharedPtr<SWidget> Walker = Widget->GetCachedWidget();
		while (Walker.IsValid())
		{
			Walker = Walker->GetParentWidget();
			if (!Walker.IsValid()) break;

			UWidget* Owner = InputFlowHelpers::GetOwnerUWidget(Walker);
			if (Owner && Owner != Widget && WorkingMap.Contains(Owner))
			{
				FoundParent = Owner;
				break;
			}
		}

		if (FoundParent)
		{
			WorkingMap[FoundParent]->Children.Add(Item);
		}
		else
		{
			Item->bIsRoot = true;
			NewRoots.Add(Item);
		}
	}

	// Calculate Active Route (Recursively mark ancestors of Focus or Leaf)
	for (auto& Pair : WorkingMap)
	{
		if (Pair.Value->bIsFocused || Pair.Value->bIsLeaf)
		{
			UWidget* PathWalker = Pair.Key;
			while (PathWalker)
			{
				if (TSharedPtr<FCommonUITreeItem> PathItem = WorkingMap.FindRef(PathWalker))
				{
					PathItem->bIsInActivePath = true;
				}

				// Find next parent in our map
				UWidget* NextParent = nullptr;
				TSharedPtr<SWidget> SWalker = PathWalker->GetCachedWidget();
				if (SWalker.IsValid())
				{
					SWalker = SWalker->GetParentWidget();
					while (SWalker.IsValid())
					{
						UWidget* Owner = InputFlowHelpers::GetOwnerUWidget(SWalker);
						if (Owner && WorkingMap.Contains(Owner)) { NextParent = Owner; break; }
						SWalker = SWalker->GetParentWidget();
					}
				}
				PathWalker = NextParent;
			}
		}
	}

	// Final Sort and Refresh
	auto SortFunc = [](const TSharedPtr<FCommonUITreeItem>& A, const TSharedPtr<FCommonUITreeItem>& B)
	{
		return A->Name < B->Name;
	};

	NewRoots.Sort(SortFunc);
	for (auto& Pair : WorkingMap) Pair.Value->Children.Sort(SortFunc);

	Roots = NewRoots;
	TreeView->RequestTreeRefresh();

	// Auto-expand the active path
	for (auto& Pair : WorkingMap)
	{
		if (Pair.Value->bIsInActivePath) TreeView->SetItemExpansion(Pair.Value, true);
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