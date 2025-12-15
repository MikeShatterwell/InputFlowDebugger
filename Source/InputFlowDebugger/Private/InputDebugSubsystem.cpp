// Copyright Mike Desrosiers, All Rights Reserved.

// InputFlowDebugger
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "LogInputFlow.h"
#include "SInputFlowOverlay.h"

// Core
#include "Containers/Ticker.h"

// Engine
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

// EnhancedInput
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "InputAction.h"

// Slate
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Views/STableViewBase.h"

// SlateCore
#include "Input/HittestGrid.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWeakWidget.h"

// CommonUI
#include <Runtime/Slate/Private/Widgets/Views/SListPanel.h>
#include <Widgets/Views/STileView.h>

#include "CommonInputSubsystem.h"
#include "CommonActivatableWidget.h"
#include "CommonUITypes.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBinding.h"

static TAutoConsoleVariable<bool> CVarInputFlowOverlay(
	TEXT("InputFlow.Overlay"),
	false,
	TEXT("Toggles the Input Flow Debugger in-game overlay"),
	ECVF_Default
);

// ----------------------------------------------------------------------------------
// Accessor Hacks
// These classes provide access to protected members of Slate views required for accurate simulation
// ----------------------------------------------------------------------------------

class FTableViewAccess : public STableViewBase
{
public:
	static TSharedPtr<SListPanel> GetItemsPanel(const STableViewBase& View) { return ((const FTableViewAccess&)View).ItemsPanel; }
	static int32 GetItemsPerLine(const STableViewBase& View) { return ((const FTableViewAccess&)View).GetNumItemsPerLine(); }
	static EOrientation GetOrientation(const STableViewBase& View) { return ((const FTableViewAccess&)View).Orientation; }
	static const TSharedPtr<SScrollBar>& GetScrollBar(const STableViewBase& View) { return ((const FTableViewAccess&)View).ScrollBar; }
};

template <typename ItemType>
class SListViewAccess : public SListView<ItemType>
{
public:
	static bool GetSelectorItem(const STableViewBase& View, ItemType& OutItem)
	{
		const SListViewAccess<ItemType>* AccessedView = static_cast<const SListViewAccess<ItemType>*>(&View);
		
		if (TListTypeTraits<ItemType>::IsPtrValid(AccessedView->SelectorItem))
		{
			OutItem = TListTypeTraits<ItemType>::NullableItemTypeConvertToItemType(AccessedView->SelectorItem);
			return true;
		}
		return false;
	}
	static TSharedPtr<ITableRow> GetWidgetFromItem(const STableViewBase& View, const ItemType& Item)
	{
		const SListView<ItemType>* TypedView = static_cast<const SListView<ItemType>*>(&View);
		return TypedView->WidgetFromItem(Item);
	}
};

class FListViewUObjectAccess : public SListView<UObject*>
{
public:
	static const UObject* GetSelectorItem(const SListView<UObject*>& List) { return ((const FListViewUObjectAccess&)List).SelectorItem; }
	static bool GetHandleDirectionalNavigation(const SListView<UObject*>& List) { return ((const FListViewUObjectAccess&)List).bHandleDirectionalNavigation; }
};

template <typename ItemType>
class FTileViewAccess : public STileView<ItemType>
{
public:
	static bool IsWrappingEnabled(const STileView<ItemType>& View) { return ((const FTileViewAccess<ItemType>&)View).bWrapHorizontalNavigation; }
};

// ----------------------------------------------------------------------------------
// Helper Functions
// ----------------------------------------------------------------------------------

/** 
 * Drills down into a widget hierarchy (e.g., a TableRow or UserWidget) to find the actual 
 * leaf widget that accepts focus (e.g., a CommonButton). 
 * 
 * Modified to aggressively drill through containers like SObjectWidget (UserWidget) and STableRow
 * even if they claim to support focus, as they often delegate focus to children in practice.
 */
static TSharedPtr<SWidget> ResolveFocusableDescendant(TSharedPtr<SWidget> Root)
{
	if (!Root.IsValid()) return nullptr;

	TSharedPtr<SWidget> BestCandidate = nullptr;

	// Breadth-first search
	TArray<TSharedPtr<SWidget>> Queue;
	Queue.Add(Root);

	int32 LoopCount = 0;
	const int32 MaxLoop = 200; // Safety cap

	while (Queue.Num() > 0 && LoopCount < MaxLoop)
	{
		TSharedPtr<SWidget> Current = Queue[0];
		Queue.RemoveAt(0);

		if (Current.IsValid() && Current->GetVisibility().IsVisible() && Current->IsEnabled())
		{
			FString Type = Current->GetTypeAsString();

			// Is this a container wrapper?
			bool bIsWrapper = (Type == TEXT("SObjectWidget") || 
							   Type.Contains(TEXT("TableRow")) || 
							   Type.Contains(TEXT("ListItem")) ||
							   Type.Contains(TEXT("SBox")) ||
							   Type.Contains(TEXT("SOverlay")) ||
							   Type.Contains(TEXT("SBorder")));

			if (Current->SupportsKeyboardFocus())
			{
				// If we haven't found a candidate yet, take this one
				if (!BestCandidate.IsValid())
				{
					BestCandidate = Current;
				}
				// If we have a candidate, but this one is NOT a wrapper and the candidate WAS a wrapper,
				// update candidate.
				else if (!bIsWrapper)
				{
					BestCandidate = Current;
					// If we found a concrete interactive element, we can stop searching children of this branch?
					// Ideally yes, but CommonUI sometimes nests buttons.
					// For now, assume first non-wrapper is good.
					return BestCandidate;
				}
			}

			// Continue searching children to find something "deeper" / "more specific"
			FChildren* Children = Current->GetChildren();
			if (Children)
			{
				for (int32 i = 0; i < Children->Num(); ++i)
				{
					Queue.Add(Children->GetChildAt(i));
				}
			}
		}
		LoopCount++;
	}

	// If we found nothing better, return root (if focusable) or nullptr
	if (!BestCandidate.IsValid() && Root->IsEnabled() && Root->SupportsKeyboardFocus())
	{
		return Root;
	}

	return BestCandidate; 
}

