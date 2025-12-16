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

// UMG
#include <Blueprint/WidgetTree.h>

// CommonUI
#include <CommonButtonBase.h>
#include <SCommonButtonTableRow.h>
#include <Components/PanelWidget.h>
#include <Runtime/Slate/Private/Widgets/Views/SListPanel.h>
#include <Slate/SObjectTableRow.h>
#include <Widgets/Views/STileView.h>

#include "CommonInputSubsystem.h"
#include "CommonActivatableWidget.h"
#include "CommonUITypes.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBinding.h"

static TAutoConsoleVariable<bool> CVarInputFlowOverlay(
	TEXT("InputFlow.Overlay"),
	true, // TODO: Default false after testing
	TEXT("Toggles the Input Flow Debugger in-game overlay"),
	ECVF_Default
);

static TAutoConsoleVariable<bool> CVarInputFlowNavSpiderDebugLog(
	TEXT("InputFlow.NavSpider.DebugLogEnabled"),
	true, // TODO: Default false after testing
	TEXT("Toggles the Input Flow Debugger in-game overlay"),
	ECVF_Default
);

// ----------------------------------------------------------------------------------
// Accessor Hacks
// ----------------------------------------------------------------------------------

class FTableViewAccess : public STableViewBase
{
public:
	static TSharedPtr<SListPanel> GetItemsPanel(const STableViewBase& View) { return ((const FTableViewAccess&)View).ItemsPanel; }
	static int32 GetItemsPerLine(const STableViewBase& View) { return ((const FTableViewAccess&)View).GetNumItemsPerLine(); }
	static EOrientation GetOrientation(const STableViewBase& View) { return ((const FTableViewAccess&)View).Orientation; }
	static const TSharedPtr<SScrollBar>& GetScrollBar(const STableViewBase& View) { return ((const FTableViewAccess&)View).ScrollBar; }
};

class FButtonAccess : public UButton
{
public:
	static TSharedPtr<SButton> GetSlateButton(const UButton* Button) 
	{ 
		if (!Button) return nullptr;
		return static_cast<const FButtonAccess*>(Button)->MyButton;	}
};

// Explicit Accessor for UObject* types (Common use case in UMG / CommonUI)
class FListViewUObjectAccess : public SListView<UObject*>
{
public:
	static const UObject* GetSelectorItem(const STableViewBase& List) 
	{ 
		return ((const FListViewUObjectAccess&)List).SelectorItem; 
	}
	
	static const TArray<UObject*>& GetItemsRef(const STableViewBase& List) 
	{ 
		const FListViewUObjectAccess* Access = static_cast<const FListViewUObjectAccess*>(&List);
		if (Access->HasValidItemsSource())
		{
			// Safe access to the data view assuming standard TArray layout or ObservableArray
			return *(const TArray<UObject*>*)(Access->GetItems().GetData()); 
		}
		static TArray<UObject*> Empty;
		return Empty;
	}

	static TSharedPtr<ITableRow> GetWidgetFromItem(const STableViewBase& List, const UObject* Item)
	{
		const FListViewUObjectAccess* Access = static_cast<const FListViewUObjectAccess*>(&List);
		return Access->WidgetFromItem(const_cast<UObject*>(Item));
	}

	// Robustly finds the index of the item corresponding to any widget in the focus path.
	// This fixes the issue where focus is on a row but the list's 'SelectorItem' is stale or null.
	static int32 GetIndexForPathWidget(const STableViewBase& List, const FWidgetPath& Path, int32 BoundaryIndex)
	{
		const FListViewUObjectAccess* Access = static_cast<const FListViewUObjectAccess*>(&List);
		const TArray<UObject*>& Items = GetItemsRef(List);

		// Iterate down the path from the list boundary (WidgetIndex) to the Leaf.
		// We are looking for the STableRow widget that holds the focused content.
		for (int32 i = BoundaryIndex + 1; i < Path.Widgets.Num(); ++i)
		{
			TSharedPtr<SWidget> PathWidget = Path.Widgets[i].Widget;
			
			// We check the ItemToWidgetMap. This map contains all *generated* (visible) rows.
			// This iteration is generally fast (only visible items).
			for (const auto& Pair : Access->WidgetGenerator.ItemToWidgetMap)
			{
				if (Pair.Value->AsWidget() == PathWidget)
				{
					return Items.Find(Pair.Key);
				}
			}
		}
		return INDEX_NONE;
	}
};

