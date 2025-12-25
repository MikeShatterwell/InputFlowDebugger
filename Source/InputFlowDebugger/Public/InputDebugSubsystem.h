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
	Normal,     // Found a neighbor via spatial hit test
	Explicit,   // Widget has a specific target via Explicit Rule
	Handled,    // Widget consumed the event via Custom Delegate (or is a ListView/TileView which handles navigation internally)
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
	// Log Filtering
	void SetShowHandledEvents(bool bEnabled);
	bool GetShowHandledEvents() const;
	
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
	// Panel Visibility Toggles
	void SetOverlayEnabled(bool bEnabled);
	bool GetIsOverlayEnabled() const { return bOverlayEnabled; }

	void SetShowSettingsPanel(bool bEnabled) { bShowSettingsPanel = bEnabled; }
	bool GetShowSettingsPanel() const { return bShowSettingsPanel; }

	void SetShowLogPanel(bool bEnabled) { bShowLogPanel = bEnabled; }
	bool GetShowLogPanel() const { return bShowLogPanel; }

	void SetShowHierarchyPanel(bool bEnabled) { bShowHierarchyPanel = bEnabled; }
	bool GetShowHierarchyPanel() const { return bShowHierarchyPanel; }
	
	void SetShowEnhancedInputPanel(bool bEnabled) { bShowEnhancedInputPanel = bEnabled; }
	bool GetShowEnhancedInputPanel() const { return bShowEnhancedInputPanel; }

	void SetShowDashboardPanel(bool bEnabled) { bShowDashboardPanel = bEnabled; }
	bool GetShowDashboardPanel() const { return bShowDashboardPanel; }

	// Hit Test Grid
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
	TPair<TSharedPtr<SWidget>, ENavSimResult> SimulateNavigation(const TSharedPtr<SWidget>& Source, EUINavigation Direction, int32 UserIndex) const;

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
	FInputOverlayState OverlayState;
	TArray<FFocusHistoryEntry> FocusHistory;
	bool bOverlayEnabled = false;
	bool bEnableNavigationSimulation = true;
	
	// Panel Visibility State
	bool bShowSettingsPanel = true;
	bool bShowLogPanel = true;
	bool bShowHierarchyPanel = true;
	bool bShowEnhancedInputPanel = true;
	bool bShowDashboardPanel = true;

	bool bShowHitTestGrid = false;
	bool bShowHandledEvents = false;

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