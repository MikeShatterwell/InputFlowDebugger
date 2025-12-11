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

// SlateCore
#include "Input/HittestGrid.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWeakWidget.h"

// CommonUI
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

void UInputDebugSubsystem::AddLog(const FString& Type, const FString& InputDetails, FColor Color,
                                  const FString& WidgetType, const FString& WidgetName, const FString& WidgetState,
                                  bool bIsButton, UObject* SourceObject, const TArray<FInputLogRichTextPart>& InParts)
{
	// Collapse duplicates on the previous entry if it exists
	if (LogHistoryIndex > 0 || bLogHistoryWrapped)
	{
		int32 PrevIndex = (LogHistoryIndex == 0) ? MaxLogHistorySize - 1 : LogHistoryIndex - 1;

		if (LogHistory.IsValidIndex(PrevIndex))
		{
			TSharedPtr<FInputEventLog> LastLog = LogHistory[PrevIndex];

			if (LastLog.IsValid() &&
				LastLog->EventType == Type &&
				LastLog->InputDetails == InputDetails &&
				LastLog->WidgetName == WidgetName &&
				LastLog->WidgetState == WidgetState)
			{
				LastLog->Count++;
				LastLog->TimeSeconds = FPlatformTime::Seconds();
				LastLog->CaptureTime = FDateTime::Now();
				LogVersion++;
				return;
			}
		}
	}

	FInputEventLog NewLog;
	NewLog.TimeSeconds = FPlatformTime::Seconds();
	NewLog.CaptureTime = FDateTime::Now();
	NewLog.EventType = Type;
	NewLog.InputDetails = InputDetails;
	NewLog.Color = Color;
	NewLog.Count = 1;
	NewLog.WidgetType = WidgetType;
	NewLog.WidgetName = WidgetName;
	NewLog.WidgetState = WidgetState;
	NewLog.bIsButton = bIsButton;
	NewLog.SourceObject = SourceObject;
	NewLog.RichTextParts = InParts; // Store parts

	if (SourceObject) NewLog.SourceClass = SourceObject->GetClass();

	if (LogHistory.Num() != MaxLogHistorySize)
	{
		LogHistory.SetNum(MaxLogHistorySize);
	}

	LogHistory[LogHistoryIndex] = MakeShared<FInputEventLog>(NewLog);
	LogHistoryIndex++;

	if (LogHistoryIndex >= MaxLogHistorySize)
	{
		LogHistoryIndex = 0;
		bLogHistoryWrapped = true;
	}
	LogVersion++;
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

// --- Navigation Spider ---

void UInputDebugSubsystem::TickNavigationSim(float DeltaTime)
{
	if (!bOverlayEnabled) return;
	if (!FSlateApplication::IsInitialized()) return;

	TSharedPtr<SWidget> CurrentFocus = FocusedWidget.Pin();

	// 1. Detect Focus Change (Restart Condition)
	if (CurrentFocus != LastSimulationStartWidget.Pin())
	{
		// Focus changed! Abandon current work and start over.
		StartNewSimulation(CurrentFocus);
		return;
	}

	// 2. Continue work periodically
	if (bSimulationInProgress)
	{
		ProcessSimulationQueue();
		/*static float AccumulatedSimTime = 0;
		AccumulatedSimTime += DeltaTime;
		if (AccumulatedSimTime >= 0.2f)
		{
			AccumulatedSimTime = 0.0f;
			ProcessSimulationQueue();
		}*/
	}
}

void UInputDebugSubsystem::OnSpyFocusChanged(const TSharedPtr<SWidget>& NewFocus)
{
	if (!NewFocus.IsValid() || !InputFlowHelpers::IsGameWorldWidget(NewFocus)) return;

	FocusedWidget = NewFocus;

	if (NewFocus.IsValid())
	{
		if (FocusHistory.Num() == 0 || FocusHistory.Last().Widget != NewFocus)
		{
			FFocusHistoryEntry Entry;
			Entry.Widget = NewFocus;
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
	if (!Source.IsValid() || !Source->SupportsKeyboardFocus() || !bEnableNavigationSimulation)
	{
		return {nullptr, ENavSimResult::Normal};
	}

	const bool bDebugLog = (Source == LastSimulationStartWidget.Pin()); // TODO: Make a CVar? Use Verbose log? Lots of spam potential.

	FWidgetPath SourcePath;
	if (!FSlateApplication::Get().FindPathToWidget(Source.ToSharedRef(), SourcePath))
	{
		return {nullptr, ENavSimResult::Normal};
	}

	// Use actual user index for visibility/interaction checks
	const FNavigationEvent VirtualNavEvent(FModifierKeysState(), RealUserIndex, Direction,
	                                       ENavigationGenesis::Controller);

	TSharedRef<SWindow> Window = SourcePath.GetDeepestWindow();
	const FSlateLayoutTransform WindowInverse = Window->GetWindowGeometryInScreen().GetAccumulatedLayoutTransform().
	                                                    Inverse();

	if (bDebugLog)
	{
		UE_LOG(LogInputFlow, Log, TEXT("--- Simulating %s from '%s' ---"),
		       *UEnum::GetValueAsString(Direction), *Source->ToString());
	}

	for (int32 WidgetIndex = SourcePath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget& ArrangedBoundary = SourcePath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& BoundaryWidget = ArrangedBoundary.Widget;

		if (!BoundaryWidget->IsEnabled()) continue;

		// 1. Query the Widget's Rule
		FNavigationReply Reply = FNavigationReply::Escape();

#if UE_WITH_SLATE_SIMULATEDNAVIGATIONMETADATA
		if (TSharedPtr<FSimulatedNavigationMetaData> SimMeta = BoundaryWidget->GetMetaData<
			FSimulatedNavigationMetaData>())
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
				case EUINavigationRule::Explicit: Reply = FNavigationReply::Explicit(MetaTarget);
					break;
				case EUINavigationRule::Custom:
				case EUINavigationRule::CustomBoundary: Reply = FNavigationReply::Custom(FNavigationDelegate());
					break;
				case EUINavigationRule::Stop: Reply = FNavigationReply::Stop();
					break;
				case EUINavigationRule::Wrap: Reply = FNavigationReply::Wrap();
					break;
				case EUINavigationRule::Escape: default: Reply = FNavigationReply::Escape();
					break;
				}
			}
		}
		else
#endif
		{
			Reply = BoundaryWidget->OnNavigation(ArrangedBoundary.Geometry, VirtualNavEvent);
		}

		EUINavigationRule Rule = Reply.GetBoundaryRule();
		bool bWasExplicitButNull = false;

		// 2. Handle Explicit / Stop / Custom
		if (Rule == EUINavigationRule::Explicit)
		{
			TSharedPtr<SWidget> ExplicitTarget = Reply.GetFocusRecipient();

			if (ExplicitTarget.IsValid() && ExplicitTarget->IsEnabled() && ExplicitTarget->SupportsKeyboardFocus())
			{
				if (bDebugLog) UE_LOG(LogInputFlow, Warning, TEXT("  [Explicit] Boundary '%s' -> Target '%s'"),
				                      *BoundaryWidget->ToString(), *ExplicitTarget->ToString());
				return {ExplicitTarget, ENavSimResult::Normal};
			}

			// Fix: If Explicit but Null, we treat this as a "Soft Block".
			// We will attempt to search, but if search fails, we consider it Handled here rather than bubbling.
			if (bDebugLog) UE_LOG(LogInputFlow, Log,
			                      TEXT(
				                      "  [Explicit-Null] Boundary '%s' provided null target. Attempting spatial search."
			                      ), *BoundaryWidget->ToString());
			bWasExplicitButNull = true;
			Rule = EUINavigationRule::Escape;
		}
		else if (Rule == EUINavigationRule::Stop)
		{
			if (bDebugLog) UE_LOG(LogInputFlow, Warning, TEXT("  [Stop] Boundary '%s' hit Stop Rule."),
			                      *BoundaryWidget->ToString());
			return {BoundaryWidget, ENavSimResult::Stopped};
		}
		else if (Rule == EUINavigationRule::Custom)
		{
			if (bDebugLog) UE_LOG(LogInputFlow, Warning, TEXT("  [Custom] Boundary '%s' Handled."),
			                      *BoundaryWidget->ToString());
			return {BoundaryWidget, ENavSimResult::Handled};
		}

		// 3. Perform Spatial Search
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
		}

		// 4. Success?
		if (ResultWidget.IsValid() && InputFlowHelpers::IsGameWorldWidget(ResultWidget))
		{
			if (bDebugLog) UE_LOG(LogInputFlow, Log, TEXT("  [Search] Boundary '%s' -> Found '%s'"),
			                      *BoundaryWidget->ToString(), *ResultWidget->ToString());
			return {ResultWidget, ENavSimResult::Normal};
		}

		// 5. Handling Search Failure
		if (bWasExplicitButNull)
		{
			if (bDebugLog) UE_LOG(LogInputFlow, Verbose,
			                      TEXT("    ...Strict search failed. Attempting global relaxed search within boundary."
			                      ));

			FArrangedWidget WindowSpaceLeaf = SourcePath.Widgets.Last();
			WindowSpaceLeaf.Geometry.AppendTransform(WindowInverse);

			// Use the WINDOW as the boundary (max possible scope)
			FArrangedWidget WindowArrangedWidget(Window, Window->GetWindowGeometryInScreen());
			WindowArrangedWidget.Geometry.AppendTransform(WindowInverse);

			FScopedSwitchWorldHack SwitchWorld(SourcePath);
			TSharedPtr<SWidget> RelaxedResult = Window->GetHittestGrid().FindNextFocusableWidget(
				WindowSpaceLeaf,
				Direction,
				FNavigationReply::Escape(), // Pure spatial, no rules
				WindowArrangedWidget,
				RealUserIndex
			);

			if (RelaxedResult.IsValid())
			{
				// Verify the result is actually relevant (is it inside our current boundary?)
				// We don't want to jump out of the ListView if the ListView is what blocked us.
				FWidgetPath PathToResult;
				FSlateApplication::Get().GeneratePathToWidgetUnchecked(RelaxedResult.ToSharedRef(), PathToResult);

				if (PathToResult.ContainsWidget(&BoundaryWidget.Get()))
				{
					if (bDebugLog) UE_LOG(LogInputFlow, Log,
					                      TEXT("  [Relaxed-Search] Found '%s' inside explicit boundary."),
					                      *RelaxedResult->ToString());
					return {RelaxedResult, ENavSimResult::Normal};
				}
			}
			
			if (bDebugLog) UE_LOG(LogInputFlow, Warning,
			                      TEXT("  [Explicit-Null] Boundary '%s' Handled (Search exhausted)."),
			                      *BoundaryWidget->ToString());
			return {BoundaryWidget, ENavSimResult::Handled};
		}

		if (Rule == EUINavigationRule::Wrap)
		{
			if (bDebugLog) UE_LOG(LogInputFlow, Warning, TEXT("  [Wrap] Boundary '%s' Wrap failed."),
			                      *BoundaryWidget->ToString());
			return {BoundaryWidget, ENavSimResult::Stopped};
		}

		if (bDebugLog) UE_LOG(LogInputFlow, Verbose, TEXT("  [Escape] Boundary '%s' bubbling up."),
		                      *BoundaryWidget->ToString());
	}

	if (bDebugLog) UE_LOG(LogInputFlow, Log, TEXT("  [Result] Bubbled to Root. No Target."));
	return {nullptr, ENavSimResult::Normal};
}