// ----------------------------------------------------------------------------------
// UInputDebugSubsystem Implementation
// ----------------------------------------------------------------------------------

void UInputDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Pre-allocate ring buffer
	LogHistory.SetNum(MaxLogHistorySize);
	LogHistoryIndex = 0;
	bLogHistoryWrapped = false;

	InputSpy = MakeShared<FInputFlowSpy>();
	if (ensure(FSlateApplication::IsInitialized()))
	{
		FSlateApplication::Get().RegisterInputPreProcessor(InputSpy);
		InputSpy->OnFocusChanged().AddUObject(this, &UInputDebugSubsystem::OnSpyFocusChanged);
	}

	LogSyncTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UInputDebugSubsystem::TickSyncLogs),
		0.1f
	);
}

void UInputDebugSubsystem::Deinitialize()
{
	if (LogSyncTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LogSyncTickerHandle);
		LogSyncTickerHandle.Reset();
	}

	if (FSlateApplication::IsInitialized() && InputSpy.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputSpy);
	}
	InputSpy.Reset();
	LogHistory.Empty();

	SetOverlayEnabled(false);

	Super::Deinitialize();
}

void UInputDebugSubsystem::ClearLogHistory()
{
	LogHistory.Empty();
	LogHistory.SetNum(MaxLogHistorySize);
	LogHistoryIndex = 0;
	bLogHistoryWrapped = false;
	LogVersion++; 
}

bool UInputDebugSubsystem::TickSyncLogs(float DeltaTime)
{
	if (InputSpy.IsValid())
	{
		const TArray<FInputEventLog>& SpyBuffer = InputSpy->GetEventLogBuffer();
		int32 SpyWriteIdx = InputSpy->GetWriteIndex();
		bool bSpyWrapped = InputSpy->IsWrapped();

		if (SpyWriteIdx > 0 || bSpyWrapped)
		{
			if (LogHistory.Num() != MaxLogHistorySize)
			{
				LogHistory.SetNum(MaxLogHistorySize);
			}

			int32 Start = bSpyWrapped ? SpyWriteIdx : 0;
			int32 Count = bSpyWrapped ? SpyBuffer.Num() : SpyWriteIdx;

			for (int32 i = 0; i < Count; ++i)
			{
				int32 ActualIdx = (Start + i) % SpyBuffer.Num();
				const FInputEventLog& RawLog = SpyBuffer[ActualIdx];

				bool bMerged = false;
				if (LogHistoryIndex > 0 || bLogHistoryWrapped)
				{
					uint32 PrevHistIdx = (LogHistoryIndex == 0) ? MaxLogHistorySize - 1 : LogHistoryIndex - 1;
					if (LogHistory.IsValidIndex(PrevHistIdx))
					{
						TSharedPtr<FInputEventLog> LastEntry = LogHistory[PrevHistIdx];

						if (LastEntry.IsValid() &&
							LastEntry->EventType.Equals(RawLog.EventType) &&
							LastEntry->InputDetails.Equals(RawLog.InputDetails) &&
							LastEntry->WidgetName.Equals(RawLog.WidgetName) &&
							LastEntry->WidgetState.Equals(RawLog.WidgetState))
						{
							LastEntry->Count++;
							LastEntry->TimeSeconds = RawLog.TimeSeconds;
							LastEntry->CaptureTime = RawLog.CaptureTime;
							bMerged = true;
						}
					}
				}

				if (!bMerged)
				{
					LogHistory[LogHistoryIndex] = MakeShared<FInputEventLog>(RawLog);
					LogHistoryIndex++;
					if (LogHistoryIndex >= MaxLogHistorySize)
					{
						LogHistoryIndex = 0;
						bLogHistoryWrapped = true;
					}
				}
				LogVersion++;
			}
			InputSpy->ResetBuffer();
		}
	}

	bool bCVarState = CVarInputFlowOverlay.GetValueOnGameThread();
	if (bCVarState != bOverlayEnabled)
	{
		SetOverlayEnabled(bCVarState);
	}

	double Now = FPlatformTime::Seconds();
	FocusHistory.RemoveAll([Now](const FFocusHistoryEntry& Entry)
	{
		return !Entry.Widget.IsValid() || (Now - Entry.Timestamp) > 3.0;
	});

	if (bOverlayEnabled)
	{
		TickNavigationSim(DeltaTime);
	}

	UpdateDataSnapshot();

	return true;
}

