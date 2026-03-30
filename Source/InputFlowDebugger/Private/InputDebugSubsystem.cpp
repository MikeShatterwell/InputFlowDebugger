// Copyright Mike Desrosiers, All Rights Reserved.

// InputFlowDebugger
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"
#include "LogInputFlow.h"
#include "SInputFlowOverlay.h"
#include "InputFlowSettings.h"

// Core
#include <Containers/Ticker.h>
#include <UObject/UObjectIterator.h>

// Engine
#include <Engine/GameInstance.h>
#include <Engine/GameViewportClient.h>

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
	false,
	TEXT("Toggles the Input Flow Debugger in-game overlay"),
	ECVF_Default
);

static TAutoConsoleVariable<bool> CVarInputFlowNavSpiderDebugLog(
	TEXT("InputFlow.NavSpider.DebugLogEnabled"),
	false,
	TEXT("Toggles logging nav sim events to the Output Log"),
	ECVF_Default
);

static TAutoConsoleVariable<float> CVarInputFlowOverlayScale(
	TEXT("InputFlow.OverlayScale"),
	1.0f,
	TEXT("Scales the Input Flow Debugger overlay UI (0.5 to 3.0)"),
	ECVF_Default
);

// ----------------------------------------------------------------------------------
// UInputDebugSubsystem Implementation
// ----------------------------------------------------------------------------------

void UInputDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GetMutableDefault<UInputFlowSettings>()->GetOnSettingsChanged().AddUObject(
		this, &UInputDebugSubsystem::HandleSettingsChanged);
	HandleSettingsChanged();

	// Pre-allocate ring buffer
	LogHistory.SetNum(MaxLogHistorySize);
	LogHistoryIndex = 0;
	bLogHistoryWrapped = false;

	InputSpy = MakeShared<FInputFlowSpy>();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(InputSpy, EInputPreProcessorType::Overlay);
		InputSpy->OnFocusChanged().AddUObject(this, &UInputDebugSubsystem::OnSpyFocusChanged);
	}

	LogSyncTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UInputDebugSubsystem::TickSyncLogs),
		0.1f
	);

	CVarInputFlowOverlay.AsVariable()->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateWeakLambda(this, [this](IConsoleVariable* Var)
												   {
													   const bool bEnabled = Var->GetBool();
													   UInputFlowSettings* Settings = GetMutableDefault<
														   UInputFlowSettings>();
													   if (Settings->IsOverlayEnabled() != bEnabled)
													   {
														   Settings->SetEnableOverlay(bEnabled);
													   }
												   }
		));
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
	if (bOverlayActive)
	{
		bOverlayActive = false;
		OnInputCaptureChanged.Broadcast(false);
	}

	Super::Deinitialize();
}

bool UInputDebugSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_SERVER
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer);
#endif // UE_SERVER
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
	if (CVarInputFlowOverlayScale.AsVariable() && CVarInputFlowOverlayScale.AsVariable()->GetFloat() != Settings->
		GetOverlayScale())
	{
		CVarInputFlowOverlayScale.AsVariable()->Set(Settings->GetOverlayScale(), ECVF_SetByCode);
	}

	if (CVarInputFlowOverlayScale.AsVariable() && CVarInputFlowOverlayScale.AsVariable()->GetFloat() != Settings->
		GetOverlayScale())
	{
		CVarInputFlowOverlayScale.AsVariable()->Set(Settings->GetOverlayScale(), ECVF_SetByCode);
	}

	UGameViewportClient* GameViewport = (GetWorld()) ? GetWorld()->GetGameViewport() : nullptr;
	const bool bIsViewportReady = IsValid(GameViewport);

	// Determine desired state
	const bool bShouldBeActive = Settings->IsOverlayEnabled() && bIsViewportReady;

	// Handle Overlay Widget Spawning/Despawning
	// It's a debug tool, we can get away with some lazy init/destruct here.
	if (bShouldBeActive != bOverlayActive)
	{
		bOverlayActive = bShouldBeActive;
		OnInputCaptureChanged.Broadcast(bOverlayActive);

		if (bOverlayActive)
		{
			// Create the widget if it doesn't exist
			if (!OverlayWidget.IsValid())
			{
				OverlayWidget = SNew(SInputFlowOverlay).Subsystem(this);
			}

			// Wrap in WeakWidget if not already done
			if (!OverlayHost.IsValid())
			{
				SAssignNew(OverlayHost, SWeakWidget)
				.PossiblyNullContent(OverlayWidget.ToSharedRef());
			}

			// Add to Viewport
			GameViewport->AddViewportWidgetContent(
				OverlayHost.ToSharedRef(),
				INT_MAX // Z-Order
			);
		}
		else
		{
			// Remove from Viewport
			if (OverlayHost.IsValid() && GameViewport)
			{
				GameViewport->RemoveViewportWidgetContent(OverlayHost.ToSharedRef());
			}

			OverlayHost.Reset();
			OverlayWidget.Reset();
		}
	}

	if (Settings->IsNavSimulationEnabled() && bOverlayActive)
	{
		StartNewSimulation(FocusedWidget.Pin(), /*bStartFromScratch*/ true);
	}
}

