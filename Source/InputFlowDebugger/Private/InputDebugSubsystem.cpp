// Copyright Mike Desrosiers, All Rights Reserved.

// InputFlowDebugger
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "LogInputFlow.h"
#include "SInputFlowOverlay.h"

// Core
#include <Containers/Ticker.h>

// Engine
#include <Engine/GameInstance.h>
#include <Engine/GameViewportClient.h>
#include "InputFlowSettings.h"

// EnhancedInput
#if WITH_PLUGIN_ENHANCEDINPUT
#include <EnhancedInputSubsystems.h>
#include <EnhancedPlayerInput.h>
#include <InputMappingContext.h>
#include <InputAction.h>
#endif // WITH_PLUGIN_ENHANCEDINPUT

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Widgets/Views/STableViewBase.h>
#include <Slate/SObjectTableRow.h>

// SlateCore
#include <Input/HittestGrid.h>
#include <Widgets/SWindow.h>
#include <Widgets/SWeakWidget.h>

// UMG
#include <Blueprint/WidgetTree.h>
#include <Components/Button.h>


// CommonUI
#if WITH_PLUGIN_COMMONUI
#include <CommonButtonBase.h>
#include <CommonInputSubsystem.h>
#include <CommonActivatableWidget.h>
#include <CommonUITypes.h>
#include <Input/CommonUIActionRouterBase.h>
#include <Input/UIActionBinding.h>
#endif // WITH_PLUGIN_COMMONUI

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

#if WITH_PLUGIN_ENHANCEDINPUT
class FInputFlowDebugAccessor : public UEnhancedPlayerInput
{
public:
	static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& GetContextData(
		const UEnhancedPlayerInput* PInput)
	{
		return static_cast<const FInputFlowDebugAccessor*>(PInput)->GetAppliedInputContextData();
	}
};
#endif // WITH_PLUGIN_ENHANCEDINPUT

// ----------------------------------------------------------------------------------
// UInputDebugSubsystem Implementation
// ----------------------------------------------------------------------------------

void UInputDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GetMutableDefault<UInputFlowSettings>()->GetOnSettingsChanged().AddUObject(this, &UInputDebugSubsystem::HandleSettingsChanged);
	HandleSettingsChanged();

	// Pre-allocate ring buffer
	LogHistory.SetNum(MaxLogHistorySize);
	LogHistoryIndex = 0;
	bLogHistoryWrapped = false;

	InputSpy = MakeShared<FInputFlowSpy>();
	if (ensure(FSlateApplication::IsInitialized()))
	{
		FSlateApplication::Get().RegisterInputPreProcessor(InputSpy, EInputPreProcessorType::Overlay);
		InputSpy->OnFocusChanged().AddUObject(this, &UInputDebugSubsystem::OnSpyFocusChanged);
	}

	LogSyncTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UInputDebugSubsystem::TickSyncLogs),
		0.1f
	);
}

