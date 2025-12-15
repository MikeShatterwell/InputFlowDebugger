// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <Containers/Ticker.h>
#include <CoreMinimal.h>

// Engine
#include <Subsystems/GameInstanceSubsystem.h>

// Internal
#include "InputFlowSpy.h"

#include "InputDebugSubsystem.generated.h"

UENUM(BlueprintType)
enum class ENavSimResult : uint8
{
	Normal,     // Found a neighbor
	Handled,    // Widget consumed the event via Custom Delegate
	Stopped     // Widget blocked navigation via Stop Rule
};

// Represents a single link in the spider nav graph
struct FNavigationLink
{
	TWeakPtr<SWidget> StartWidget;
	TWeakPtr<SWidget> EndWidget;
	EUINavigation Direction;
	uint32 DepthStep; // 1 = Immediate, 2 = Next, etc.
	ENavSimResult ResultType = ENavSimResult::Normal;
};

// Snapshot of data for the Overlay to render without accessing Editor classes
struct FInputOverlayState
{
	FString ActiveCommonUILeaf;
	FString InputConfig;
	FString MouseCaptureMode;
	TArray<FString> ActiveEnhancedInputActions;
	TArray<FString> BoundActions;
};

struct FFocusHistoryEntry
{
	TWeakPtr<SWidget> Widget;
	EFocusCause FocusCause = EFocusCause::Cleared;
	double Timestamp = 0.0;
};

class SInputFlowOverlay;

/*
 * Subsystem that collects input events via the InputFlowSpy and organizes the navigation simulation "spider".
 */
UCLASS()
class INPUTFLOWDEBUGGER_API UInputDebugSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const TArray<TSharedPtr<FInputEventLog>>& GetLogHistory() const { return LogHistory; }
	int32 GetLogHistoryWriteIndex() const { return LogHistoryIndex; }
	bool IsLogHistoryWrapped() const { return bLogHistoryWrapped; }
	void ClearLogHistory();
	uint32 GetLogVersion() const { return LogVersion; }

	// --- Spy Configuration ---
	void SetCaptureMouseMove(bool bEnabled);
	bool GetCaptureMouseMove() const;

	void SetCaptureClicks(bool bEnabled);
	bool GetCaptureClicks() const;

	void SetCaptureHover(bool bEnabled);
	bool GetCaptureHover() const;

	void SetCaptureAnalog(bool bEnabled);
	bool GetCaptureAnalog() const;

	void SetCaptureFocus(bool bEnabled);
	bool GetCaptureFocus() const;

	void SetCaptureKeyEvents(bool bEnabled);
	bool GetCaptureKeyEvents() const;
	// -------------------------

	// --- Overlay ---
	void SetOverlayEnabled(bool bEnabled);
	bool IsOverlayEnabled() const;
	
	void SetShowHitTestGrid(bool bEnabled) { bShowHitTestGrid = bEnabled; }
	bool GetShowHitTestGrid() const { return bShowHitTestGrid; }
	// -------------------------

	// -- Navigation Simulation ---
	void SetNavigationSimulationEnabled(bool bEnabled) { bEnableNavigationSimulation = bEnabled; }
	bool GetNavigationSimulationEnabled() const { return bEnableNavigationSimulation; }
	
	void SetNavigationDepth(int32 NewDepth);
	int32 GetNavigationDepth() const { return NavigationSearchDepth; }
	// -----------------------------

	// Data Access for Overlay
	const TArray<FNavigationLink>& GetNavigationLinks() const { return NavigationLinks; }
	const FInputOverlayState& GetOverlayState() const { return OverlayState; }
	TSharedPtr<SWidget> GetFocusedWidget() const { return FocusedWidget.Pin(); }
	const TArray<FFocusHistoryEntry>& GetFocusHistory() const { return FocusHistory; }

private:
	// Internal ticker to sync data from the Spy and run Spider
	bool TickSyncLogs(float DeltaTime);
	void TickNavigationSim(float DeltaTime);

	// Callback from Spy on Focus Change
	void OnSpyFocusChanged(const TSharedPtr<SWidget>& NewFocus, const FFocusEvent& InFocusEvent);
	
	// Recursive navigation finder responsible for filling NavigationLinks with the crawled results
	void StartNewSimulation(TSharedPtr<SWidget> StartWidget);
	
	// Single step simulation
	TPair<TSharedPtr<SWidget>, ENavSimResult> SimulateNavigation(const TSharedPtr<SWidget>& Source, EUINavigation Direction, int32 RealUserIndex) const;

	// Helper to process a chunk of the queue
	void ProcessSimulationQueue();

	// Snapshots and formats data for FInputOverlayState
	void UpdateDataSnapshot();

	// --- Members ---

	// The Spy Instance that captures Slate-level input
	TSharedPtr<FInputFlowSpy> InputSpy;

	// Ring Buffer for the Event Log
	TArray<TSharedPtr<FInputEventLog>> LogHistory;
	bool bLogHistoryWrapped = false;
	uint32 LogHistoryIndex = 0;
	uint32 LogVersion = 0;
	const uint32 MaxLogHistorySize = 2000; // TODO: Make configurable?
	FTSTicker::FDelegateHandle LogSyncTickerHandle;

	// Overlay State
	TSharedPtr<SInputFlowOverlay> OverlayWidget;
	TSharedPtr<class SWidget> OverlayHost; // Container added to viewport
	bool bOverlayEnabled = false;
	bool bEnableNavigationSimulation = true;
	bool bShowHitTestGrid = false;
	FInputOverlayState OverlayState;
	TArray<FFocusHistoryEntry> FocusHistory;

	// Simulation Results
	TWeakPtr<SWidget> FocusedWidget;
	TArray<FNavigationLink> NavigationLinks;
	uint32 NavigationSearchDepth = 1;

	// --- Time Slicing State ---

	// Queue item: Widget and its Depth
	struct FSimQueueItem
	{
		TWeakPtr<SWidget> Widget;
		int32 Depth;
	};

	TArray<FSimQueueItem> SimulationQueue;
	TSet<TWeakPtr<SWidget>> VisitedWidgets;
	bool bSimulationInProgress = false;
	TWeakPtr<SWidget> LastSimulationStartWidget;
	const double MaxSimulationTimePerFrame = 0.002f; // 2ms budget
};