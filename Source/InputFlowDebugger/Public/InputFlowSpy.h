// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>
#include <Misc/DateTime.h>

// Slate
#include <Framework/Application/IInputProcessor.h>
#include <Framework/Application/SlateApplication.h>

// SlateCore
#if WITH_SLATE_DEBUGGING
#include "Debugging/SlateDebugging.h"
#endif

class UWidget;
class UCommonButtonBase;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInputFlowFocusChanged, const TSharedPtr<SWidget>& /*NewFocus*/, const FFocusEvent& /*InFocusEvent*/);

struct FInputLogRichTextPart
{
	FString Text;
	TWeakObjectPtr<UObject> Object;
	TWeakObjectPtr<UClass> Class; // Backup if instance is GC'd
	bool bIsLink = false;
	bool bOpenRootContext = false;

	FInputLogRichTextPart() = default;
	FInputLogRichTextPart(const FString& InText) : Text(InText), bIsLink(false) {}
	FInputLogRichTextPart(const FString& InText, UObject* InObj, bool bInOpenRoot) 
		: Text(InText), Object(InObj), bIsLink(true), bOpenRootContext(bInOpenRoot)
	{
		if (InObj) Class = InObj->GetClass();
	}
};

struct FInputEventLog
{
	double TimeSeconds = 0.0f; 
	FDateTime CaptureTime;     
	
	FString EventType;
	FString InputDetails; 
	FColor Color;
	int32 Count = 1;

	FString WidgetType;
	FString WidgetName;
	FString WidgetState;
	bool bIsButton = false;
	
	TArray<FInputLogRichTextPart> RichTextParts;

	// Reference to the object for "Go To Asset" functionality
	TWeakObjectPtr<UObject> SourceObject;
	// Backup reference to class in case instance is GC'd
	TWeakObjectPtr<UClass> SourceClass;
};

/** 
 * Intercepts low-level Slate input events before they reach widgets.
 */
class FInputFlowSpy : public IInputProcessor, public TSharedFromThis<FInputFlowSpy>
{
public:
	FInputFlowSpy();
	virtual ~FInputFlowSpy();

	// --- IInputProcessor Interface ---
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {};
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	const TArray<FInputEventLog>& GetEventLog() const { return EventLog; }
	void ClearLog() { EventLog.Empty(); }

	// Returns the raw buffer and ring buffer state
	const TArray<FInputEventLog>& GetEventLogBuffer() const { return EventLog; }
	int32 GetWriteIndex() const { return WriteIndex; }
	bool IsWrapped() const { return bWrapped; }

	// Clear local buffer
	void ResetBuffer();

	FOnInputFlowFocusChanged& OnFocusChanged() { return OnFocusChangedDelegate; }
	
	// Configuration
	bool bCaptureMouseMove = false;
	bool bCaptureAnalog = false;
	bool bCaptureMouseClicks = true;
	bool bCaptureHover = false;
	bool bCaptureFocusEvents = true;
	bool bCaptureKeyEvents = true;
	bool bCaptureHandledEvents = true;

private:
	void AddLog(const FString& Type, const FString& InputDetails, FColor Color, const FString& WidgetType = TEXT(""), const FString& WidgetName = TEXT(""), const FString& WidgetState = TEXT(""), bool bIsButton = false, UObject* InSourceObject = nullptr, const TArray<FInputLogRichTextPart>& InParts = TArray<FInputLogRichTextPart>());
	
	// Helper to build the chain of widgets (Leaf -> Parent -> Root)
	void GenerateWidgetContextParts(const TSharedPtr<SWidget>& Widget, TArray<FInputLogRichTextPart>& OutParts, FString& OutFlatName) const;

	// Helpers
	FString GetWidgetDisplayName(const TSharedPtr<SWidget>& Widget) const;
	FString GetWidgetStateDescription(const TSharedPtr<SWidget>& Widget) const;
	TSharedPtr<SWidget> FindInterestingWidget(FSlateApplication& SlateApp, const FVector2D& CursorPos) const;

	// Focus & Selection Tracking
	void OnFocusChanging(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& InNewPath, const TSharedPtr<SWidget>& NewFocusedWidget);
	FString GetNavigationDirectionString(EUINavigation NavDir) const;
	
	void BindButtonObservation(const TSharedPtr<SWidget>& Widget);
	
	// Callbacks matching CommonUI delegates
	void OnButtonSelectionChanged(bool bSelected, TWeakObjectPtr<UCommonButtonBase> ButtonWeak);
	void OnGenericButtonEvent(FString EventName, TWeakObjectPtr<UCommonButtonBase> ButtonWeak);

	// Slate Debugging callback for logging Handled events
	void OnSlateInputEvent(const FSlateDebuggingInputEventArgs& EventArgs);

	FOnInputFlowFocusChanged OnFocusChangedDelegate;

	FDelegateHandle FocusChangedHandle;
	TSet<TWeakObjectPtr<UCommonButtonBase>> ObservedButtons;

	TArray<FInputEventLog> EventLog;
	const int32 MaxLogSize = 500;
	int32 WriteIndex = 0;
	bool bWrapped = false;
	

	TWeakPtr<SWidget> LastHoveredWidget;
};