bool UInputDebugSubsystem::TickSyncLogs(float DeltaTime)
{
	// If settings say "On", but we aren't active (likely due to missing Viewport on Init), try again.
	if (UInputFlowSettings::Get()->IsOverlayEnabled() != bOverlayActive)
	{
		HandleSettingsChanged();
	}

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
					const uint32 PrevHistIdx = (LogHistoryIndex == 0) ? MaxLogHistorySize - 1 : LogHistoryIndex - 1;
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
	DataSnapshot = FInputSnapshotStrings();

	UGameInstance* GI = GetGameInstance();
	if (!IsValid(GI)) return;

	ULocalPlayer* LP = GI->GetFirstGamePlayer();
	if (!IsValid(LP)) return;

#if WITH_PLUGIN_COMMONUI
	UCommonUIActionRouterBase* Router = LP->GetSubsystem<UCommonUIActionRouterBase>();
	if (!IsValid(Router)) return;

	// Snapshot Input Mode & Mouse Capture from Router
	const ECommonInputMode ActiveMode = Router->GetActiveInputMode();
	const EMouseCaptureMode ActiveCapture = Router->GetActiveMouseCaptureMode();

	switch (ActiveMode)
	{
	case ECommonInputMode::Game: DataSnapshot.InputConfig = TEXT("Game");
		break;
	case ECommonInputMode::Menu: DataSnapshot.InputConfig = TEXT("Menu");
		break;
	case ECommonInputMode::All: DataSnapshot.InputConfig = TEXT("All");
		break;
	default: DataSnapshot.InputConfig = TEXT("Default");
		break;
	}

	switch (ActiveCapture)
	{
	case EMouseCaptureMode::NoCapture:
		DataSnapshot.MouseCaptureMode = TEXT("No Capture");
		break;
	case EMouseCaptureMode::CapturePermanently:
		DataSnapshot.MouseCaptureMode = TEXT("Capture Perm");
		break;
	case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown:
		DataSnapshot.MouseCaptureMode = TEXT("Capture Perm+");
		break;
	case EMouseCaptureMode::CaptureDuringMouseDown:
		DataSnapshot.MouseCaptureMode = TEXT("Capture Down");
		break;
	case EMouseCaptureMode::CaptureDuringRightMouseDown:
		DataSnapshot.MouseCaptureMode = TEXT("Capture RMB");
		break;
	default:
		DataSnapshot.MouseCaptureMode = TEXT("Unknown");
		break;
	}

	// ==========================================================================
	// Find Leafmost Active Widget
	// ==========================================================================
	// Strategy:
	// 1. Gather all activated widgets that are in the active root and support focus
	// 2. Build parent relationships between them
	// 3. Find widgets with no active descendants (leaf candidates)
	// 4. Use paint layer as tiebreaker if multiple leaves exist

	TArray<UCommonActivatableWidget*> ActiveInRoot;
	TMap<UCommonActivatableWidget*, UCommonActivatableWidget*> ParentMap;

	for (TObjectIterator<UCommonActivatableWidget> It; It; ++It)
	{
		UCommonActivatableWidget* Widget = *It;
		if (!IsValid(Widget)) continue;
		if (Widget->GetWorld() != LP->GetWorld()) continue;
		if (!Widget->IsActivated()) continue;
		if (!Widget->SupportsActivationFocus()) continue;
		if (!Router->IsWidgetInActiveRoot(Widget)) continue;

		ActiveInRoot.Add(Widget);

		// Find parent activatable widget
		TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
		if (CachedWidget.IsValid())
		{
			UCommonActivatableWidget* Parent = UCommonUIActionRouterBase::FindOwningActivatable(CachedWidget, LP);
			if (IsValid(Parent) && Parent != Widget)
			{
				ParentMap.Add(Widget, Parent);
			}
		}
	}

	// Identify widgets that have active children (and thus cannot be the leafmost)
	TSet<UCommonActivatableWidget*> HasActiveChild;
	for (const auto& Pair : ParentMap)
	{
		UCommonActivatableWidget* Parent = Pair.Value;
		// Only count if the parent is also in our active set
		if (ActiveInRoot.Contains(Parent))
		{
			HasActiveChild.Add(Parent);
		}
	}

	// Find leaf candidates (no active descendants) and pick the one with highest paint layer
	UCommonActivatableWidget* LeafmostCandidate = nullptr;
	int32 HighestPaintLayer = TNumericLimits<int32>::Min();

	for (UCommonActivatableWidget* Widget : ActiveInRoot)
	{
		// Skip if this widget has active children
		if (HasActiveChild.Contains(Widget)) continue;

		TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
		if (!CachedWidget.IsValid()) continue;

		// Use paint layer as tiebreaker when multiple leaves exist
		// (e.g., sibling widgets at the same depth)
		const int32 PaintLayer = CachedWidget->GetPersistentState().LayerId;

		if (!LeafmostCandidate || PaintLayer > HighestPaintLayer)
		{
			LeafmostCandidate = Widget;
			HighestPaintLayer = PaintLayer;
		}
	}

	DataSnapshot.ActiveCommonUILeaf = LeafmostCandidate
										  ? LeafmostCandidate->GetName()
										  : TEXT("None (Viewport/PlayerController)");

	// ==========================================================================
	// Snapshot Bound Actions
	// ==========================================================================
	TArray<FUIActionBindingHandle> Bindings = Router->GatherActiveBindings();

#if WITH_PLUGIN_ENHANCEDINPUT
	UEnhancedInputLocalPlayerSubsystem* EISub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
#endif

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

		// Resolve bound keys
		FString KeyString;
		TSharedPtr<FUIActionBinding> BindingPtr = FUIActionBinding::FindBinding(Handle);

		if (BindingPtr.IsValid())
		{
#if WITH_PLUGIN_ENHANCEDINPUT
			if (const UInputAction* InputAction = BindingPtr->InputAction.Get())
			{
				if (IsValid(EISub))
				{
					TArray<FKey> Keys = EISub->QueryKeysMappedToAction(InputAction);
					TArray<FString> KeyNames;
					for (const FKey& Key : Keys)
					{
						KeyNames.Add(Key.GetDisplayName().ToString());
					}
					if (KeyNames.Num() > 0)
					{
						KeyString = TEXT("EInput: ") + FString::Join(KeyNames, TEXT(", "));
					}
				}
			}
			else
#endif
				if (const FCommonInputActionDataBase* LegacyData = CommonUI::GetInputActionData(
					BindingPtr->LegacyActionTableRow))
				{
					TArray<FString> KeyNames;

					const FCommonInputTypeInfo& KbdInfo = LegacyData->GetInputTypeInfo(
						ECommonInputType::MouseAndKeyboard, NAME_None);
					if (KbdInfo.GetKey().IsValid())
					{
						KeyNames.Add(KbdInfo.GetKey().GetDisplayName().ToString());
					}

					const FCommonInputTypeInfo& GamepadInfo = LegacyData->GetInputTypeInfo(
						ECommonInputType::Gamepad, NAME_None);
					if (GamepadInfo.GetKey().IsValid())
					{
						KeyNames.Add(GamepadInfo.GetKey().GetDisplayName().ToString());
					}

					if (KeyNames.Num() > 0)
					{
						KeyString = TEXT("CommonUI: ") + FString::Join(KeyNames, TEXT(", "));
					}
				}
		}

		if (!KeyString.IsEmpty())
		{
			Entry += FString::Printf(TEXT(" (%s)"), *KeyString);
		}

		DataSnapshot.BoundActions.AddUnique(Entry);
	}