void UInputDebugSubsystem::SetNavigationDepth(int32 NewDepth)
{
	NavigationSearchDepth = FMath::Clamp(NewDepth, 1, 5);
}

void UInputDebugSubsystem::SetOverlayEnabled(bool bEnabled)
{
	if (bOverlayEnabled == bEnabled) return;

	bOverlayEnabled = bEnabled;
	CVarInputFlowOverlay->Set(bEnabled, ECVF_SetByConsole);

	if (bEnabled)
	{
		if (GetWorld() && GetWorld()->GetGameViewport())
		{
			OverlayWidget = SNew(SInputFlowOverlay).Subsystem(this);

			SAssignNew(OverlayHost, SWeakWidget)
			.PossiblyNullContent(OverlayWidget.ToSharedRef());

			GetWorld()->GetGameViewport()->AddViewportWidgetContent(
				OverlayHost.ToSharedRef(),
				1000 
			);
		}
	}
	else
	{
		if (OverlayHost.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
		{
			GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(OverlayHost.ToSharedRef());
		}

		OverlayHost.Reset();
		OverlayWidget.Reset();
	}
}

bool UInputDebugSubsystem::IsOverlayEnabled() const
{
	return bOverlayEnabled;
}

void UInputDebugSubsystem::UpdateDataSnapshot()
{
	OverlayState = FInputOverlayState();

	ULocalPlayer* LP = GetGameInstance()->GetFirstGamePlayer();
	if (!LP) return;

	TSharedPtr<SWidget> FocusedSlateWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (FocusedSlateWidget.IsValid())
	{
		if (UCommonActivatableWidget* Active = UCommonUIActionRouterBase::FindActivatable(FocusedSlateWidget, LP))
		{
			OverlayState.ActiveCommonUILeaf = Active->GetName();

			FUIInputConfig Config = Active->GetDesiredInputConfig().Get(FUIInputConfig());

			switch (Config.GetInputMode())
			{
			case ECommonInputMode::Game: OverlayState.InputConfig = TEXT("Game"); break;
			case ECommonInputMode::Menu: OverlayState.InputConfig = TEXT("Menu"); break;
			case ECommonInputMode::All: OverlayState.InputConfig = TEXT("All"); break;
			default: OverlayState.InputConfig = TEXT("Default"); break;
			}

			switch (Config.GetMouseCaptureMode())
			{
			case EMouseCaptureMode::NoCapture: OverlayState.MouseCaptureMode = TEXT("No Capture"); break;
			case EMouseCaptureMode::CapturePermanently: OverlayState.MouseCaptureMode = TEXT("Capture Perm"); break;
			case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown: OverlayState.MouseCaptureMode = TEXT("Capture Perm+"); break;
			case EMouseCaptureMode::CaptureDuringMouseDown: OverlayState.MouseCaptureMode = TEXT("Capture Down"); break;
			default: OverlayState.MouseCaptureMode = TEXT("Unknown"); break;
			}
		}
	}

	if (UCommonUIActionRouterBase* Router = ULocalPlayer::GetSubsystem<UCommonUIActionRouterBase>(LP))
	{
		TArray<FUIActionBindingHandle> Bindings = Router->GatherActiveBindings();
		UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();

		for (const FUIActionBindingHandle& Handle : Bindings)
		{
			if (!Handle.IsValid()) continue;

			FString ActionName = Handle.GetActionName().ToString();
			FString DisplayName = Handle.GetDisplayName().ToString();
			FString Entry = ActionName;

			if (!DisplayName.IsEmpty() && DisplayName != ActionName)
			{
				Entry += FString::Printf(TEXT(" [%s]"), *DisplayName);
			}

			FString KeyString;
			TSharedPtr<FUIActionBinding> BindingPtr = FUIActionBinding::FindBinding(Handle);

			if (BindingPtr.IsValid())
			{
				if (const UInputAction* InputAction = BindingPtr->InputAction.Get())
				{
					if (EISub)
					{
						TArray<FKey> Keys = EISub->QueryKeysMappedToAction(InputAction);
						for (const FKey& Key : Keys)
						{
							if (!KeyString.IsEmpty()) KeyString += TEXT(", ");
							KeyString += Key.GetDisplayName().ToString();
						}
					}
				}
				else if (const FCommonInputActionDataBase* LegacyData = CommonUI::GetInputActionData(BindingPtr->LegacyActionTableRow))
				{
					const FCommonInputTypeInfo& KbdInfo = LegacyData->GetInputTypeInfo(ECommonInputType::MouseAndKeyboard, NAME_None);
					FKey KbdKey = KbdInfo.GetKey();
					if (KbdKey.IsValid())
					{
						KeyString += KbdKey.GetDisplayName().ToString();
					}

					const FCommonInputTypeInfo& GamepadInfo = LegacyData->GetInputTypeInfo(ECommonInputType::Gamepad, NAME_None);
					FKey GamepadKey = GamepadInfo.GetKey();
					if (GamepadKey.IsValid())
					{
						if (!KeyString.IsEmpty()) KeyString += TEXT(", ");
						KeyString += GamepadKey.GetDisplayName().ToString();
					}
				}
			}

			if (!KeyString.IsEmpty())
			{
				Entry += FString::Printf(TEXT(" (%s)"), *KeyString);
			}

			OverlayState.BoundActions.AddUnique(Entry);
		}
	}

	if (OverlayState.ActiveCommonUILeaf.IsEmpty())
	{
		OverlayState.ActiveCommonUILeaf = TEXT("None (Viewport/PlayerController)");
		OverlayState.InputConfig = TEXT("Game");
		OverlayState.MouseCaptureMode = TEXT("Default");
	}

	if (UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		if (UEnhancedPlayerInput* PlayerInput = EISub->GetPlayerInput())
		{
			struct FInputFlowDebugAccessor : public UEnhancedPlayerInput
			{
				static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& GetContextData(const UEnhancedPlayerInput* PInput)
				{
					return static_cast<const FInputFlowDebugAccessor*>(PInput)->GetAppliedInputContextData();
				}
			};

			const auto& ContextMap = FInputFlowDebugAccessor::GetContextData(PlayerInput);
			for (const auto& Pair : ContextMap)
			{
				if (const UInputMappingContext* IMC = Pair.Key)
				{
					for (const auto& Mapping : IMC->GetMappings())
					{
						if (const UInputAction* Action = Mapping.Action)
						{
							if (const FInputActionInstance* Data = PlayerInput->FindActionInstanceData(Action))
							{
								const ETriggerEvent Trigger = Data->GetTriggerEvent();
								if (Trigger == ETriggerEvent::Triggered || Trigger == ETriggerEvent::Ongoing)
								{
									const FString Val = PlayerInput->GetActionValue(Action).ToString();
									FString Entry = FString::Printf(TEXT("%s: %s"), *Action->GetName(), *Val);
									OverlayState.ActiveEnhancedInputActions.AddUnique(Entry);
								}
							}
						}
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------------
// Navigation Spider & Simulation
// ----------------------------------------------------------------------------------

void UInputDebugSubsystem::TickNavigationSim(float DeltaTime)
{
	if (!bOverlayEnabled) return;
	if (!FSlateApplication::IsInitialized()) return;

	TSharedPtr<SWidget> CurrentFocus = FocusedWidget.Pin();

	if (CurrentFocus != LastSimulationStartWidget.Pin())
	{
		StartNewSimulation(CurrentFocus);
		return;
	}

	if (bSimulationInProgress)
	{
		ProcessSimulationQueue();
	}
}

void UInputDebugSubsystem::OnSpyFocusChanged(const TSharedPtr<SWidget>& NewFocus, const FFocusEvent& InFocusEvent)
{
	if (!NewFocus.IsValid() || !InputFlowHelpers::IsGameWorldWidget(NewFocus)) return;

	FocusedWidget = NewFocus;
	const EFocusCause Cause = InFocusEvent.GetCause();
	UE_LOG(LogInputFlow, Verbose, TEXT("Focus Changed Detected by Spy: %s, Reason: %s"),
		*InputFlowHelpers::GetWidgetDisplayName(NewFocus), *UEnum::GetValueAsString(Cause));

	if (NewFocus.IsValid())
	{
		if (FocusHistory.Num() == 0 || FocusHistory.Last().Widget != NewFocus)
		{
			FFocusHistoryEntry Entry;
			Entry.Widget = NewFocus;
			Entry.FocusCause = Cause;
			Entry.Timestamp = FPlatformTime::Seconds();
			FocusHistory.Add(Entry);

			if (FocusHistory.Num() > 20)
			{
				FocusHistory.RemoveAt(0);
			}
		}
	}

	TickNavigationSim(0.0f);
}

TPair<TSharedPtr<SWidget>, ENavSimResult> UInputDebugSubsystem::SimulateNavigation(
	const TSharedPtr<SWidget>& Source, EUINavigation Direction, int32 RealUserIndex) const
{
	const bool bDebugLog = true;

	if (!Source.IsValid() || !Source->SupportsKeyboardFocus() || !bEnableNavigationSimulation)
	{
		return {nullptr, ENavSimResult::Normal};
	}

	FWidgetPath SourcePath;
	if (!FSlateApplication::Get().FindPathToWidget(Source.ToSharedRef(), SourcePath))
	{
		return {nullptr, ENavSimResult::Normal};
	}

	const FNavigationEvent VirtualNavEvent(FModifierKeysState(), RealUserIndex, Direction, ENavigationGenesis::Controller);
	
	// Helper to identify container widgets (ListView, TileView)
	auto IsTableView = [](const TSharedPtr<SWidget>& InWidget, FString InTypeStr = TEXT("")) -> bool
	{
		if (!InWidget.IsValid()) return false;
		if (InTypeStr.IsEmpty()) InTypeStr = InWidget->GetTypeAsString();
		// Must be a View
		const bool bIsView = InTypeStr.Contains(TEXT("ListView")) || InTypeStr.Contains(TEXT("TileView")) || InTypeStr.Contains(TEXT("TreeView")) || InTypeStr.Contains(TEXT("TableView"));
		// Must NOT be a Row or Entry 
		const bool bIsRow = InTypeStr.Contains(TEXT("Row")) || InTypeStr.Contains(TEXT("Entry"));
		return bIsView && !bIsRow;
	};

	UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("--- Simulating %s from '%s' ---"),
		   *UEnum::GetValueAsString(Direction), *Source->ToString());

	for (int32 WidgetIndex = SourcePath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget& ArrangedBoundary = SourcePath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& BoundaryWidget = ArrangedBoundary.Widget;

		if (!BoundaryWidget->IsEnabled()) continue;

		const FString WidgetType = BoundaryWidget->GetTypeAsString();
		const bool bIsTableViewContainer = IsTableView(BoundaryWidget, WidgetType);

		FNavigationReply Reply = FNavigationReply::Escape();

		// --- TABLE VIEW INTERNAL LOGIC ---
		if (bIsTableViewContainer)
		{
			const STableViewBase* TableView = static_cast<const STableViewBase*>(&BoundaryWidget.Get());
			const bool bIsTileView = WidgetType.Contains(TEXT("TileView"));
			const EOrientation Orient = FTableViewAccess::GetOrientation(*TableView);
			const int32 ItemsPerLine = FTableViewAccess::GetItemsPerLine(*TableView);

			// 1. Identify the current index of the source widget inside this list.
			int32 CurrentIndex = INDEX_NONE;
			int32 NumItems = 0;
			
			TSharedPtr<SListPanel> ItemsPanel = FTableViewAccess::GetItemsPanel(*TableView);
			FChildren* Children = nullptr;
			
			if (ItemsPanel.IsValid())
			{
				Children = ItemsPanel->GetChildren();
				NumItems = Children->Num(); 
				
				if (Source != BoundaryWidget)
				{
					// Search for the visual child that contains our Source.
					// We check if the ChildRow is in the SourcePath. 
					// Since SourcePath contains [Window...TableView, Panel, ChildRow...Source],
					// we can just check if ChildRow exists in SourcePath's widget list.
					for (int32 i = 0; i < Children->Num(); ++i)
					{
						TSharedRef<SWidget> ChildRow = Children->GetChildAt(i);
						
						// Efficient check: iterate SourcePath starting from current WidgetIndex
						for (int32 k = WidgetIndex; k < SourcePath.Widgets.Num(); ++k)
						{
							if (SourcePath.Widgets[k].Widget == ChildRow)
							{
								CurrentIndex = i;
								break;
							}
						}
						
						if (CurrentIndex != INDEX_NONE) break;
					}
				}
			}

			// 2. Data Source Fallback (for UObject Lists if Visual Check Failed)
			bool bIsUObjectList = WidgetType.Contains(TEXT("UObject"));
			if (!bIsUObjectList && InputFlowHelpers::GetOwnerUWidget(BoundaryWidget)) bIsUObjectList = true;

			if (CurrentIndex == INDEX_NONE && bIsUObjectList)
			{
				const SListView<UObject*>* CastedList = static_cast<const SListView<UObject*>*>(TableView);
				const TArrayView<const UObject* const> Items = CastedList->GetItems();
				const UObject* SelectorItem = FListViewUObjectAccess::GetSelectorItem(*CastedList);
				
				CurrentIndex = Items.Find(SelectorItem);
				NumItems = Items.Num();
			}

			if (CurrentIndex != INDEX_NONE)
			{
				int32 NextIndex = CurrentIndex;
				bool bHandledDirection = false;
				
				bool bWrapHorizontal = false;
				if (bIsTileView && bIsUObjectList)
				{
					bWrapHorizontal = FTileViewAccess<UObject*>::IsWrappingEnabled(*(static_cast<const STileView<UObject*>*>(TableView)));
				}

				// Standard ListView Navigation Math
				if (Orient == Orient_Vertical)
				{
					if (Direction == EUINavigation::Up) 
					{ 
						NextIndex -= ItemsPerLine; 
						bHandledDirection = true; 
					}
					else if (Direction == EUINavigation::Down) 
					{ 
						NextIndex += ItemsPerLine; 
						bHandledDirection = true; 
						// Jagged check
						if (NextIndex >= NumItems && NumItems > 0 && !bIsTileView)
						{
							const int32 NumLines = FMath::CeilToInt((float)NumItems / (float)ItemsPerLine);
							const int32 CurLine = FMath::CeilToInt((float)(CurrentIndex + 1) / (float)ItemsPerLine);
							if (CurLine < NumLines) NextIndex = NumItems - 1;
						}
					}
					else if (Direction == EUINavigation::Left) 
					{ 
						if (bIsTileView && (bWrapHorizontal || (CurrentIndex % ItemsPerLine) > 0))
						{
							NextIndex -= 1; bHandledDirection = true; 
						}
					} 
					else if (Direction == EUINavigation::Right) 
					{ 
						if (bIsTileView && (bWrapHorizontal || (CurrentIndex % ItemsPerLine) < (ItemsPerLine - 1)))
						{
							NextIndex += 1; bHandledDirection = true; 
						}
					}
				}
				else // Horizontal
				{
					if (Direction == EUINavigation::Left) 
					{ 
						NextIndex -= ItemsPerLine; bHandledDirection = true; 
					}
					else if (Direction == EUINavigation::Right) 
					{ 
						NextIndex += ItemsPerLine; bHandledDirection = true; 
						if (NextIndex >= NumItems && NumItems > 0 && !bIsTileView)
						{
							const int32 NumLines = FMath::CeilToInt((float)NumItems / (float)ItemsPerLine);
							const int32 CurLine = FMath::CeilToInt((float)(CurrentIndex + 1) / (float)ItemsPerLine);
							if (CurLine < NumLines) NextIndex = NumItems - 1;
						}
					}
					else if (Direction == EUINavigation::Up) 
					{ 
						if (bIsTileView && (bWrapHorizontal || (CurrentIndex % ItemsPerLine) > 0))
						{
							NextIndex -= 1; bHandledDirection = true;
						}
					}
					else if (Direction == EUINavigation::Down) 
					{ 
						if (bIsTileView && (bWrapHorizontal || (CurrentIndex % ItemsPerLine) < (ItemsPerLine - 1)))
						{
							NextIndex += 1; bHandledDirection = true; 
						}
					}
				}
				
				// List (ItemsPerLine=1) ignores orthogonal directions usually, allowing bubble up
				if (ItemsPerLine == 1)
				{
					if (Orient == Orient_Vertical && (Direction == EUINavigation::Left || Direction == EUINavigation::Right)) bHandledDirection = false; 
					else if (Orient == Orient_Horizontal && (Direction == EUINavigation::Up || Direction == EUINavigation::Down)) bHandledDirection = false;
				}

				if (bHandledDirection)
				{
					// Check bounds
					if (NextIndex >= 0 && NextIndex < NumItems)
					{
						// Valid move. Convert Index -> Widget.
						
						// 1. Try Visual Children (Best for Overlay)
						if (Children && NextIndex < Children->Num())
						{
							TSharedRef<SWidget> NextWidget = Children->GetChildAt(NextIndex);
							
							// Resolve the row (which might be an ObjectTableRow) to the actual focusable button inside it
							TSharedPtr<SWidget> ResolvedTarget = ResolveFocusableDescendant(NextWidget);
							
							UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [ListView Internal] Found visible neighbor row index %d: '%s'. Resolved to '%s'"), 
								NextIndex, *NextWidget->ToString(), *ResolvedTarget->ToString());
								
							return {ResolvedTarget, ENavSimResult::Normal};
						}
						
						// 2. Try Data Item -> Widget map
						if (bIsUObjectList)
						{
							const SListView<UObject*>* CastedList = static_cast<const SListView<UObject*>*>(TableView);
							const TArrayView<UObject* const> Items = CastedList->GetItems();
							
							if (Items.IsValidIndex(NextIndex))
							{
								UObject* Item = Items[NextIndex];
								TSharedPtr<ITableRow> Row = CastedList->WidgetFromItem(Item);
								if (Row.IsValid())
								{
									TSharedPtr<SWidget> ResolvedTarget = ResolveFocusableDescendant(Row->AsWidget());
									return {ResolvedTarget, ENavSimResult::Normal};
								}
								else
								{
									// Item exists but widget is not generated (Offscreen)
									// We report the List itself as handling it (Scrolling)
									UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [ListView Internal] Target valid but offscreen. Scrolling."));
									return {BoundaryWidget, ENavSimResult::Handled};
								}
							}
						}
					}
					else
					{
						// Out of bounds -> Check Scrollbar or Overscroll behavior
						TSharedPtr<SScrollBar> ScrollBar = FTableViewAccess::GetScrollBar(*TableView);
						bool bCanScroll = false;
						if (ScrollBar.IsValid() && ScrollBar->IsNeeded())
						{
							// If we are at the visual end but there is scroll distance left, we consume the event
							if (NextIndex >= NumItems && ScrollBar->DistanceFromBottom() > 0.001f) bCanScroll = true;
							if (NextIndex < 0 && ScrollBar->DistanceFromTop() > 0.001f) bCanScroll = true;
						}

						if (bCanScroll)
						{
							UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [ListView Internal] Offscreen navigation implies scrolling. Handled."));
							return {BoundaryWidget, ENavSimResult::Handled};
						}
						
						// Explicitly bubbled out of the list (e.g. hitting top of list and pressing Up)
						UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [ListView Internal] End of list reached. Bubbling up."));
					}
				}
			}
		}
		// --- STANDARD WIDGET LOGIC ---
		else 
		{
#if UE_WITH_SLATE_SIMULATEDNAVIGATIONMETADATA
			if (TSharedPtr<FSimulatedNavigationMetaData> SimMeta = BoundaryWidget->GetMetaData<FSimulatedNavigationMetaData>())
			{
				if (SimMeta->IsOnNavigationConst())
				{
					Reply = BoundaryWidget->OnNavigation(ArrangedBoundary.Geometry, VirtualNavEvent);
				}
				else
				{
					EUINavigation Type = VirtualNavEvent.GetNavigationType();
					EUINavigationRule MetaRule = SimMeta->GetBoundaryRule(Type);
					TSharedPtr<SWidget> MetaTarget = SimMeta->GetFocusRecipient(Type).Pin();

					switch (MetaRule)
					{
					case EUINavigationRule::Explicit: Reply = FNavigationReply::Explicit(MetaTarget); break;
					case EUINavigationRule::Custom:
					case EUINavigationRule::CustomBoundary: Reply = FNavigationReply::Custom(FNavigationDelegate()); break;
					case EUINavigationRule::Stop: Reply = FNavigationReply::Stop(); break;
					case EUINavigationRule::Wrap: Reply = FNavigationReply::Wrap(); break;
					case EUINavigationRule::Escape: default: Reply = FNavigationReply::Escape(); break;
					}
				}
			}
			else
#endif
			{
				Reply = BoundaryWidget->OnNavigation(ArrangedBoundary.Geometry, VirtualNavEvent);
			}
		}

		EUINavigationRule Rule = Reply.GetBoundaryRule();

		// Handle Explicit / Stop / Custom
		if (Rule == EUINavigationRule::Explicit)
		{
			TSharedPtr<SWidget> ExplicitTarget = Reply.GetFocusRecipient();
			if (ExplicitTarget.IsValid() && ExplicitTarget->IsEnabled() && ExplicitTarget->SupportsKeyboardFocus())
			{
				return {ExplicitTarget, ENavSimResult::Normal};
			}
			Rule = EUINavigationRule::Escape;
		}
		else if (Rule == EUINavigationRule::Stop)
		{
			return {BoundaryWidget, ENavSimResult::Stopped};
		}
		else if (Rule == EUINavigationRule::Custom)
		{
			return {BoundaryWidget, ENavSimResult::Handled};
		}

		// Perform Spatial Search via Hittest Grid
		TSharedPtr<SWidget> ResultWidget = nullptr;
		TSharedRef<SWindow> Window = SourcePath.GetDeepestWindow();
		const FSlateLayoutTransform WindowInverse = Window->GetWindowGeometryInScreen().GetAccumulatedLayoutTransform().Inverse();

		if (Direction == EUINavigation::Next || Direction == EUINavigation::Previous)
		{
			FWeakWidgetPath WeakSource(SourcePath);
			FWidgetPath NextPath = WeakSource.ToNextFocusedPath(Direction, Reply, ArrangedBoundary);
			if (NextPath.IsValid()) ResultWidget = NextPath.Widgets.Last().Widget;
		}
		else
		{
			// Transform to window space for HittestGrid
			FArrangedWidget WindowSpaceLeaf = SourcePath.Widgets.Last();
			WindowSpaceLeaf.Geometry.AppendTransform(WindowInverse);

			FArrangedWidget WindowSpaceBoundary = ArrangedBoundary;
			WindowSpaceBoundary.Geometry.AppendTransform(WindowInverse);

			FScopedSwitchWorldHack SwitchWorld(SourcePath);
			FNavigationReply GridRule = (Rule == EUINavigationRule::Wrap) ? FNavigationReply::Wrap() : FNavigationReply::Escape();
			
			ResultWidget = Window->GetHittestGrid().FindNextFocusableWidget(
				WindowSpaceLeaf, Direction, GridRule, WindowSpaceBoundary, RealUserIndex
			);
		}

		// Resolve Target (Drill down if target is a TableView found spatially)
		if (ResultWidget.IsValid() && ResultWidget != BoundaryWidget && InputFlowHelpers::IsGameWorldWidget(ResultWidget))
		{
			// If we found a TableView via spatial search (e.g. pressing Down into a List), 
			// we want to point to the *entry* that will actually get focus, not the list container.
			if (IsTableView(ResultWidget))
			{
				const STableViewBase* ResultTableView = static_cast<const STableViewBase*>(ResultWidget.Get());
				TSharedPtr<SListPanel> ResultItemsPanel = FTableViewAccess::GetItemsPanel(*ResultTableView);
				
				if (ResultItemsPanel.IsValid())
				{
					FChildren* ResultChildren = ResultItemsPanel->GetChildren();
					if (ResultChildren->Num() > 0)
					{
						int32 EntryIndex = 0;
						// If coming from Bottom/Right, enter at end. Otherwise Top/Left defaults to 0.
						if (Direction == EUINavigation::Up || Direction == EUINavigation::Left)
						{
							EntryIndex = ResultChildren->Num() - 1;
						}
						
						TSharedRef<SWidget> EntryWidget = ResultChildren->GetChildAt(EntryIndex);
						TSharedPtr<SWidget> ResolvedEntry = ResolveFocusableDescendant(EntryWidget);

						UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [ResolveTarget] Target '%s' is TableView. Resolved to entry %d: '%s' -> '%s'"), 
							*ResultWidget->ToString(), EntryIndex, *EntryWidget->ToString(), *ResolvedEntry->ToString());
						
						ResultWidget = ResolvedEntry;
					}
				}
			}

			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Search] Boundary '%s' -> Found '%s'"),
								  *BoundaryWidget->ToString(), *ResultWidget->ToString());
			return {ResultWidget, ENavSimResult::Normal};
		}

		if (Rule == EUINavigationRule::Wrap)
		{
			return {BoundaryWidget, ENavSimResult::Normal};
		}
	}

	return {nullptr, ENavSimResult::Normal};
}

void UInputDebugSubsystem::ProcessSimulationQueue()
{
	const double StartTime = FPlatformTime::Seconds();
	const int32 MaxDepth = NavigationSearchDepth;

	while (SimulationQueue.Num() > 0)
	{
		if ((FPlatformTime::Seconds() - StartTime) > MaxSimulationTimePerFrame) return;

		FSimQueueItem CurrentItem = SimulationQueue[0];
		SimulationQueue.RemoveAt(0);

		TSharedPtr<SWidget> CurrentWidget = CurrentItem.Widget.Pin();
		int32 CurrentDepth = CurrentItem.Depth;

		if (!CurrentWidget.IsValid() || CurrentDepth >= MaxDepth) continue;

		constexpr EUINavigation Directions[] = {
			EUINavigation::Up, EUINavigation::Down, EUINavigation::Left, EUINavigation::Right
		};
		const int32 RealUserIndex = FSlateApplication::Get().GetUserIndexForKeyboard();

		for (const EUINavigation Dir : Directions)
		{
			TPair<TSharedPtr<SWidget>, ENavSimResult> SimResult = SimulateNavigation(CurrentWidget, Dir, RealUserIndex);
			TSharedPtr<SWidget> Next = SimResult.Key;
			ENavSimResult ResultType = SimResult.Value;

			if (Next.IsValid() && InputFlowHelpers::IsGameWorldWidget(Next))
			{
				bool bIsTerminal = (ResultType == ENavSimResult::Handled || ResultType == ENavSimResult::Stopped);

				// Filter Logic: Ignore internal links if navigation is "Normal"
				bool bIsSameWidget = (Next == CurrentWidget);
				bool bIsInternal = false;

				if (!bIsSameWidget && ResultType == ENavSimResult::Normal)
				{
					FWidgetPath PathToNext;
					FSlateApplication::Get().GeneratePathToWidgetUnchecked(Next.ToSharedRef(), PathToNext);

					if (PathToNext.ContainsWidget(CurrentWidget.Get()))
					{
						bIsInternal = true; // Next is a child of Current
					}
					else
					{
						FWidgetPath PathToCurrent;
						FSlateApplication::Get().GeneratePathToWidgetUnchecked(
							CurrentWidget.ToSharedRef(), PathToCurrent);
						if (PathToCurrent.ContainsWidget(Next.Get()))
						{
							bIsInternal = true; // Current is a child of Next
						}
					}
				}

				if ((bIsSameWidget || bIsInternal) && ResultType == ENavSimResult::Normal)
				{
					continue;
				}

				bool bIsVisited = VisitedWidgets.Contains(Next);
				bool bShouldRecord = bIsTerminal || !bIsVisited;

				if (bShouldRecord)
				{
					FNavigationLink Link;
					Link.StartWidget = CurrentWidget;
					Link.EndWidget = Next;
					Link.Direction = Dir;
					Link.DepthStep = CurrentDepth + 1;
					Link.ResultType = ResultType;

					NavigationLinks.Add(Link);

					if (ResultType == ENavSimResult::Normal && !bIsVisited)
					{
						VisitedWidgets.Add(Next);
						SimulationQueue.Add({Next, CurrentDepth + 1});
					}
				}
			}
		}
	}

	if (SimulationQueue.Num() == 0)
	{
		bSimulationInProgress = false;
	}
}

void UInputDebugSubsystem::StartNewSimulation(TSharedPtr<SWidget> StartWidget)
{
	NavigationLinks.Reset();
	SimulationQueue.Reset();
	VisitedWidgets.Reset();

	LastSimulationStartWidget = StartWidget;
	bSimulationInProgress = false;

	if (StartWidget.IsValid() && InputFlowHelpers::IsGameWorldWidget(StartWidget))
	{
		SimulationQueue.Add({StartWidget, 0});
		VisitedWidgets.Add(StartWidget);
		bSimulationInProgress = true;

		ProcessSimulationQueue();
	}
}

// --- Configuration Accessors ---

void UInputDebugSubsystem::SetCaptureMouseMove(bool bEnabled)
{
	if (InputSpy) InputSpy->bCaptureMouseMove = bEnabled;
}

bool UInputDebugSubsystem::GetCaptureMouseMove() const
{
	return InputSpy ? InputSpy->bCaptureMouseMove : false;
}

void UInputDebugSubsystem::SetCaptureClicks(bool bEnabled)
{
	if (InputSpy) InputSpy->bCaptureMouseClicks = bEnabled;
}

bool UInputDebugSubsystem::GetCaptureClicks() const
{
	return InputSpy ? InputSpy->bCaptureMouseClicks : false;
}

void UInputDebugSubsystem::SetCaptureHover(bool bEnabled)
{
	if (InputSpy) InputSpy->bCaptureHover = bEnabled;
}

bool UInputDebugSubsystem::GetCaptureHover() const
{
	return InputSpy ? InputSpy->bCaptureHover : false;
}

void UInputDebugSubsystem::SetCaptureAnalog(bool bEnabled)
{
	if (InputSpy) InputSpy->bCaptureAnalog = bEnabled;
}

bool UInputDebugSubsystem::GetCaptureAnalog() const
{
	return InputSpy ? InputSpy->bCaptureAnalog : false;
}

void UInputDebugSubsystem::SetCaptureFocus(bool bEnabled)
{
	if (InputSpy) InputSpy->bCaptureFocusEvents = bEnabled;
}

bool UInputDebugSubsystem::GetCaptureFocus() const
{
	return InputSpy ? InputSpy->bCaptureFocusEvents : false;
}

void UInputDebugSubsystem::SetCaptureKeyEvents(bool bEnabled)
{
	if (InputSpy) InputSpy->bCaptureKeyEvents = bEnabled;
}

bool UInputDebugSubsystem::GetCaptureKeyEvents() const
{
	return InputSpy ? InputSpy->bCaptureKeyEvents : false;
}