template <typename ItemType>
class FTileViewAccess : public STileView<ItemType>
{
public:
	static bool IsWrappingEnabled(const STableViewBase& View) 
	{ 
		return ((const FTileViewAccess<ItemType>&)View).bWrapHorizontalNavigation; 
	}
};

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
	// Reset indices and clear buffer content while maintaining size
	LogHistory.Empty();
	LogHistory.SetNum(MaxLogHistorySize);
	LogHistoryIndex = 0;
	bLogHistoryWrapped = false;
	LogVersion++; // Force view update
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
			// Ensure buffer size (robustness)
			if (LogHistory.Num() != MaxLogHistorySize)
			{
				LogHistory.SetNum(MaxLogHistorySize);
			}

			// Iterate Spy Buffer in chronological order
			int32 Start = bSpyWrapped ? SpyWriteIdx : 0;
			int32 Count = bSpyWrapped ? SpyBuffer.Num() : SpyWriteIdx;

			for (int32 i = 0; i < Count; ++i)
			{
				int32 ActualIdx = (Start + i) % SpyBuffer.Num();
				const FInputEventLog& RawLog = SpyBuffer[ActualIdx];

				// Merge Logic
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
					// Ring Buffer Add
					LogHistory[LogHistoryIndex] = MakeShared<FInputEventLog>(RawLog);
					LogHistoryIndex++;
					if (LogHistoryIndex >= MaxLogHistorySize)
					{
						LogHistoryIndex = 0;
						bLogHistoryWrapped = true;
					}
				}
				LogVersion++; // Update version
			}

			// Clear Spy
			InputSpy->ResetBuffer();
		}
	}

	// 2. Sync Overlay State from CVar
	bool bCVarState = CVarInputFlowOverlay.GetValueOnGameThread();
	if (bCVarState != bOverlayEnabled)
	{
		SetOverlayEnabled(bCVarState);
	}

	// Clean out stale focus history entries (3s retention)
	double Now = FPlatformTime::Seconds();
	FocusHistory.RemoveAll([Now](const FFocusHistoryEntry& Entry)
	{
		return !Entry.Widget.IsValid() || (Now - Entry.Timestamp) > 3.0;
	});

	// 3. Run Logic
	if (bOverlayEnabled)
	{
		TickNavigationSim(DeltaTime);
	}

	// Always update snapshot data (Active Leaf info)
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
				1000 // Z-Order
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

// --- Data Gathering for Overlay ---