#endif // WITH_PLUGIN_COMMONUI

	// ==========================================================================
	// Snapshot Active Enhanced Input Actions
	// ==========================================================================
#if WITH_PLUGIN_ENHANCEDINPUT

	if (UEnhancedPlayerInput* PlayerInput = EISub->GetPlayerInput())
	{
		const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& ContextMap =
			InputFlowHelpers::GetInputContextData(PlayerInput);

		for (const auto& Pair : ContextMap)
		{
			const UInputMappingContext* IMC = Pair.Key;
			if (!IsValid(IMC)) continue;

			for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
			{
				const UInputAction* Action = Mapping.Action;
				if (!IsValid(Action)) continue;

				const FInputActionInstance* Data = PlayerInput->FindActionInstanceData(Action);
				if (!Data) continue;

				const ETriggerEvent Trigger = Data->GetTriggerEvent();
				if (Trigger == ETriggerEvent::Triggered || Trigger == ETriggerEvent::Ongoing)
				{
					const FString Val = PlayerInput->GetActionValue(Action).ToString();
					FString Entry = FString::Printf(TEXT("%s: %s"), *Action->GetName(), *Val);
					DataSnapshot.ActiveEnhancedInputActions.AddUnique(Entry);
				}
			}
		}
	}

#endif

	// Fallback defaults
	if (DataSnapshot.InputConfig.IsEmpty())
	{
		DataSnapshot.InputConfig = TEXT("Game");
	}
	if (DataSnapshot.MouseCaptureMode.IsEmpty())
	{
		DataSnapshot.MouseCaptureMode = TEXT("Default");
	}
}

