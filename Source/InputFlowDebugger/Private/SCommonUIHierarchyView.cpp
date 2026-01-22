// Copyright Mike Desrosiers, All Rights Reserved.

#include "SCommonUIHierarchyView.h"


#if WITH_PLUGIN_COMMONUI

// CommonUI
#include <CommonActivatableWidget.h>
#include <Input/CommonUIActionRouterBase.h>
#include <Widgets/CommonActivatableWidgetContainer.h>
#include <CommonUI/Private/Input/UIActionRouterTypes.h>

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

		// Color Logic: Focus (Green) > Leaf (Pink) > ActiveRoot (Orange) > Container (Blue) > Default
		auto GetNodeColor = [InItem]() -> FSlateColor
		{
			if (InItem->bIsFocused)    return FLinearColor(0.0f, 1.0f, 0.4f);  // Actual Slate Focus
			if (InItem->bIsLeaf)       return FLinearColor(1.0f, 0.4f, 0.8f);  // CommonUI Active Leaf
			if (InItem->bIsActiveRoot) return FLinearColor(1.0f, 0.6f, 0.2f);  // Active Root receiving input
			if (InItem->bIsContainer)  return FLinearColor(0.3f, 0.7f, 1.0f);  // Structure
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
				Parts.Add(FString::Printf(TEXT("Desired Focus Target: %s"), *InItem->DesiredFocusTarget));
			}
			if (!InItem->ActionDomain.IsEmpty())
			{
				Parts.Add(FString::Printf(TEXT("Domain: %s"), *InItem->ActionDomain));
			}
			return FText::FromString(FString::Join(Parts, TEXT(" | ")));
		};
		
		auto GetRowColor = [this]() -> FLinearColor
		{
			return (Item.IsValid() && Item->bIsActive) ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
		};
		
		this->ChildSlot
		[
			SNew(SBox).Padding(FMargin(0, 2))
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(FAppStyle::GetBrush("NoBrush"))
				.ColorAndOpacity_Lambda(GetRowColor)
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
							// Badge: Container
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("STACK"), FLinearColor(0.2f, 0.6f, 1.0f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsContainer ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
							]
							// Badge: Active Root (receives input priority)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("ACTIVE ROOT"), FLinearColor(1.0f, 0.6f, 0.2f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsActiveRoot ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
							]
							// Badge: Slate Focus
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("FOCUS"), FLinearColor(0.0f, 1.0f, 0.4f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsFocused ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
							]
							// Badge: Active Leaf (leafmost active node)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("LEAFMOST"), FLinearColor(1.0f, 0.4f, 0.8f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsLeaf ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
							]
							// Badge: Modal
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("MODAL"), FLinearColor(0.8f, 0.4f, 1.0f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return InItem->bIsModal ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
							]
							// Badge: Route (Only shown for ancestors, not the Focus/Leaf itself)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("ROUTE"), FLinearColor(0.8f, 0.8f, 0.8f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return (InItem->bIsInActivePath && !InItem->bIsFocused && !InItem->bIsLeaf) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
							]
							// Badge: Deactivated
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0).VAlign(VAlign_Center)
							[
								CreateBadge(TEXT("DEACTIVATED"), FLinearColor(0.5f, 0.5f, 0.5f), 
									TAttribute<EVisibility>::CreateLambda([InItem]{ return !InItem->bIsActive ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
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
	default: ModeStr = "Default"; break;
	}
	
	FString MouseStr;
	switch (Config.GetMouseCaptureMode())
	{
	case EMouseCaptureMode::NoCapture:                                    MouseStr = "No Capture"; break;
	case EMouseCaptureMode::CapturePermanently:                           MouseStr = "Capture Perm"; break;
	case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown: MouseStr = "Capture Perm+Initial"; break;
	case EMouseCaptureMode::CaptureDuringMouseDown:                       MouseStr = "Capture Down"; break;
	case EMouseCaptureMode::CaptureDuringRightMouseDown:                  MouseStr = "Capture RMB Down"; break;
	default:                                                              MouseStr = "Unknown"; break;
	}
	return FString::Printf(TEXT("[%s | %s]"), *ModeStr, *MouseStr);
}

void SCommonUIHierarchyView::UpdateTree()
{
	if (true)
	{
		
	}
	if (!DebugSubsystem.IsValid()) return;
	UGameInstance* GI = DebugSubsystem->GetGameInstance();
	ULocalPlayer* LP = GI ? GI->GetFirstGamePlayer() : nullptr;
	if (!IsValid(LP)) return;

	// Get the action router for querying active root
	UCommonUIActionRouterBase* ActionRouter = LP->GetSubsystem<UCommonUIActionRouterBase>();

	TSet<TSharedPtr<FCommonUITreeItem>> ActiveItems;
	TMap<UWidget*, TSharedPtr<FCommonUITreeItem>> WorkingMap;
	TMap<UWidget*, UWidget*> LogicalParentMap;

	// Gather Nodes
	TArray<UWidget*> FoundWidgets;
	TArray<UCommonActivatableWidgetContainerBase*> FoundContainers;
	for (TObjectIterator<UWidget> It; It; ++It)
	{
		if (It->GetWorld() != DebugSubsystem->GetWorld()) continue;

		if (UCommonActivatableWidget* AW = Cast<UCommonActivatableWidget>(*It))
		{
			FoundWidgets.Add(AW);
		}

		if (UCommonActivatableWidgetContainerBase* Container = Cast<UCommonActivatableWidgetContainerBase>(*It))
		{
			FoundContainers.Add(Container);
			FoundWidgets.Add(Container);
		}
	}

	// Build logical parent map from containers
	for (UCommonActivatableWidgetContainerBase* Container : FoundContainers)
	{
		for (UCommonActivatableWidget* Child : Container->GetWidgetList())
		{
			if (IsValid(Child)) LogicalParentMap.Add(Child, Container);
		}
	}

	// Peek inside the protected GeneratedWidgetsPool via reflection to find pooled widgets
	static FStructProperty* PoolProp = FindFProperty<FStructProperty>(UCommonActivatableWidgetContainerBase::StaticClass(), TEXT("GeneratedWidgetsPool"));
	if (PoolProp)
	{
		for (UCommonActivatableWidgetContainerBase* Container : FoundContainers)
		{
			FUserWidgetPool* PoolAddr = PoolProp->ContainerPtrToValuePtr<FUserWidgetPool>(Container);

			for (TFieldIterator<FArrayProperty> ArrayIt(PoolProp->Struct); ArrayIt; ++ArrayIt)
			{
				FArrayProperty* ArrayProp = *ArrayIt;
				
				if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
				{
					FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(PoolAddr));
					for (int32 i = 0; i < ArrayHelper.Num(); ++i)
					{
						UObject* Obj = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(i));
						if (UWidget* PooledWidget = Cast<UWidget>(Obj))
						{
							if (!LogicalParentMap.Contains(PooledWidget))
							{
								LogicalParentMap.Add(PooledWidget, Container);
							}
						}
					}
				}
			}
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
		Item->bIsFocused = Widget->HasAnyUserFocus();
		Item->bIsLeaf = false;
		Item->bIsInActivePath = false;
		Item->bIsActiveRoot = false;
		Item->bIsModal = false;
		Item->bIsInActiveRoot = false;
		Item->Children.Empty();

		if (UCommonActivatableWidget* AW = Cast<UCommonActivatableWidget>(Widget))
		{
			Item->bIsActive = AW->IsActivated();
			Item->InputConfig = GetInputConfigString(AW);
			Item->DesiredFocusTarget = AW->GetDesiredFocusTarget() ? AW->GetDesiredFocusTarget()->GetName() : TEXT("None");
			Item->bIsModal = AW->IsModal();
			Item->bSupportsActivationFocus = AW->SupportsActivationFocus();
			
			// Check if widget is in the active root (receiving input)
			if (IsValid(ActionRouter))
			{
				Item->bIsInActiveRoot = ActionRouter->IsWidgetInActiveRoot(AW);
			}
			
			// Get action domain info
			if (UCommonInputActionDomain* Domain = AW->GetCalculatedActionDomain())
			{
				Item->ActionDomain = Domain->GetName();
			}
			else
			{
				Item->ActionDomain.Empty();
			}
		}
		else if (UCommonActivatableWidgetContainerBase* Container = Cast<UCommonActivatableWidgetContainerBase>(Widget))
		{
			Item->bIsActive = Container->GetActiveWidget() != nullptr;
			Item->InputConfig.Empty();
			Item->DesiredFocusTarget.Empty();
			Item->ActionDomain.Empty();
		}
		else
		{
			Item->bIsActive = true;
			Item->InputConfig.Empty();
			Item->DesiredFocusTarget.Empty();
			Item->ActionDomain.Empty();
		}

		WorkingMap.Add(Widget, Item);
		ActiveItems.Add(Item);
	}

	// Clean Stale Cache
	TArray<TWeakObjectPtr<UWidget>> KeysToRemove;
	for (auto& Pair : WidgetItemCache) 
	{ 
		if (!ActiveItems.Contains(Pair.Value)) 
		{
			KeysToRemove.Add(Pair.Key); 
		}
	}
	for (TWeakObjectPtr<UWidget>& Key : KeysToRemove) 
	{
		WidgetItemCache.Remove(Key);
	}

	// Link Hierarchy
	TArray<TSharedPtr<FCommonUITreeItem>> NewRoots;

	for (auto& Pair : WorkingMap)
	{
		UWidget* Widget = Pair.Key;
		TSharedPtr<FCommonUITreeItem> Item = Pair.Value;

		// Priority 1: Check Explicit Logical Parent (Active in Stack/Queue)
		UWidget* FoundParent = nullptr;
		if (UWidget** LogParent = LogicalParentMap.Find(Widget))
		{
			if (WorkingMap.Contains(*LogParent))
			{
				FoundParent = *LogParent;
			}
		}

		// Priority 2: Find parent via Slate tree (Visual Hierarchy)
		if (!IsValid(FoundParent))
		{
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

	// Determine leafmost nodes and active roots by tree traversal
	// A widget is the leafmost if:
	// 1. It is activated
	// 2. It is in the active root
	// 3. It supports activation focus
	// 4. None of its descendants (that support activation focus) are also active + in active root
	TFunction<bool(const TSharedPtr<FCommonUITreeItem>&)> HasActiveDescendantInActiveRoot = 
		[&HasActiveDescendantInActiveRoot](const TSharedPtr<FCommonUITreeItem>& Item) -> bool
	{
		for (const TSharedPtr<FCommonUITreeItem>& Child : Item->Children)
		{
			if (Child->bIsActive && Child->bIsInActiveRoot && Child->bSupportsActivationFocus)
			{
				return true;
			}
			if (HasActiveDescendantInActiveRoot(Child))
			{
				return true;
			}
		}
		return false;
	};

	// Find leafmost nodes and mark active roots
	for (auto& Pair : WorkingMap)
	{
		const TSharedPtr<FCommonUITreeItem> Item = Pair.Value;
		if (!Item.IsValid()) continue;
		
		// Determine if this is the leafmost active node
		if (Item->bIsActive && Item->bIsInActiveRoot && Item->bSupportsActivationFocus)
		{
			if (!HasActiveDescendantInActiveRoot(Item))
			{
				Item->bIsLeaf = true;
			}
		}

		// A root widget in the active root tree is the "active root"
		// (Modal widgets also act as roots for input routing)
		if (Item->bIsRoot && Item->bIsInActiveRoot && Item->bIsActive)
		{
			Item->bIsActiveRoot = true;
		}
		else if (Item->bIsModal && Item->bIsActive && Item->bIsInActiveRoot)
		{
			Item->bIsActiveRoot = true;
		}

		if (!Item->bIsFocused && !Item->bIsLeaf) continue;

		// Mark active path up to root
		UWidget* PathWalker = Pair.Key;
		while (PathWalker)
		{
			if (TSharedPtr<FCommonUITreeItem> PathItem = WorkingMap.FindRef(PathWalker))
			{
				PathItem->bIsInActivePath = true;
			}

			UWidget* NextParent = nullptr;
			if (UWidget** LogParent = LogicalParentMap.Find(PathWalker))
			{
				if (WorkingMap.Contains(*LogParent)) NextParent = *LogParent;
			}
				
			if (!IsValid(NextParent))
			{
				TSharedPtr<SWidget> SWalker = PathWalker->GetCachedWidget();
				if (SWalker.IsValid())
				{
					SWalker = SWalker->GetParentWidget();
					while (SWalker.IsValid())
					{
						UWidget* Owner = InputFlowHelpers::GetOwnerUWidget(SWalker);
						if (IsValid(Owner) && WorkingMap.Contains(Owner)) 
						{ 
							NextParent = Owner; 
							break; 
						}
						SWalker = SWalker->GetParentWidget();
					}
				}
			}
			PathWalker = NextParent;
		}
	}
	// Sort siblings by Slate hierarchy order
	auto GetSlatePath = [](TSharedPtr<SWidget> Widget)
	{
		TArray<TSharedPtr<SWidget>> Path;
		while (Widget.IsValid())
		{
			Path.Add(Widget);
			Widget = Widget->GetParentWidget();
		}
		Algo::Reverse(Path);
		return Path;
	};

	auto HierarchySort = [&](const TSharedPtr<FCommonUITreeItem>& A, const TSharedPtr<FCommonUITreeItem>& B)
	{
		if (A == B) return false;
		if (!A.IsValid() || !B.IsValid()) return A.IsValid();
		
		if (!A->Widget.IsValid() || !B->Widget.IsValid()) 
		{
			if (A->Widget.IsValid() != B->Widget.IsValid())
			{
				return A->Widget.IsValid();
			}
			return A->Name < B->Name;
		}

		// Active widgets before inactive
		if (A->bIsActive != B->bIsActive) return A->bIsActive > B->bIsActive;

		TSharedPtr<SWidget> SA = A->Widget->GetCachedWidget();
		TSharedPtr<SWidget> SB = B->Widget->GetCachedWidget();

		if (!SA.IsValid() && !SB.IsValid())
		{
			return A->Name < B->Name;
		}
		if (!SA.IsValid() || !SB.IsValid()) return SA.IsValid(); 

		const TArray<TSharedPtr<SWidget>> PathA = GetSlatePath(SA);
		const TArray<TSharedPtr<SWidget>> PathB = GetSlatePath(SB);
		const int32 Count = FMath::Min(PathA.Num(), PathB.Num());
		
		for (int32 i = 0; i < Count; ++i)
		{
			if (PathA[i] != PathB[i])
			{
				if (i > 0)
				{
					TSharedPtr<SWidget> CommonParent = PathA[i - 1];
					FChildren* Children = CommonParent->GetChildren();
					int32 IndexA = -1, IndexB = -1;
					for (int32 c = 0; c < Children->Num(); ++c)
					{
						if (Children->GetChildAt(c) == PathA[i].ToSharedRef()) IndexA = c;
						if (Children->GetChildAt(c) == PathB[i].ToSharedRef()) IndexB = c;
					}
					if (IndexA != IndexB) return IndexA < IndexB;
				}
				return A->Name < B->Name;
			}
		}
		return PathA.Num() < PathB.Num();
	};

	NewRoots.Sort(HierarchySort);
	for (auto& Pair : WorkingMap) 
	{
		Pair.Value->Children.Sort(HierarchySort);
	}

	Roots = NewRoots;
	TreeView->RequestTreeRefresh();

	// Auto-expand the active path
	for (auto& Pair : WorkingMap)
	{
		if (Pair.Value->bIsInActivePath) 
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