void UInputDebugSubsystem::UpdateDataSnapshot()
{
	OverlayState = FInputOverlayState(); // Reset

	ULocalPlayer* LP = GetGameInstance()->GetFirstGamePlayer();
	if (!LP) return;

	// 1. Snapshot CommonUI Active Leaf & Config
	TSharedPtr<SWidget> FocusedSlateWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (FocusedSlateWidget.IsValid())
	{
		if (UCommonActivatableWidget* Active = UCommonUIActionRouterBase::FindActivatable(FocusedSlateWidget, LP))
		{
			OverlayState.ActiveCommonUILeaf = Active->GetName();

			FUIInputConfig Config = Active->GetDesiredInputConfig().Get(FUIInputConfig());

			// Input Mode String
			switch (Config.GetInputMode())
			{
			case ECommonInputMode::Game: OverlayState.InputConfig = TEXT("Game");
				break;
			case ECommonInputMode::Menu: OverlayState.InputConfig = TEXT("Menu");
				break;
			case ECommonInputMode::All: OverlayState.InputConfig = TEXT("All");
				break;
			default: OverlayState.InputConfig = TEXT("Default");
				break;
			}

			// Mouse Capture String
			switch (Config.GetMouseCaptureMode())
			{
			case EMouseCaptureMode::NoCapture: OverlayState.MouseCaptureMode = TEXT("No Capture");
				break;
			case EMouseCaptureMode::CapturePermanently: OverlayState.MouseCaptureMode = TEXT("Capture Perm");
				break;
			case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown: OverlayState.MouseCaptureMode = TEXT(
					"Capture Perm+");
				break;
			case EMouseCaptureMode::CaptureDuringMouseDown: OverlayState.MouseCaptureMode = TEXT("Capture Down");
				break;
			default: OverlayState.MouseCaptureMode = TEXT("Unknown");
				break;
			}
		}
	}

	// 2. Snapshot Bound Actions (With Key Names)
	if (UCommonUIActionRouterBase* Router = ULocalPlayer::GetSubsystem<UCommonUIActionRouterBase>(LP))
	{
		TArray<FUIActionBindingHandle> Bindings = Router->GatherActiveBindings();
		UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();

		for (const FUIActionBindingHandle& Handle : Bindings)
		{
			if (!Handle.IsValid()) continue;

			// Basic Display Name: "Confirm [A]"
			FString ActionName = Handle.GetActionName().ToString();
			FString DisplayName = Handle.GetDisplayName().ToString();
			FString Entry = ActionName;

			if (!DisplayName.IsEmpty() && DisplayName != ActionName)
			{
				Entry += FString::Printf(TEXT(" [%s]"), *DisplayName);
			}

			// --- Resolve Keys ---
			FString KeyString;
			TSharedPtr<FUIActionBinding> BindingPtr = FUIActionBinding::FindBinding(Handle);

			if (BindingPtr.IsValid())
			{
				// Case A: Enhanced Input (UE5 Standard)
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
				// Case B: Legacy CommonUI (Data Table)
				else if (const FCommonInputActionDataBase* LegacyData = CommonUI::GetInputActionData(
					BindingPtr->LegacyActionTableRow))
				{
					// Use Public API to retrieve keys safely

					// 1. Keyboard
					const FCommonInputTypeInfo& KbdInfo = LegacyData->GetInputTypeInfo(
						ECommonInputType::MouseAndKeyboard, NAME_None);
					FKey KbdKey = KbdInfo.GetKey();
					if (KbdKey.IsValid())
					{
						KeyString += KbdKey.GetDisplayName().ToString();
					}

					// 2. Gamepad (Default)
					const FCommonInputTypeInfo& GamepadInfo = LegacyData->GetInputTypeInfo(
						ECommonInputType::Gamepad, NAME_None);
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

	// Default Fallbacks
	if (OverlayState.ActiveCommonUILeaf.IsEmpty())
	{
		OverlayState.ActiveCommonUILeaf = TEXT("None (Viewport/PlayerController)");
		OverlayState.InputConfig = TEXT("Game");
		OverlayState.MouseCaptureMode = TEXT("Default");
	}

	// 3. Snapshot Triggered Enhanced Input Actions
	if (UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		if (UEnhancedPlayerInput* PlayerInput = EISub->GetPlayerInput())
		{
			// ... (Existing Accessor Hack for Context Data) ...
			struct FInputFlowDebugAccessor : public UEnhancedPlayerInput
			{
				static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& GetContextData(
					const UEnhancedPlayerInput* PInput)
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
// Navigation Spider
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
	const bool bDebugLog = CVarInputFlowNavSpiderDebugLog.GetValueOnGameThread();
	
	// Basic validation
	if (!Source.IsValid() || !Source->SupportsKeyboardFocus() || !bEnableNavigationSimulation)
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning,
		        TEXT("  [NavSim] Source is invalid or does not support keyboard focus."));
		return {nullptr, ENavSimResult::Normal};
	}

	FWidgetPath SourcePath;
	if (!FSlateApplication::Get().FindPathToWidget(Source.ToSharedRef(), SourcePath))
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [NavSim] Failed to find Widget Path for Source '%s'"),
		        *Source->ToString());
		return {nullptr, ENavSimResult::Normal};
	}

	const FNavigationEvent VirtualNavEvent(FModifierKeysState(), 999, Direction,
	                                       ENavigationGenesis::Controller);

	TSharedRef<SWindow> Window = SourcePath.GetDeepestWindow();
	const FSlateLayoutTransform WindowInverse = Window->GetWindowGeometryInScreen().GetAccumulatedLayoutTransform().Inverse();

	if (bDebugLog)
	{
		UE_LOG(LogInputFlow, Log, TEXT("--- Simulating %s from '%s' ---"),
		       *UEnum::GetValueAsString(Direction), *Source->ToString());
	}

	// Bubbling Loop
	for (int32 WidgetIndex = SourcePath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget& ArrangedBoundary = SourcePath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& BoundaryWidget = ArrangedBoundary.Widget;

		if (!BoundaryWidget->IsEnabled()) continue;

		// 1. Query the Widget's Navigation Rule
		FNavigationReply Reply = FNavigationReply::Escape();

#if UE_WITH_SLATE_SIMULATEDNAVIGATIONMETADATA
		// Handle UMG Designer metadata if present
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
		
		// Use CommonUI Button Logic if applicable
		if (Source->GetTypeAsString() == TEXT("ObjectTableRowT"))
		{
			TSharedPtr<SObjectTableRow<UObject*>> ObjectRow = StaticCastSharedPtr<SObjectTableRow<UObject*>>(Source);
			if (ObjectRow.IsValid())
			{
				const UCommonButtonBase* CommonButton = Cast<UCommonButtonBase>(ObjectRow->GetWidgetObject());
				if (IsValid(CommonButton))
				{
					const UButton* RootButton = Cast<UButton>(CommonButton->GetRootWidget());

					if (!IsValid(RootButton) && IsValid(CommonButton->WidgetTree))
					{
						CommonButton->WidgetTree->ForEachWidget([&](UWidget* Widget) {
							// We stop searching once we find the first valid internal button
							if (!IsValid(RootButton))
							{
								RootButton = Cast<UButton>(Widget);
							}
						});
					}
					if (IsValid(RootButton))
					{
						if (TSharedPtr<SButton> SlateCommonButton = FButtonAccess::GetSlateButton(RootButton))
						{
							return SimulateNavigation(StaticCastSharedPtr<SWidget>(SlateCommonButton), Direction, RealUserIndex);
						}
					}
				}
			}
		}

		EUINavigationRule Rule = Reply.GetBoundaryRule();

		// 2. Handle Explicit / Stop / Custom Rules
		if (Rule == EUINavigationRule::Explicit)
		{
			TSharedPtr<SWidget> ExplicitTarget = Reply.GetFocusRecipient();

			if (!ExplicitTarget.IsValid())
			{
				// FIX: Treat Explicit(nullptr) as Handled, NOT Escape.
				// SListView and STileView return Explicit(nullptr) when they handle navigation internally 
				// (e.g., moving selection from index 0 to 1). 
				// 
				// If we bubble this (Escape), the parent widget performs a Spatial Search and re-discovers 
				// the ListView itself (as seen in your logs: Found 'TileViewT'), causing the simulation 
				// to incorrectly predict focus returning to the container or failing to find the next item.
				UE_CLOG(bDebugLog, LogInputFlow, Log, 
					TEXT("  [Explicit-Null] Boundary '%s' handled navigation internally (e.g. ListView Selection). Stopping."),
					*BoundaryWidget->ToString());

				return {BoundaryWidget, ENavSimResult::Handled};
			}

			if (ExplicitTarget->IsEnabled() && ExplicitTarget->SupportsKeyboardFocus())
			{
				UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Explicit] Boundary '%s' -> Target '%s'"),
					*BoundaryWidget->ToString(), *ExplicitTarget->ToString());
				return {ExplicitTarget, ENavSimResult::Normal};
			}
		}
		
		if (Rule == EUINavigationRule::Stop)
		{
			UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Stop] Boundary '%s' hit Stop Rule."),
			        *BoundaryWidget->ToString());
			return {BoundaryWidget, ENavSimResult::Stopped};
		}
		
		if (Rule == EUINavigationRule::Custom)
		{
			UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Custom] Boundary '%s' Handled."),
			        *BoundaryWidget->ToString());
			return {BoundaryWidget, ENavSimResult::Handled};
		}

		// 3. Perform Spatial Search (Hittest Grid)
		TSharedPtr<SWidget> ResultWidget = nullptr;

		if (Direction == EUINavigation::Next || Direction == EUINavigation::Previous)
		{
			FWeakWidgetPath WeakSource(SourcePath);
			FWidgetPath NextPath = WeakSource.ToNextFocusedPath(Direction, Reply, ArrangedBoundary);
			if (NextPath.IsValid()) ResultWidget = NextPath.Widgets.Last().Widget;
		}
		else
		{
			FArrangedWidget WindowSpaceLeaf = SourcePath.Widgets.Last();
			WindowSpaceLeaf.Geometry.AppendTransform(WindowInverse);

			FArrangedWidget WindowSpaceBoundary = ArrangedBoundary;
			WindowSpaceBoundary.Geometry.AppendTransform(WindowInverse);

			FScopedSwitchWorldHack SwitchWorld(SourcePath);

			FNavigationReply GridRule = (Rule == EUINavigationRule::Wrap)
				                            ? FNavigationReply::Wrap()
				                            : FNavigationReply::Escape();

			ResultWidget = Window->GetHittestGrid().FindNextFocusableWidget(
				WindowSpaceLeaf,
				Direction,
				GridRule,
				WindowSpaceBoundary,
				RealUserIndex
			);

			// FIX 2: Filter out Ancestors
			// The HittestGrid often returns the Source's parent (e.g., the ListView container) 
			// because the parent effectively overlaps the child. 
			// We check if the ResultWidget is currently in the SourcePath. If it is, it's an ancestor
			// (or the widget itself), and we should ignore it to find a true "neighbor".
			if (ResultWidget.IsValid())
			{
				if (ResultWidget == Source || SourcePath.ContainsWidget(ResultWidget.Get()))
				{
					UE_CLOG(bDebugLog, LogInputFlow, Verbose, 
						TEXT("  [SpatialSearch] Found ancestor/self '%s'. Ignoring to prevent loop."), 
						*ResultWidget->ToString());
					ResultWidget.Reset();
				}
			}

			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [SpatialSearch] Boundary '%s' -> Target '%s'"),
			        *BoundaryWidget->ToString(),
			        ResultWidget.IsValid() ? *ResultWidget->ToString() : TEXT("NULL"));
		}

		// 4. Did we find a valid target?
		if (ResultWidget.IsValid() && InputFlowHelpers::IsGameWorldWidget(ResultWidget))
		{
			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Search] Boundary '%s' -> Found '%s'"),
			        *BoundaryWidget->ToString(), *ResultWidget->ToString());
			return {ResultWidget, ENavSimResult::Normal};
		}

		UE_CLOG(bDebugLog, LogInputFlow, Verbose, TEXT("  [Escape] Boundary '%s' bubbling up."),
		        *BoundaryWidget->ToString());
	}

	UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Result] Bubbled to Root. No Target."));
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

				bool bIsSameWidget = (Next == CurrentWidget);
				bool bIsInternal = false;

				if (!bIsSameWidget && ResultType == ENavSimResult::Normal)
				{
					FWidgetPath PathToNext;
					FSlateApplication::Get().GeneratePathToWidgetUnchecked(Next.ToSharedRef(), PathToNext);

					if (PathToNext.ContainsWidget(CurrentWidget.Get()))
					{
						bIsInternal = true; 
					}
					else
					{
						FWidgetPath PathToCurrent;
						FSlateApplication::Get().GeneratePathToWidgetUnchecked(
							CurrentWidget.ToSharedRef(), PathToCurrent);
						if (PathToCurrent.ContainsWidget(Next.Get()))
						{
							bIsInternal = true; 
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

// ----------------------------------------------------------------------------------
// Setters/Getters
// ----------------------------------------------------------------------------------

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