// ----------------------------------------------------------------------------------
// Navigation Spider
// ----------------------------------------------------------------------------------

void UInputDebugSubsystem::TickNavigationSim(float DeltaTime)
{
	if (!bOverlayActive || !UInputFlowSettings::Get()->IsNavSimulationEnabled()) return;
	if (!FSlateApplication::IsInitialized()) return;

	const TSharedPtr<SWidget> CurrentFocus = FocusedWidget.Pin();
	const double Now = FPlatformTime::Seconds();

	const bool bFocusChanged = (CurrentFocus != LastSimulationStartWidget.Pin());

	// If focus changed, interrupt and rebuild from scratch immediately.
	if (bFocusChanged)
	{
		StartNewSimulation(CurrentFocus, /*bStartFromScratch*/ true);
	}
	// If idle (not currently simulating) and 0.5s has passed since the LAST simulation finished, start a background poll.
	else if (!bSimulationInProgress && (Now - LastSimulationStartTime > UInputFlowSettings::Get()->GetNavigationSimPollInterval()))
	{
		StartNewSimulation(CurrentFocus, /*bStartFromScratch*/ false);
	}

	// Continue processing the time-sliced queue if active
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
}

/*
 * For edge case reasons and reasons known and unknown, we must call the private FNavigationReply::SetHandler function
 * when simulating navigation to properly set up the reply for UE's internal navigation handling.
 *
 * If FNavigationReply::SetHandler is ever made public, we can remove this hack and call it directly.
 * If the signature of FNavigationReply::SetHandler changes, this will break and need to be updated.
 * 
 * Until then, enjoy this cowabunga template friend function hack to gain access to the private member.
 */
namespace UInputDebugSubsystem_SimulateNavigation_PrivateAccessHack
{
	// Define the signature of the private SetHandler function
	using SetHandlerFn = FNavigationReply& (FNavigationReply::*)(const TSharedRef<SWidget>&);

	// Forward declare the friend function we will inject
	SetHandlerFn GetPrivateSetHandler();

	// Define the Thief template
	template <SetHandlerFn Ptr>
	struct FSetHandlerThief
	{
		// Friend definition inside the template captures the private pointer 'Ptr'
		friend SetHandlerFn GetPrivateSetHandler()
		{
			return Ptr;
		}
	};

	// Explicitly instantiate the template with the private member
	template struct FSetHandlerThief<
	  static_cast<SetHandlerFn>(&FNavigationReply::SetHandler)
	>;

	// Safe Wrapper
	void ForceSetHandler(FNavigationReply& Reply, const TSharedRef<SWidget>& Handler)
	{
		(Reply.*GetPrivateSetHandler())(Handler); // Invoke the private SetHandler function via the retrieved pointer
	}
}

