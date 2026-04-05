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

class SWidget;
class UInputDebugSubsystem;

/**
 * Interface for queuing labels in the physics-based label canvas
 */
class INPUTFLOWDEBUGGER_API FInputFlowLabelAPI
{
public:
	virtual ~FInputFlowLabelAPI() = default;
	
	/** Queue a text label at an absolute screen position */
	virtual void QueueLabel(const FVector2D& AbsolutePosition, const FString& Text, const FLinearColor& Color, const FVector2D& Pivot = FVector2D(0.5f, 0.5f)) = 0;
	
	/** Helper to automatically position a label above a specific widget */
	virtual void QueueWidgetLabel(TSharedPtr<SWidget> Widget, const FString& Text, const FLinearColor& Color) = 0;
};

/**
 * Interface for drawing shapes/splines directly to the HUD
 */
class INPUTFLOWDEBUGGER_API FInputFlowDrawAPI
{
public:
	virtual ~FInputFlowDrawAPI() = default;
	
	/** Draw a simple line between two absolute screen positions */
	virtual void DrawLine(const FVector2D& AbsoluteStart, const FVector2D& AbsoluteEnd, const FLinearColor& Color, float Thickness = 1.0f) = 0;
	
	/** Draw an empty box (outline) */
	virtual void DrawBox(const FVector2D& AbsoluteTopLeft, const FVector2D& Size, const FLinearColor& Color, float Thickness = 1.0f) = 0;
	
	/** Draw a smooth bezier curve between two points */
	virtual void DrawSpline(const FVector2D& AbsoluteStart, const FVector2D& StartTangent, const FVector2D& AbsoluteEnd, const FVector2D& EndTangent, const FLinearColor& Color, float Thickness = 2.0f) = 0;
	
	/** Automatically draws a highlight box around a widget's bounds */
	virtual void DrawWidgetHighlight(TSharedPtr<SWidget> Widget, const FLinearColor& Color, float Thickness = 2.0f) = 0;
};

// Delegates for external code to bind to
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGatherInputFlowLabels, UInputDebugSubsystem* /*Subsystem*/, FInputFlowLabelAPI& /*LabelAPI*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDrawInputFlowOverlay, UInputDebugSubsystem* /*Subsystem*/, FInputFlowDrawAPI& /*DrawAPI*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnInputFlowCaptureChanged, bool /*bIsCapturing*/);

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
	// Special user index reserved for simulated navigation events
	// Widdgets can check for this in their OnNavigation override to avoid side effects during simulation
	constexpr static uint32 SimulatedNavigationUserIndex = 999;

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

	/** Hook for external plugins to add custom labels to the physics solver */
	FOnGatherInputFlowLabels& GetOnGatherLabels() { return OnGatherLabels; }
	
	/** Hook for external plugins to draw custom shapes, lines, and splines */
	FOnDrawInputFlowOverlay& GetOnDrawOverlay() { return OnDrawOverlay; }

	/**
	 * Fires when the overlay starts or stops capturing input.
	 * Game code should bind to this to suppress its own input handling while the overlay is active
	 */
	FOnInputFlowCaptureChanged& GetOnInputCaptureChanged() { return OnInputCaptureChanged; }

	/** Polling accessor — returns true while the overlay is actively capturing input. */
	bool IsCapturingInput() const { return bOverlayActive; }

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

	// External Hooks
	FOnGatherInputFlowLabels OnGatherLabels;
	FOnDrawInputFlowOverlay OnDrawOverlay;
	FOnInputFlowCaptureChanged OnInputCaptureChanged;

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