void UInputDebugSubsystem::ProcessSimulationQueue()
{
	const double StartTime = FPlatformTime::Seconds();
	const int32 RealUserIndex = FSlateApplication::Get().GetUserIndexForKeyboard();
	// TODO: Support multiple users with controllers
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

		for (const EUINavigation Dir : Directions)
		{
			TPair<TSharedPtr<SWidget>, ENavSimResult> SimResult = SimulateNavigation(CurrentWidget, Dir, RealUserIndex);
			TSharedPtr<SWidget> Next = SimResult.Key;
			ENavSimResult ResultType = SimResult.Value;

			if (Next.IsValid() && InputFlowHelpers::IsGameWorldWidget(Next))
			{
				bool bIsTerminal = (ResultType == ENavSimResult::Handled || ResultType == ENavSimResult::Stopped);

				// -----------------------------------------------------------
				// FILTER LOGIC
				// -----------------------------------------------------------

				bool bIsSameWidget = (Next == CurrentWidget);
				bool bIsInternal = false;

				// If addresses differ, check if it's just a structural child/parent (e.g. Button finding its own background)
				if (!bIsSameWidget && ResultType == ENavSimResult::Normal)
				{
					// Advanced path checking using Slate's relationship tools
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
					// Drop links that don't actually leave the widget hierarchy
					continue;
				}

				bool bIsVisited = VisitedWidgets.Contains(Next);
				bool bShouldRecord = bIsTerminal || !bIsVisited;

				if (bShouldRecord)
				{
					// Only log real external links
					UE_LOG(LogInputFlow, Log, TEXT("  LINK ADDED: %s -> %s [%s]"),
					       *InputFlowHelpers::GetWidgetDisplayName(CurrentWidget),
					       *InputFlowHelpers::GetWidgetDisplayName(Next),
					       *UEnum::GetValueAsString(Dir));

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