FSimNavStepResult UInputDebugSubsystem::SimulateNavigation(
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

	UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("--- Simulating %s from '%s' ---"), *UEnum::GetValueAsString(Direction), *Source->ToString());

	// Bubble up the widget path to find the effective boundary rule and boundary widget.
	FNavigationReply BoundaryReply = FNavigationReply::Escape();
	FArrangedWidget BoundaryWidget = SourcePath.Widgets.Last();

	for (int32 WidgetIndex = SourcePath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget& ArrangedWidget = SourcePath.Widgets[WidgetIndex];
		
		if (!ArrangedWidget.Widget->IsEnabled()) 
		{
			continue;
		}

		// Widget Type Checks, before we call OnNavigation, to handle certain known widget types that consume navigation in an unpredictable way
		// (e.g. SEditableText consumes Left/Right to move the text carat, and STableViewBase handles navigation internally)
		// Add more edge cases here as needed
		static const FName TypeSEditableText("SEditableText");
		static const FName TypeSMultiLineEditableText("SMultiLineEditableText");
		const FName WidgetType = ArrangedWidget.Widget->GetType();
		FSimNavStepResult EdgeCaseResult = {ArrangedWidget.Widget, ENavSimResult::Handled};

		if (WidgetType == TypeSEditableText && (Direction == EUINavigation::Left || Direction == EUINavigation::Right))
		{
			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Editable] Boundary '%s' consumes input. Handled."), *ArrangedWidget.Widget->ToString());
			return EdgeCaseResult;
		}
		if (WidgetType == TypeSMultiLineEditableText)
		{
			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Editable] Boundary '%s' consumes input. Handled."), *ArrangedWidget.Widget->ToString());
			return EdgeCaseResult;
		}
		if (InputFlowHelpers::IsTableViewWidget(ArrangedWidget.Widget))
		{
			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [TableView] Boundary '%s' handles navigation natively. Handled."), *ArrangedWidget.Widget->ToString());
			return EdgeCaseResult;
		}

		FNavigationReply Reply = FNavigationReply::Escape();

