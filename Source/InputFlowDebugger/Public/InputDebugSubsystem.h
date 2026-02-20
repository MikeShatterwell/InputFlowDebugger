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

// Represents a widget that was evaluated for navigation but rejected
struct FRejectedNavigation
{
	TWeakPtr<SWidget> Widget;
	FString Reason;
};

// Represents a single link in the spider nav graph
struct FNavigationLink
{
	TWeakPtr<SWidget> StartWidget;
	TWeakPtr<SWidget> EndWidget;
	EUINavigation Direction;
	uint32 DepthStep; // 1 = Immediate, 2 = Next, etc.
	ENavSimResult ResultType = ENavSimResult::Normal;
	uint32 SimVersion = 0; // Tracks which simulation pass found this link
	TArray<FRejectedNavigation> RejectedWidgets;
};

// Represents the result of a single step in the simulation
struct FSimNavStepResult
{
	TSharedPtr<SWidget> Widget;
	ENavSimResult ResultType = ENavSimResult::Normal;
	TArray<FRejectedNavigation> RejectedWidgets;
};

// Snapshot of data for the Overlay to render without accessing Editor classes
struct FInputSnapshotStrings
{
	FString ActiveCommonUILeaf = TEXT("None");
	FString InputConfig = TEXT("None");
	FString MouseCaptureMode = TEXT("None");
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
class UInputFlowSettings;

/*
 * Subsystem that collects input events via the InputFlowSpy and organizes the navigation simulation "spider".
 */
UCLASS()
class INPUTFLOWDEBUGGER_API UInputDebugSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Begin USubsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// End USubsystem overrides

	const TArray<TSharedPtr<FInputEventLog>>& GetLogHistory() const { return LogHistory; }
	int32 GetLogHistoryWriteIndex() const { return LogHistoryIndex; }
	bool IsLogHistoryWrapped() const { return bLogHistoryWrapped; }
	void ClearLogHistory();
	uint32 GetLogVersion() const { return LogVersion; }

	const FInputSnapshotStrings& GetInputSnapshotStrings() const { return DataSnapshot; }

	// Data Access
	const TArray<FNavigationLink>& GetNavigationLinks() const { return NavigationLinks; }
	TSharedPtr<SWidget> GetFocusedWidget() const { return FocusedWidget.Pin(); }
	const TArray<FFocusHistoryEntry>& GetFocusHistory() const { return FocusHistory; }

	// Callback for settings changes to handle side-effects (like Overlay visibility/scale/etc)
	void HandleSettingsChanged();

private:
	// Internal ticker to sync data from the Spy and run Spider
	bool TickSyncLogs(float DeltaTime);
	void TickNavigationSim(float DeltaTime);

	// Callback from Spy on Focus Change
	void OnSpyFocusChanged(const TSharedPtr<SWidget>& NewFocus, const FFocusEvent& InFocusEvent);
	
	// Recursive navigation finder
	void StartNewSimulation(TSharedPtr<SWidget> StartWidget, const bool bStartFromScratch);
	
	// Single step simulation
	FSimNavStepResult SimulateNavigation(const TSharedPtr<SWidget>& Source, EUINavigation Direction, int32 UserIndex) const;

	void ProcessSimulationQueue();
	void UpdateDataSnapshot();

	// --- Members ---

	TSharedPtr<FInputFlowSpy> InputSpy;
	FInputSnapshotStrings DataSnapshot;

	// Log History
	TArray<TSharedPtr<FInputEventLog>> LogHistory;
	bool bLogHistoryWrapped = false;
	uint32 LogHistoryIndex = 0;
	uint32 LogVersion = 0;
	const uint32 MaxLogHistorySize = 2000;
	FTSTicker::FDelegateHandle LogSyncTickerHandle;

	// Overlay State
	TSharedPtr<SInputFlowOverlay> OverlayWidget;
	TSharedPtr<class SWidget> OverlayHost; 
	TArray<FFocusHistoryEntry> FocusHistory;
	bool bOverlayActive = false;
	// Simulation Results
	TWeakPtr<SWidget> FocusedWidget;
	TArray<FNavigationLink> NavigationLinks; // What the UI is currently drawing

	// --- Time Slicing State ---
	struct FSimQueueItem
	{
		TWeakPtr<SWidget> Widget;
		int32 Depth;
	};

	TArray<FSimQueueItem> SimulationQueue;
	TSet<TWeakPtr<SWidget>> VisitedWidgets;
	bool bSimulationInProgress = false;
	TWeakPtr<SWidget> LastSimulationStartWidget;
	double LastSimulationStartTime = 0.0;
	const double MaxSimulationTimePerFrame = 0.002f;
	uint32 CurrentSimVersion = 0;
};