void UInputDebugSubsystem::Deinitialize()
{
	if (UInputFlowSettings* Settings = GetMutableDefault<UInputFlowSettings>())
	{
		Settings->GetOnSettingsChanged().RemoveAll(this);
	}

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

	// Destroy overlay
	if (OverlayHost.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
	{
		GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(OverlayHost.ToSharedRef());
	}
	OverlayHost.Reset();
	OverlayWidget.Reset();
	bOverlayActive = false;

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

void UInputDebugSubsystem::HandleSettingsChanged()
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	
	// Handle Overlay Widget Spawning/Despawning
	if (Settings->IsOverlayEnabled() != bOverlayActive)
	{
		bOverlayActive = Settings->IsOverlayEnabled();
		
		if (bOverlayActive)
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
}

bool UInputDebugSubsystem::TickSyncLogs(float DeltaTime)
{
	if (InputSpy.IsValid())
	{
		const TArray<FInputEventLog>& SpyBuffer = InputSpy->GetEventLogBuffer();
		const int32 SpyWriteIdx = InputSpy->GetWriteIndex();
		const bool bSpyWrapped = InputSpy->IsWrapped();

		if (SpyWriteIdx > 0 || bSpyWrapped)
		{
			// Ensure buffer size (robustness)
			if (LogHistory.Num() != MaxLogHistorySize)
			{
				LogHistory.SetNum(MaxLogHistorySize);
			}

			// Iterate Spy Buffer in chronological order
			const int32 Start = bSpyWrapped ? SpyWriteIdx : 0;
			const int32 Count = bSpyWrapped ? SpyBuffer.Num() : SpyWriteIdx;

			for (int32 i = 0; i < Count; ++i)
			{
				const int32 ActualIdx = (Start + i) % SpyBuffer.Num();
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

	// Clean out stale focus history entries (3s retention)
	double Now = FPlatformTime::Seconds();
	FocusHistory.RemoveAll([Now](const FFocusHistoryEntry& Entry)
	{
		return !Entry.Widget.IsValid() || (Now - Entry.Timestamp) > 3.0;
	});
	
	TickNavigationSim(DeltaTime);

	// Always update snapshot data (Active Leaf info)
	UpdateDataSnapshot();

	return true;
}

// --- Data Gathering for Overlay ---

void UInputDebugSubsystem::UpdateDataSnapshot()
{
	OverlayState = FInputOverlayState(); // Reset

	ULocalPlayer* LP = GetGameInstance()->GetFirstGamePlayer();
	if (!LP) return;

	// Snapshot CommonUI Active Leaf & Config
	TSharedPtr<SWidget> FocusedSlateWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
#if WITH_PLUGIN_COMMONUI
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

	// Snapshot Bound Actions (With Key Names)
	if (UCommonUIActionRouterBase* Router = ULocalPlayer::GetSubsystem<UCommonUIActionRouterBase>(LP))
	{
		TArray<FUIActionBindingHandle> Bindings = Router->GatherActiveBindings();
#if WITH_PLUGIN_ENHANCEDINPUT
		UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
#endif

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
				// Case A: Enhanced Input 
#if WITH_PLUGIN_ENHANCEDINPUT
				if (const UInputAction* InputAction = BindingPtr->InputAction.Get())
				{
					if (EISub)
					{
						KeyString += TEXT("EInput: ");
						TArray<FKey> Keys = EISub->QueryKeysMappedToAction(InputAction);
						for (const FKey& Key : Keys)
						{
							if (!KeyString.IsEmpty()) KeyString += TEXT(", ");
							KeyString += Key.GetDisplayName().ToString();
						}
					}
				}
#endif // WITH_PLUGIN_ENHANCEDINPUT
				// Case B: Legacy CommonUI (Data Table)
				else if (const FCommonInputActionDataBase* LegacyData = CommonUI::GetInputActionData(
					BindingPtr->LegacyActionTableRow))
				{
					KeyString += TEXT("CommonUI: ");

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
#endif // WITH_PLUGIN_COMMONUI

	// Default Fallbacks
	if (OverlayState.ActiveCommonUILeaf.IsEmpty())
	{
		OverlayState.ActiveCommonUILeaf = TEXT("None (Viewport/PlayerController)");
		OverlayState.InputConfig = TEXT("Game");
		OverlayState.MouseCaptureMode = TEXT("Default");
	}

	// Snapshot Triggered Enhanced Input Actions
#if WITH_PLUGIN_ENHANCEDINPUT
	if (UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		if (UEnhancedPlayerInput* PlayerInput = EISub->GetPlayerInput())
		{
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
#endif // WITH_PLUGIN_ENHANCEDINPUT
}

// ----------------------------------------------------------------------------------
// Navigation Spider
// ----------------------------------------------------------------------------------

void UInputDebugSubsystem::TickNavigationSim(float DeltaTime)
{
	if (!bOverlayActive || !UInputFlowSettings::Get()->IsNavSimulationEnabled()) return;
	if (!FSlateApplication::IsInitialized()) return;

	const TSharedPtr<SWidget> CurrentFocus = FocusedWidget.Pin();

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
	const TSharedPtr<SWidget>& Source, EUINavigation Direction, int32 UserIndex) const
{
	const bool bDebugLog = CVarInputFlowNavSpiderDebugLog.GetValueOnGameThread();
	const bool bEnableNavigationSimulation = UInputFlowSettings::Get()->IsNavSimulationEnabled();
	
	// Basic validation
	if (!Source.IsValid() || !Source->SupportsKeyboardFocus() || !bEnableNavigationSimulation)
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [NavSim] Source is invalid or does not support keyboard focus."));
		return {nullptr, ENavSimResult::Normal};
	}

	FWidgetPath SourcePath;
	const bool bPathFound = FSlateApplication::Get().FindPathToWidget(Source.ToSharedRef(), SourcePath);
	if (!bPathFound)
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [NavSim] Could not find WidgetPath for source widget."));
		return {nullptr, ENavSimResult::Normal};
	}

	const FNavigationEvent VirtualNavEvent(FModifierKeysState(), 999, Direction, ENavigationGenesis::Controller);

	TSharedRef<SWindow> Window = SourcePath.GetDeepestWindow();
	const FSlateLayoutTransform WindowInverse = Window->GetWindowGeometryInScreen().GetAccumulatedLayoutTransform().Inverse();
	
	UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("--- Simulating %s from '%s' ---"), *UEnum::GetValueAsString(Direction), *Source->ToString());

	// -----------------------------------------------------------------------------------
	// Identify TableView Context
	// If the widget is inside a ListView/TileView, we want to defer to the container's logic 
	// (which we assume is "Handled") rather than performing spatial searches from row children.
	// These containers often manage focus internally (index changes) rather than passing focus outwards.
	// Replicating that behavior here doesn't work well, so we just stop at the container boundary.
	// -----------------------------------------------------------------------------------
	int32 DeepestTableViewIndex = INDEX_NONE;
	for (int32 i = 0; i < SourcePath.Widgets.Num(); ++i)
	{
		if (InputFlowHelpers::IsTableViewWidget(SourcePath.Widgets[i].Widget))
		{
			DeepestTableViewIndex = i;
		}
	}

	// Bubbling Loop
	for (int32 WidgetIndex = SourcePath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget& ArrangedBoundary = SourcePath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& BoundaryWidget = ArrangedBoundary.Widget;

		if (!BoundaryWidget->IsEnabled()) continue;
		
		FNavigationReply Reply = FNavigationReply::Escape();

		// Calling OnNavigation on ListView/TileView widgets can cause side effects (focus/selection changes).
		// We avoid calling it and assume they handle navigation internally or bubble.
		if (InputFlowHelpers::IsTableViewWidget(BoundaryWidget))
		{
			// Treat as Handled to stop simulation at the list boundary.
			// This prevents side-effects and assumes the list handles internal nav (index changes).
			UE_CLOG(bDebugLog, LogInputFlow, Log, 
				TEXT("  [TableView] Boundary '%s' is a List/TileView. Treating as Handled/DeadEnd to avoid side effects."),
				*BoundaryWidget->ToString());
			
			return {ConstCastSharedRef<SWidget>(BoundaryWidget), ENavSimResult::Handled};
		}
		else
		{
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
				// Query the widget's navigation
				Reply = BoundaryWidget->OnNavigation(ArrangedBoundary.Geometry, VirtualNavEvent);
			}
		}

		EUINavigationRule Rule = Reply.GetBoundaryRule();

		if (Rule == EUINavigationRule::Explicit)
		{
			TSharedPtr<SWidget> ExplicitTarget = Reply.GetFocusRecipient();

			if (!ExplicitTarget.IsValid())
			{
				// SListView and STileView return Explicit(nullptr) when they handle navigation internally 
				// The simulation should stop here, indicating the container handled the input.
				UE_CLOG(bDebugLog, LogInputFlow, Log, 
					TEXT("  [Explicit-Null] Boundary '%s' returned Explicit(Null). Navigation Handled internally."),
					*BoundaryWidget->ToString());

				return {ConstCastSharedRef<SWidget>(BoundaryWidget), ENavSimResult::Handled};
			}

			if (ExplicitTarget->IsEnabled() && ExplicitTarget->SupportsKeyboardFocus())
			{
				UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Explicit] Boundary '%s' -> Target '%s'"),
					*BoundaryWidget->ToString(), *ExplicitTarget->ToString());
				return {ExplicitTarget, ENavSimResult::Explicit};
			}
		}
		
		if (Rule == EUINavigationRule::Stop)
		{
			UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Stop] Boundary '%s' hit Stop Rule."),
			        *BoundaryWidget->ToString());
			return {ConstCastSharedRef<SWidget>(BoundaryWidget), ENavSimResult::Stopped};
		}
		
		if (Rule == EUINavigationRule::Custom)
		{
			UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Custom] Boundary '%s' Handled."),
			        *BoundaryWidget->ToString());
			return {ConstCastSharedRef<SWidget>(BoundaryWidget), ENavSimResult::Handled};
		}

		// -----------------------------------------------------------------------------------
		// Spatial Search Guard
		// -----------------------------------------------------------------------------------
		// If we are inside a ListView/TileView, we skip the spatial search on children.
		// We bubble up until we hit the container, which will return Handled (dead end).
		// This prevents "leaking" navigation from rows to neighbors when the list itself 
		// should manage the transition (or consume it).
		if (DeepestTableViewIndex != INDEX_NONE && WidgetIndex > DeepestTableViewIndex)
		{
			UE_CLOG(bDebugLog, LogInputFlow, Verbose, 
				TEXT("  [Skip-Spatial] Widget '%s' is inside a TableView. Bubbling to container."), 
				*BoundaryWidget->ToString());
			continue;
		}

		// Prepare for Spatial Search (Hittest Grid)
		// If the widget returned Escape (default), we perform a spatial search from this boundary.
		// This works for most non-virtualized widgets within a window.
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

			// -----------------------------------------------------------------------
			// Step-Over Logic
			// If we find a non-game widget (debugger overlay), we try to "step over" it
			// by using that widget as the new start point for the search.
			// Otherwise, the simulation incorrectly lands on the overlay widget.
			// -----------------------------------------------------------------------
			constexpr int32 MaxStepOver = 16;
			FArrangedWidget SearchStartBoundary = WindowSpaceBoundary;

			for (int32 StepOverCount = 0; StepOverCount <= MaxStepOver; ++StepOverCount)
			{
				ResultWidget = Window->GetHittestGrid().FindNextFocusableWidget(
					WindowSpaceLeaf,
					Direction,
					GridRule,
					SearchStartBoundary,
					UserIndex
				);

				// Ancestor/Self Filter
				if (ResultWidget.IsValid())
				{
					if (ResultWidget == Source || SourcePath.ContainsWidget(ResultWidget.Get()))
					{
						UE_CLOG(bDebugLog, LogInputFlow, Verbose, 
							TEXT("  [SpatialSearch] Found ancestor/self '%s'. Ignoring to continue bubbling."), 
							*ResultWidget->ToString());
						ResultWidget.Reset();
						break; // Stop stepping, normal behavior (bubble up)
					}
				}

				// Non-Game World Filter (Step Over)
				if (ResultWidget.IsValid() && !InputFlowHelpers::IsGameWorldWidget(ResultWidget))
				{
					if (StepOverCount < MaxStepOver)
					{
						UE_CLOG(bDebugLog, LogInputFlow, Verbose, 
							TEXT("  [StepOver] Found non-game widget '%s'. Attempting to step over..."), 
							*ResultWidget->ToString());

						FWidgetPath ObstaclePath;
						// Find the geometry of the obstacle to search from it
						FSlateApplication::Get().FindPathToWidget(ResultWidget.ToSharedRef(), ObstaclePath);
						
						if (ObstaclePath.IsValid())
						{
							SearchStartBoundary = ObstaclePath.Widgets.Last();
							SearchStartBoundary.Geometry.AppendTransform(WindowInverse);
							continue; // Loop again from obstacle
						}
					}
					// If failed to find path or max steps reached, we accept the result 
					// (and likely bubble up due to GameWorld check later)
				}
				
				break; // Valid result or null
			}

			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [SpatialSearch] Boundary '%s' -> Target '%s'"),
			        *BoundaryWidget->ToString(),
			        ResultWidget.IsValid() ? *ResultWidget->ToString() : TEXT("NULL"));
		}

		// 5. Did we find a valid target?
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
	const int32 MaxDepth = UInputFlowSettings::Get()->GetNavigationSearchDepth();

	while (SimulationQueue.Num() > 0)
	{
		if ((FPlatformTime::Seconds() - StartTime) > MaxSimulationTimePerFrame) return;

		FSimQueueItem CurrentItem = SimulationQueue[0];
		SimulationQueue.RemoveAt(0);

		TSharedPtr<SWidget> CurrentWidget = CurrentItem.Widget.Pin();
		const int32 CurrentDepth = CurrentItem.Depth;

		if (!CurrentWidget.IsValid() || CurrentDepth >= MaxDepth) continue;

		constexpr EUINavigation Directions[] = {
			EUINavigation::Up, EUINavigation::Down, EUINavigation::Left, EUINavigation::Right
		};
		const int32 RealUserIndex = FSlateApplication::Get().GetUserIndexForKeyboard();

		for (const EUINavigation Dir : Directions)
		{
			TPair<TSharedPtr<SWidget>, ENavSimResult> SimResult = SimulateNavigation(CurrentWidget, Dir, RealUserIndex);
			TSharedPtr<SWidget> Next = SimResult.Key;
			const ENavSimResult ResultType = SimResult.Value;

			if (Next.IsValid() && InputFlowHelpers::IsGameWorldWidget(Next))
			{bool bIsSameWidget = (Next == CurrentWidget);
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

				const bool bIsTerminal = (ResultType == ENavSimResult::Handled || ResultType == ENavSimResult::Stopped);
				const bool bIsVisited = VisitedWidgets.Contains(Next);
				const bool bIsExplicit = (ResultType == ENavSimResult::Explicit);
				const bool bShouldRecord = bIsTerminal || bIsExplicit || !bIsVisited;

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