#if UE_WITH_SLATE_SIMULATEDNAVIGATIONMETADATA
		if (TSharedPtr<FSimulatedNavigationMetaData> SimMeta = ArrangedWidget.Widget->GetMetaData<FSimulatedNavigationMetaData>())
		{
			if (SimMeta->IsOnNavigationConst())
			{
				Reply = ArrangedWidget.Widget->OnNavigation(ArrangedWidget.Geometry, VirtualNavEvent);
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
			Reply = ArrangedWidget.Widget->OnNavigation(ArrangedWidget.Geometry, VirtualNavEvent);
		}

		// Inject the widget as the Handler. HittestGrid requires this to most accurately simulate the internal navigation handling.
		// See FHittestGrid::FindFocusableWidget (in HittestGrid.cpp)
		UInputDebugSubsystem_SimulateNavigation_PrivateAccessHack::ForceSetHandler(Reply, ConstCastSharedRef<SWidget>(ArrangedWidget.Widget));

		UE_CLOG(bDebugLog, LogInputFlow, Verbose, TEXT("  [Bubble] Evaluated '%s', Rule: %s"), *ArrangedWidget.Widget->ToString(), *UEnum::GetValueAsString(Reply.GetBoundaryRule()));

		// If we found a boundary rule that is NOT Escape (e.g. Stop, Wrap, Explicit), OR we reached the Window root, we stop bubbling.
		if (Reply.GetBoundaryRule() != EUINavigationRule::Escape || ArrangedWidget.Widget == Window || WidgetIndex == 0)
		{
			BoundaryReply = Reply;
			BoundaryWidget = ArrangedWidget;
			break;
		}
	}

	EUINavigationRule Rule = BoundaryReply.GetBoundaryRule();
	UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Boundary Found] '%s' with Rule: %s"), *BoundaryWidget.Widget->ToString(), *UEnum::GetValueAsString(Rule));

	// Evaluate the effective Boundary Rule
	if (Rule == EUINavigationRule::Explicit)
	{
		TSharedPtr<SWidget> ExplicitTarget = BoundaryReply.GetFocusRecipient();

		if (!ExplicitTarget.IsValid())
		{
			UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Explicit-Null] Boundary '%s' returned Explicit(Null). Handled."), *BoundaryWidget.Widget->ToString());
			return {BoundaryWidget.Widget, ENavSimResult::Handled};
		}

		if (ExplicitTarget->IsEnabled() && ExplicitTarget->SupportsKeyboardFocus())
		{
			UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Explicit] Target '%s'"), *ExplicitTarget->ToString());
			return {ExplicitTarget, ENavSimResult::Explicit};
		}

		return {BoundaryWidget.Widget, ENavSimResult::Normal};
	}

	if (Rule == EUINavigationRule::Custom || Rule == EUINavigationRule::CustomBoundary)
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Custom] Boundary '%s' Handled."), *BoundaryWidget.Widget->ToString());
		return {BoundaryWidget.Widget, ENavSimResult::Handled};
	}

	// Perform Spatial Search (or Next/Prev traversal) using the established Boundary
	TSharedPtr<SWidget> ResultWidget = nullptr;
	FSimNavStepResult StepResult;

	if (Direction == EUINavigation::Next || Direction == EUINavigation::Previous)
	{
		FWeakWidgetPath WeakSource(SourcePath);
		FWidgetPath NextPath = WeakSource.ToNextFocusedPath(Direction, BoundaryReply, BoundaryWidget);
		if (NextPath.IsValid()) 
		{
			ResultWidget = NextPath.Widgets.Last().Widget;
		}
	}
	else
	{
		FScopedSwitchWorldHack SwitchWorld(SourcePath);
		
#if WITH_SLATE_DEBUGGING
		// Intercept the internal hit test grid debugging results to visualize *why* things were skipped
		FDelegateHandle DebugHandle = FHittestGrid::OnFindNextFocusableWidgetExecuted.AddLambda([&](const FHittestGrid* Grid, const FHittestGrid::FDebuggingFindNextFocusableWidgetArgs& Args)
		{
			const UInputFlowSettings& Settings = *UInputFlowSettings::Get();
			for (const auto& Intermediate : Args.IntermediateResults)
			{
				if (Args.Result.IsValid() && Intermediate.Widget == Args.Result) continue;
				if (Intermediate.Widget == Source) continue;

				FString ReasonStr;
				ReasonStr.Appendf(TEXT("Widget: '%s',\n"), *Intermediate.Widget->ToString());
				ReasonStr.Appendf(TEXT("Result: %s"), *Intermediate.Result.ToString());

				bool bShouldShow = false;

				// Map the FText/Strings from HittestGridDebuggingText (in HittestGrid.cpp) to settings toggles
				if (ReasonStr.Contains(TEXT("User Index not compatible"))) 
					bShouldShow = Settings.IsNavFilterUserIndexEnabled();
				else if (ReasonStr.Contains(TEXT("Does not intersect"))) 
					bShouldShow = Settings.IsNavFilterIntersectionEnabled();
				else if (ReasonStr.Contains(TEXT("Previous Widget was better"))) 
					bShouldShow = Settings.IsNavFilterDistanceEnabled();
				else if (ReasonStr.Contains(TEXT("Not a descendant"))) 
					bShouldShow = Settings.IsNavFilterDescendantEnabled();
				else if (ReasonStr.Contains(TEXT("Disabled")) || ReasonStr.Contains(TEXT("ParentDisabled"))) 
					bShouldShow = Settings.IsNavFilterDisabledEnabled();
				else if (ReasonStr.Contains(TEXT("Keyboard focus unsupported"))) 
					bShouldShow = Settings.IsNavFilterFocusEnabled();

				if (!bShouldShow) continue;

				// Avoid duplicates
				bool bFound = false;
				for (const FRejectedNavigation& Existing : StepResult.RejectedWidgets)
				{
					if (Existing.Widget == Intermediate.Widget)
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					FRejectedNavigation Rej;
					Rej.Widget = ConstCastSharedPtr<SWidget>(Intermediate.Widget);
					Rej.Reason = ReasonStr;
					StepResult.RejectedWidgets.Add(Rej);
				}
			}
		});
#endif

		// Because BoundaryReply now holds the correct Handler and BoundaryRule (e.g. Stop or Wrap), 
		// HittestGrid natively enforces hierarchy constraints, ensuring results are descendants 
		// of the modal layer (filtering out layer 0 / background widgets).
		ResultWidget = Window->GetHittestGrid().FindNextFocusableWidget(
			SourcePath.Widgets.Last(),
			Direction,
			BoundaryReply,
			BoundaryWidget,
			UserIndex
		);

#if WITH_SLATE_DEBUGGING
		FHittestGrid::OnFindNextFocusableWidgetExecuted.Remove(DebugHandle);
#endif
	}

	if (ResultWidget.IsValid())
	{
		if (!InputFlowHelpers::IsGameWorldWidget(ResultWidget))
		{
			UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Rejected] Target '%s' is not in Game World."), *ResultWidget->ToString());
			StepResult.RejectedWidgets.Add({ResultWidget, TEXT("Not in Game World")});
			
			if (Rule == EUINavigationRule::Stop) return {BoundaryWidget.Widget, ENavSimResult::Stopped};
			if (Rule == EUINavigationRule::Wrap) return {BoundaryWidget.Widget, ENavSimResult::Handled};
			
			return {nullptr, ENavSimResult::Normal};
		}

		UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Search] Boundary '%s' -> Found '%s'"), *BoundaryWidget.Widget->ToString(), *ResultWidget->ToString());
		StepResult.Widget = ResultWidget;
		return StepResult;
	}

	if (Rule == EUINavigationRule::Stop)
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Stop] Hit Stop Rule."));
		StepResult.Widget = BoundaryWidget.Widget;
		StepResult.ResultType = ENavSimResult::Stopped;
		return StepResult;
	}

	if (Rule == EUINavigationRule::Wrap)
	{
		UE_CLOG(bDebugLog, LogInputFlow, Warning, TEXT("  [Wrap] Hit Wrap Rule (Simulated as Handled)."));
		StepResult.Widget = BoundaryWidget.Widget;
		StepResult.ResultType = ENavSimResult::Handled;
		return StepResult;
	}

	UE_CLOG(bDebugLog, LogInputFlow, Log, TEXT("  [Result] Bubbled to Root. No Target."));
	return StepResult;
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
		const int32 RealUserIndex = FSlateApplication::Get().GetUserIndexForKeyboard(); // TODO: Support multiple users?

		for (const EUINavigation Dir : Directions)
		{
			FSimNavStepResult SimResult = SimulateNavigation(CurrentWidget, Dir, RealUserIndex);
			TSharedPtr<SWidget> Next = SimResult.Widget;
			const ENavSimResult ResultType = SimResult.ResultType;

			if (Next.IsValid() && InputFlowHelpers::IsGameWorldWidget(Next))
			{
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

				const bool bIsTerminal = (ResultType == ENavSimResult::Handled || ResultType == ENavSimResult::Stopped);
				const bool bIsVisited = VisitedWidgets.Contains(Next);
				const bool bIsExplicit = (ResultType == ENavSimResult::Explicit);
				const bool bShouldRecord = bIsTerminal || bIsExplicit || !bIsVisited;

				if (bShouldRecord)
				{
					// Find existing link or append a new one immediately
					bool bFound = false;
					for (FNavigationLink& ExistingLink : NavigationLinks)
					{
						if (ExistingLink.StartWidget == CurrentWidget && ExistingLink.Direction == Dir)
						{
							ExistingLink.EndWidget = Next;
							ExistingLink.DepthStep = CurrentDepth + 1;
							ExistingLink.ResultType = ResultType;
							ExistingLink.SimVersion = CurrentSimVersion;
							bFound = true;
							break;
						}
					}

					if (!bFound)
					{
						FNavigationLink Link;
						Link.StartWidget = CurrentWidget;
						Link.EndWidget = Next;
						Link.Direction = Dir;
						Link.DepthStep = CurrentDepth + 1;
						Link.ResultType = ResultType;
						Link.RejectedWidgets = SimResult.RejectedWidgets;
						Link.SimVersion = CurrentSimVersion;
						NavigationLinks.Add(Link);
					}

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
		// Clean up stale links that were not rediscovered in this pass
		NavigationLinks.RemoveAll([this](const FNavigationLink& Link) {
			return Link.SimVersion != CurrentSimVersion;
		});
		bSimulationInProgress = false;
		LastSimulationStartTime = FPlatformTime::Seconds();
	}
}

void UInputDebugSubsystem::StartNewSimulation(TSharedPtr<SWidget> StartWidget, const bool bStartFromScratch)
{
	CurrentSimVersion++;
	SimulationQueue.Reset();
	VisitedWidgets.Reset();

	LastSimulationStartWidget = StartWidget;
	bSimulationInProgress = false;

	if (bStartFromScratch)
	{
		NavigationLinks.Reset();
	}

	if (StartWidget.IsValid() && InputFlowHelpers::IsGameWorldWidget(StartWidget))
	{
		SimulationQueue.Add({StartWidget, 0});
		VisitedWidgets.Add(StartWidget);
		bSimulationInProgress = true;
	}
}