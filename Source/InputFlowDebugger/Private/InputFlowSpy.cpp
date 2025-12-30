// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowSpy.h"
#include "InputFlowSettings.h"

// CommonUI
#if WITH_PLUGIN_COMMONUI
#include <CommonActivatableWidget.h>
#include <CommonButtonBase.h>
#endif

// Editor
#if WITH_EDITOR
#include <Editor.h>
#endif

// Slate
#include <Framework/Application/NavigationConfig.h>
#include <Framework/Application/SlateApplication.h>
#include <Layout/WidgetPath.h>
#include <Slate/SObjectWidget.h>

// UMG
#include <Components/Button.h>

// Internal
#include "InputFlowHelpers.h"
#include "LogInputFlow.h"

FInputFlowSpy::FInputFlowSpy() 
{
	EventLog.SetNum(MaxLogSize); // Pre-allocate for ring buffer

	if (FSlateApplication::IsInitialized())
	{
		FocusChangedHandle = FSlateApplication::Get().OnFocusChanging().AddRaw(this, &FInputFlowSpy::OnFocusChanging);
	}

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::InputEvent.AddRaw(this, &FInputFlowSpy::OnSlateInputEvent);
#endif
}

FInputFlowSpy::~FInputFlowSpy()
{
#if WITH_PLUGIN_COMMONUI
	ObservedButtons.Empty();
#endif

	if (FSlateApplication::IsInitialized() && FocusChangedHandle.IsValid())
	{
		FSlateApplication::Get().OnFocusChanging().Remove(FocusChangedHandle);
		FocusChangedHandle.Reset();
	}

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::InputEvent.RemoveAll(this);
#endif
}

void FInputFlowSpy::ResetBuffer()
{
	WriteIndex = 0;
	bWrapped = false;
}

void FInputFlowSpy::AddLog(const FString& Type, const FString& InputDetails, FColor Color, const FString& WidgetType, const FString& WidgetName, const FString& WidgetState, bool bIsButton, UObject* InSourceObject, const TArray<FInputLogRichTextPart>& InParts)
{
	FInputEventLog& NewLog = EventLog[WriteIndex];
	
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
	NewLog.SourceObject = InSourceObject;
	NewLog.RichTextParts = InParts; // Store the parts
	
	// Store Class for Hyperlink robustness
	if (InSourceObject)
	{
		NewLog.SourceClass = InSourceObject->GetClass();
	}
	else
	{
		NewLog.SourceClass = nullptr;
	}

	// Advance Ring Buffer
	WriteIndex++;
	if (WriteIndex >= MaxLogSize)
	{
		WriteIndex = 0;
		bWrapped = true;
	}

	UE_LOG(LogInputFlow, Verbose, TEXT("[InputFlowDebugger] Log Added: %s %s on %s"), *Type, *InputDetails, *WidgetName);
}

void FInputFlowSpy::GenerateWidgetContextParts(const TSharedPtr<SWidget>& Widget, TArray<FInputLogRichTextPart>& OutParts, FString& OutFlatName) const
{
	if (!Widget.IsValid())
	{
		OutFlatName = TEXT("None");
		OutParts.Add(FInputLogRichTextPart(OutFlatName));
		return;
	}

	UWidget* Owner = InputFlowHelpers::GetOwnerUWidget(Widget);
	FString LeafName;
	
	if (Owner)
	{
		LeafName = Owner->GetName();
	}
	else
	{
		FString Tag = Widget->GetTag().ToString();
		if (!Tag.IsEmpty() && Tag != TEXT("None"))
		{
			LeafName = Tag;
		}
		else
		{
			LeafName = Widget->GetTypeAsString();
		}
	}

	// Add the leaf part
	OutParts.Add(FInputLogRichTextPart(LeafName, Owner, true));
	OutFlatName = LeafName;

	// Walk hierarchy
	TSharedPtr<SWidget> Walker = Widget;
	UUserWidget* ImmediateUserParent = nullptr;
#if WITH_PLUGIN_COMMONUI
	UCommonActivatableWidget* ActivatableParent = nullptr;
#endif // WITH_PLUGIN_COMMONUI

	while (Walker.IsValid())
	{
		Walker = Walker->GetParentWidget();
		if (!Walker.IsValid()) break;

		UWidget* WalkerObj = InputFlowHelpers::GetOwnerUWidget(Walker);
		
		if (IsValid(WalkerObj) && WalkerObj != Owner)
		{
#if WITH_PLUGIN_COMMONUI
			if (UCommonActivatableWidget* FoundActivatable = Cast<UCommonActivatableWidget>(WalkerObj))
			{
				ActivatableParent = FoundActivatable;
				break;
			}
#endif // WITH_PLUGIN_COMMONUI
			
			if (!ImmediateUserParent && WalkerObj->IsA<UUserWidget>())
			{
				ImmediateUserParent = Cast<UUserWidget>(WalkerObj);
			}
		}
	}

	// Build context parts: " (from X in Y)"
#if WITH_PLUGIN_COMMONUI
	if (ImmediateUserParent && ActivatableParent)
	{
		if (ImmediateUserParent == ActivatableParent)
		{
			OutParts.Add(FInputLogRichTextPart(TEXT(" (from ")));
			OutParts.Add(FInputLogRichTextPart(ActivatableParent->GetName(), ActivatableParent, false));
			OutParts.Add(FInputLogRichTextPart(TEXT(")")));
			OutFlatName += FString::Printf(TEXT(" (from %s)"), *ActivatableParent->GetName());
		}
		else
		{
			OutParts.Add(FInputLogRichTextPart(TEXT(" (from ")));
			OutParts.Add(FInputLogRichTextPart(ImmediateUserParent->GetName(), ImmediateUserParent, false));
			OutParts.Add(FInputLogRichTextPart(TEXT(" in ")));
			OutParts.Add(FInputLogRichTextPart(ActivatableParent->GetName(), ActivatableParent, false));
			OutParts.Add(FInputLogRichTextPart(TEXT(")")));
			OutFlatName += FString::Printf(TEXT(" (from %s in %s)"), *ImmediateUserParent->GetName(), *ActivatableParent->GetName());
		}
	}
	else if (ActivatableParent)
	{
		OutParts.Add(FInputLogRichTextPart(TEXT(" (from ")));
		OutParts.Add(FInputLogRichTextPart(ActivatableParent->GetName(), ActivatableParent, false));
		OutParts.Add(FInputLogRichTextPart(TEXT(")")));
		OutFlatName += FString::Printf(TEXT(" (from %s)"), *ActivatableParent->GetName());
		return;
	}
#endif // WITH_PLUGIN_COMMONUI
	if (ImmediateUserParent)
	{
		OutParts.Add(FInputLogRichTextPart(TEXT(" (from ")));
		OutParts.Add(FInputLogRichTextPart(ImmediateUserParent->GetName(), ImmediateUserParent, false));
		OutParts.Add(FInputLogRichTextPart(TEXT(")")));
		OutFlatName += FString::Printf(TEXT(" (from %s)"), *ImmediateUserParent->GetName());
	}
}

FString FInputFlowSpy::GetWidgetDisplayName(const TSharedPtr<SWidget>& Widget) const
{
	if (!Widget.IsValid()) return TEXT("None");

	FString DisplayName;
	const UWidget* Owner = InputFlowHelpers::GetOwnerUWidget(Widget);
	
	if (IsValid(Owner))
	{
		DisplayName = Owner->GetName();
	}
	else
	{
		FString Tag = Widget->GetTag().ToString();
		if (!Tag.IsEmpty() && Tag != TEXT("None"))
			DisplayName = Tag;
		else
			DisplayName = Widget->GetTypeAsString();
	}

	TSharedPtr<SWidget> Walker = Widget;

	const UUserWidget* ImmediateUserParent = nullptr;
#if WITH_PLUGIN_COMMONUI
	const UCommonActivatableWidget* ActivatableParent = nullptr;
#endif // WITH_PLUGIN_COMMONUI

	while (Walker.IsValid())
	{
		Walker = Walker->GetParentWidget();
		if (!Walker.IsValid()) break;

		UWidget* WalkerObj = InputFlowHelpers::GetOwnerUWidget(Walker);
		
		if (WalkerObj && WalkerObj != Owner)
		{
#if WITH_PLUGIN_COMMONUI
			if (UCommonActivatableWidget* FoundActivatable = Cast<UCommonActivatableWidget>(WalkerObj))
			{
				ActivatableParent = FoundActivatable;
				break;
			}
#endif // WITH_PLUGIN_COMMONUI 
			
			if (!ImmediateUserParent && WalkerObj->IsA<UUserWidget>())
			{
				ImmediateUserParent = Cast<UUserWidget>(WalkerObj);
			}
		}
	}
#if WITH_PLUGIN_COMMONUI
	if (ImmediateUserParent && ActivatableParent)
	{
		if (ImmediateUserParent == ActivatableParent)
		{
			DisplayName += FString::Printf(TEXT(" (from %s)"), *ActivatableParent->GetName());
		}
		else
		{
			DisplayName += FString::Printf(TEXT(" (from %s in %s)"), *ImmediateUserParent->GetName(), *ActivatableParent->GetName());
		}
	}
	else if (ActivatableParent)
	{
		DisplayName += FString::Printf(TEXT(" (from %s)"), *ActivatableParent->GetName());
		return DisplayName;
	}
#endif // WITH_PLUGIN_COMMONUI
	if (ImmediateUserParent)
	{
		DisplayName += FString::Printf(TEXT(" (from %s)"), *ImmediateUserParent->GetName());
	}

	return DisplayName;
}

TSharedPtr<SWidget> FInputFlowSpy::FindInterestingWidget(FSlateApplication& SlateApp, const FVector2D& CursorPos) const
{
	FWidgetPath WidgetsUnderCursor = SlateApp.LocateWindowUnderMouse(CursorPos, SlateApp.GetInteractiveTopLevelWindows());
	
	if (WidgetsUnderCursor.IsValid() && WidgetsUnderCursor.Widgets.Num() > 0)
	{
		if (!InputFlowHelpers::IsGameWorldWidget(WidgetsUnderCursor.Widgets.Last().Widget))
		{
			return nullptr;
		}

		for (int32 i = WidgetsUnderCursor.Widgets.Num() - 1; i >= 0; --i)
		{
			TSharedPtr<SWidget> SlateWidget = WidgetsUnderCursor.Widgets[i].Widget;
			if (!SlateWidget.IsValid()) continue;

			if (const UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(SlateWidget))
			{
				if (!IsValid(UObj) || UObj->IsUnreachable()) continue;

#if WITH_PLUGIN_COMMONUI
				if (UObj->IsA<UCommonButtonInternalBase>())
				{
					continue;
				}

				if (UObj->IsA<UCommonButtonBase>())
				{
					return SlateWidget;
				}
#endif // WITH_PLUGIN_COMMONUI
				if (UObj->IsA<UButton>())
				{
					return SlateWidget;
				}
			}

			FString Type = SlateWidget->GetTypeAsString();
			if (Type.Contains("SButton") || Type.Contains("SCheckBox") || Type.Contains("SEditableText"))
			{
				return SlateWidget;
			}
		}

		TSharedPtr<SWidget> LeafWidget = WidgetsUnderCursor.Widgets.Last().Widget;
		return LeafWidget;
	}

	return nullptr;
}

FString FInputFlowSpy::GetWidgetStateDescription(const TSharedPtr<SWidget>& Widget) const
{
	if (!Widget.IsValid()) return TEXT("Invalid");

	TArray<FString> States;
	States.Add(TEXT("States:"));

	if (!Widget->IsEnabled()) States.Add(TEXT("DISABLED"));
	if (Widget->HasUserFocus(0)) States.Add(TEXT("FOCUSED"));
	if (Widget->IsHovered()) States.Add(TEXT("HOVERED"));

	EVisibility Vis = Widget->GetVisibility();
	if (Vis != EVisibility::Visible)
	{
		States.Add(FString::Printf(TEXT("[%s]"), *Vis.ToString()));
	}

	UWidget* AssociatedUWidget = nullptr;
	if (Widget->GetTypeAsString() == TEXT("SObjectWidget"))
	{
		TSharedPtr<SObjectWidget> ObjectWidget = StaticCastSharedPtr<SObjectWidget>(Widget);
		if (ObjectWidget.IsValid()) AssociatedUWidget = ObjectWidget->GetWidgetObject();
	}
	
	if (!AssociatedUWidget)
	{
		TSharedPtr<FReflectionMetaData> MetaData = Widget->GetMetaData<FReflectionMetaData>();
		if (MetaData.IsValid()) AssociatedUWidget = Cast<UWidget>(MetaData->SourceObject.Get());
	}
#if WITH_PLUGIN_COMMONUI
	if (AssociatedUWidget)
	{
		if (UCommonButtonBase* CommonBtn = Cast<UCommonButtonBase>(AssociatedUWidget))
		{
			if (CommonBtn->GetSelected()) States.Add(TEXT("SELECTED"));
			if (!CommonBtn->IsInteractionEnabled()) States.Add(TEXT("NOT INTERACTABLE"));
			if (CommonBtn->GetLocked()) States.Add(TEXT("LOCKED"));
		}

		if (UCommonActivatableWidget* Activatable = Cast<UCommonActivatableWidget>(AssociatedUWidget))
		{
			if (Activatable->IsActivated())
			{
				States.Add(TEXT("[ACTIVE]"));
			}
			else
			{
				States.Add(TEXT("[DEACTIVATED/POOLED]"));
			}
		}
	}
#endif // WITH_PLUGIN_COMMONUI

	if (States.Num() == 1) return TEXT("Normal");
	
	return FString::Join(States, TEXT(" | "));
}

// --- Dynamic Observation ---

void FInputFlowSpy::BindButtonObservation(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid()) return;
	if (!InputFlowHelpers::IsGameWorldWidget(Widget)) return;

	UWidget* Owner = InputFlowHelpers::GetOwnerUWidget(Widget);
#if WITH_PLUGIN_COMMONUI
	if (UCommonButtonBase* Btn = Cast<UCommonButtonBase>(Owner))
	{
		// Check if we already observe this button
		if (!ObservedButtons.Contains(Btn))
		{
			ObservedButtons.Add(Btn);

			TWeakObjectPtr<UCommonButtonBase> WeakBtn = Btn;
			
			// Selection Event
			Btn->OnIsSelectedChanged().AddSP(this, &FInputFlowSpy::OnButtonSelectionChanged, WeakBtn);

			// Logic Events
			Btn->OnClicked().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("Clicked")), WeakBtn);
			Btn->OnDoubleClicked().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("DoubleClicked")), WeakBtn);
			Btn->OnPressed().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("Pressed")), WeakBtn);
			Btn->OnReleased().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("Released")), WeakBtn);
			Btn->OnHovered().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("Hovered")), WeakBtn);
			Btn->OnUnhovered().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("Unhovered")), WeakBtn);
			Btn->OnFocusReceived().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("FocusReceived")), WeakBtn);
			Btn->OnFocusLost().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("FocusLost")), WeakBtn);
			Btn->OnLockClicked().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("LockClicked")), WeakBtn);
			Btn->OnLockDoubleClicked().AddSP(this, &FInputFlowSpy::OnCommonUIButtonEvent, FString(TEXT("LockDoubleClicked")), WeakBtn);
		}
	}
#endif // WITH_PLUGIN_COMMONUI
	// TODO: Add support for UButton
	/*if (UButton* Btn = Cast<UButton>(Owner))
	{
		Btn->OnClicked.AddUniqueDynamic(this, &FInputFlowSpy::OnUButtonClicked);
	}*/
}
#if WITH_PLUGIN_COMMONUI
void FInputFlowSpy::OnButtonSelectionChanged(bool bSelected, TWeakObjectPtr<UCommonButtonBase> ButtonWeak)
{
	if (UCommonButtonBase* Btn = ButtonWeak.Get())
	{
		TSharedPtr<SWidget> CachedWidget = Btn->GetCachedWidget();
		if (!CachedWidget.IsValid()) return;

		TArray<FInputLogRichTextPart> Parts;
		FString Name;
		GenerateWidgetContextParts(CachedWidget, Parts, Name);

		FString State = GetWidgetStateDescription(CachedWidget);
		FString Type = Btn->GetClass()->GetName();

		AddLog("CommonUI | Button Selection", bSelected ? TEXT("Selected") : TEXT("Deselected"), FColor(0, 150, 255), Type, Name, State, true, Btn, Parts);
	}
}

void FInputFlowSpy::OnCommonUIButtonEvent(FString EventName, TWeakObjectPtr<UCommonButtonBase> ButtonWeak)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();

	if (EventName == TEXT("Hovered") || EventName == TEXT("Unhovered"))
	{
		if (!Settings->IsCaptureHoverEnabled()) return;
	}
	
	bool bIsClick = (EventName == TEXT("Pressed") || EventName == TEXT("Released") || EventName == TEXT("Clicked") || EventName == TEXT("DoubleClicked"));
	if (bIsClick && !Settings->IsCaptureClicksEnabled())
	{
		return;
	}

	bool bIsFocus = (EventName == TEXT("FocusReceived") || EventName == TEXT("FocusLost"));
	if (bIsFocus && !Settings->IsCaptureFocusEnabled())
	{
		return;
	}
	if (UCommonButtonBase* Btn = ButtonWeak.Get())
	{
		TSharedPtr<SWidget> CachedWidget = Btn->GetCachedWidget();
		if (!CachedWidget.IsValid()) return;

		TArray<FInputLogRichTextPart> Parts;
		FString Name;
		GenerateWidgetContextParts(CachedWidget, Parts, Name);

		FString State = GetWidgetStateDescription(CachedWidget);
		FString WidgetType = Btn->GetClass()->GetName();

		AddLog(FString::Printf(TEXT("CommonUI | Button %s"), *EventName), "", FColor::Cyan, WidgetType, Name, State, true, Btn, Parts);
	}
}
#endif // WITH_PLUGIN_COMMONUI

void FInputFlowSpy::OnSlateInputEvent(const FSlateDebuggingInputEventArgs& EventArgs)
{
#if WITH_SLATE_DEBUGGING
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (!Settings->GetShowHandledEventsEnabled()) return;

	// We only care about events that were actually CONSUMED (Handled)
	if (!EventArgs.Reply.IsEventHandled())
	{
		return;
	}

	// Filter based on configuration booleans
	const ESlateDebuggingInputEvent EventType = EventArgs.InputEventType;
	FString InputDetail;
	FString Modifiers;
	
	// -- Key Events --
	if (EventType == ESlateDebuggingInputEvent::KeyDown || EventType == ESlateDebuggingInputEvent::KeyUp)
	{
		if (!Settings->IsCaptureKeyEventsEnabled()) return;

		const FKeyEvent* KeyEvent = static_cast<const FKeyEvent*>(EventArgs.InputEvent);
		InputDetail = KeyEvent->GetKey().ToString();

		if (KeyEvent->IsControlDown()) Modifiers += TEXT("Ctrl+");
		if (KeyEvent->IsShiftDown()) Modifiers += TEXT("Shift+");
		if (KeyEvent->IsAltDown()) Modifiers += TEXT("Alt+");
		if (KeyEvent->IsCommandDown()) Modifiers += TEXT("Cmd+");
	}
	// -- Mouse Clicks & Wheel --
	else if (EventType == ESlateDebuggingInputEvent::MouseButtonDown || 
			 EventType == ESlateDebuggingInputEvent::MouseButtonUp || 
			 EventType == ESlateDebuggingInputEvent::MouseButtonDoubleClick ||
			 EventType == ESlateDebuggingInputEvent::MouseWheel)
	{
		if (!Settings->IsCaptureClicksEnabled()) return;

		const FPointerEvent* PointerEvent = static_cast<const FPointerEvent*>(EventArgs.InputEvent);
		
		if (EventType == ESlateDebuggingInputEvent::MouseWheel)
		{
			InputDetail = FString::Printf(TEXT("Wheel (%+.1f)"), PointerEvent->GetWheelDelta());
		}
		else
		{
			InputDetail = PointerEvent->GetEffectingButton().ToString();
		}

		// Mouse events also have modifiers
		if (PointerEvent->IsControlDown()) Modifiers += TEXT("Ctrl+");
		if (PointerEvent->IsShiftDown()) Modifiers += TEXT("Shift+");
		if (PointerEvent->IsAltDown()) Modifiers += TEXT("Alt+");
	}
	// -- Analog --
	else if (EventType == ESlateDebuggingInputEvent::AnalogInput)
	{
		if (!Settings->IsCaptureAnalogEnabled()) return;
		
		const FAnalogInputEvent* AnalogEvent = static_cast<const FAnalogInputEvent*>(EventArgs.InputEvent);
		if (FMath::Abs(AnalogEvent->GetAnalogValue()) < 0.15f) return;
		
		InputDetail = FString::Printf(TEXT("%s (%.2f)"), *AnalogEvent->GetKey().ToString(), AnalogEvent->GetAnalogValue());
	}
	else
	{
		return;
	}

	// Prepend modifiers to detail (e.g., "Ctrl+S" instead of "S")
	if (!Modifiers.IsEmpty())
	{
		InputDetail = Modifiers + InputDetail;
	}

	// Check for Reply Side Effects (Capture/Focus)
	FString SideEffects;
	if (EventArgs.Reply.GetMouseCaptor().IsValid())
	{
		SideEffects += TEXT(" [Captures Mouse]");
	}
	if (EventArgs.Reply.ShouldSetUserFocus())
	{
		SideEffects += TEXT(" [Sets Focus]");
	}
	// Slate often passes extra info in AdditionalContent
	if (!EventArgs.AdditionalContent.IsEmpty())
	{
		SideEffects += FString::Printf(TEXT(" (%s)"), *EventArgs.AdditionalContent);
	}

	// Log the Consumption
	const TSharedPtr<SWidget> Handler = EventArgs.HandlerWidget;
	if (Handler.IsValid())
	{
		TArray<FInputLogRichTextPart> Parts;
		FString Name;
		
		GenerateWidgetContextParts(Handler, Parts, Name);
		
		FString WidgetType = Handler->GetTypeAsString();
		if (UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(Handler))
		{
			WidgetType = UObj->GetClass()->GetName();
		}

		// Combine detail with side effects
		FString FinalDetail = InputDetail + SideEffects;

		AddLog(
			TEXT("Slate | Handled Event"), 
			FinalDetail, 
			FColor::Emerald, 
			WidgetType, 
			Name, 
			TEXT("CONSUMED"), 
			InputFlowHelpers::IsButtonWidget(Handler), 
			InputFlowHelpers::GetOwnerUWidget(Handler), 
			Parts
		);
	}
#endif
}

// ---------------------------

bool FInputFlowSpy::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (!Settings->IsCaptureClicksEnabled()) return false;

	FWidgetPath WidgetsUnderCursor = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
	if (WidgetsUnderCursor.IsValid() && WidgetsUnderCursor.Widgets.Num() > 0)
	{
		if (!InputFlowHelpers::IsGameWorldWidget(WidgetsUnderCursor.Widgets.Last().Widget)) return false;
	}

	const TSharedPtr<SWidget> Target = FindInterestingWidget(SlateApp, MouseEvent.GetScreenSpacePosition());
	
	if (Target.IsValid()) BindButtonObservation(Target);

	FString InputDetail = MouseEvent.GetEffectingButton().ToString();
	FString WName, WType, WState;
	bool bIsButton = false;
	UObject* SourceObj = nullptr;
	TArray<FInputLogRichTextPart> Parts;

	if (Target.IsValid())
	{
		GenerateWidgetContextParts(Target, Parts, WName);
		WState = GetWidgetStateDescription(Target);
		UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(Target);
		SourceObj = UObj;
		WType = UObj ? UObj->GetClass()->GetName() : Target->GetTypeAsString();
		bIsButton = InputFlowHelpers::IsButtonWidget(Target);
	}
	else
	{
		WName = TEXT("(None)");
	}

	AddLog("Slate | Mouse Down", InputDetail, FColor::Orange, WType, WName, WState, bIsButton, SourceObj, Parts);
	return false;
}

bool FInputFlowSpy::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (!Settings->IsCaptureClicksEnabled()) return false;
	FWidgetPath WidgetsUnderCursor = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
	if (WidgetsUnderCursor.IsValid() && WidgetsUnderCursor.Widgets.Num() > 0)
	{
		if (!InputFlowHelpers::IsGameWorldWidget(WidgetsUnderCursor.Widgets.Last().Widget)) return false;
	}
	
	AddLog("Slate | Mouse Up", MouseEvent.GetEffectingButton().ToString(), FColor::Yellow);
	return false;
}

bool FInputFlowSpy::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (!Settings->IsCaptureClicksEnabled()) return false;
	FWidgetPath WidgetsUnderCursor = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
	if (WidgetsUnderCursor.IsValid() && WidgetsUnderCursor.Widgets.Num() > 0)
	{
		if (!InputFlowHelpers::IsGameWorldWidget(WidgetsUnderCursor.Widgets.Last().Widget)) return false;
	}

	AddLog("Slate | DblClick", MouseEvent.GetEffectingButton().ToString(), FColor::Orange);
	return false;
}

bool FInputFlowSpy::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	const bool bCaptureMouseMove = Settings->IsCaptureMouseMoveEnabled();
	const bool bCaptureHover = Settings->IsCaptureHoverEnabled();
	if (!Settings->IsCaptureMouseMoveEnabled() && !Settings->IsCaptureHoverEnabled()) return false;

	FWidgetPath WidgetsUnderCursor = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
	if (WidgetsUnderCursor.IsValid() && WidgetsUnderCursor.Widgets.Num() > 0)
	{
		if (!InputFlowHelpers::IsGameWorldWidget(WidgetsUnderCursor.Widgets.Last().Widget)) return false;
	}

	if (bCaptureMouseMove)
	{
		static double LastMouseLogTime = 0.0;
		if (FPlatformTime::Seconds() - LastMouseLogTime > 0.1)
		{
			AddLog("Slate | MouseMove", MouseEvent.GetCursorDelta().ToString(), FColor::White);
			LastMouseLogTime = FPlatformTime::Seconds();
		}
	}

	if (bCaptureHover)
	{
		TSharedPtr<SWidget> CurrentHovered = FindInterestingWidget(SlateApp, MouseEvent.GetScreenSpacePosition());
		if (CurrentHovered.IsValid()) BindButtonObservation(CurrentHovered);

		TSharedPtr<SWidget> PreviousHovered = LastHoveredWidget.Pin();

		if (CurrentHovered != PreviousHovered)
		{
			if (PreviousHovered.IsValid() && InputFlowHelpers::IsGameWorldWidget(PreviousHovered))
			{
				TArray<FInputLogRichTextPart> Parts;
				FString Name;
				GenerateWidgetContextParts(PreviousHovered, Parts, Name);

				UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(PreviousHovered);
				FString TypePrefix = UObj ? UObj->GetClass()->GetName() : PreviousHovered->GetTypeAsString();
				bool bIsButton = InputFlowHelpers::IsButtonWidget(PreviousHovered);
				
				AddLog("Slate | Hover Leave", "", FColor(180, 180, 180), TypePrefix, Name, "", bIsButton, UObj, Parts);
			}

			if (CurrentHovered.IsValid())
			{
				TArray<FInputLogRichTextPart> Parts;
				FString Name;
				GenerateWidgetContextParts(CurrentHovered, Parts, Name);

				FString StateDesc = GetWidgetStateDescription(CurrentHovered);
				UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(CurrentHovered);
				FString TypePrefix = UObj ? UObj->GetClass()->GetName() : CurrentHovered->GetTypeAsString();
				bool bIsButton = InputFlowHelpers::IsButtonWidget(CurrentHovered);

				AddLog("Slate | Hover Enter", "", FColor(200, 160, 255), TypePrefix, Name, StateDesc, bIsButton, UObj, Parts);
			}
			
			LastHoveredWidget = CurrentHovered;
		}
	}

	return false;
}

bool FInputFlowSpy::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	const bool bCaptureKeyEvents = Settings->IsCaptureKeyEventsEnabled();
	const bool bCaptureFocusEvents = Settings->IsCaptureFocusEnabled();
	if (!bCaptureKeyEvents)
	{
		return false;
	}
	TSharedPtr<SWidget> FocusedWidget = SlateApp.GetKeyboardFocusedWidget();
	if (FocusedWidget.IsValid() && !InputFlowHelpers::IsGameWorldWidget(FocusedWidget)) return false;
#if WITH_EDITOR
	if (!FocusedWidget.IsValid()) return false;
#endif

	if (FocusedWidget.IsValid()) 
	{
		BindButtonObservation(FocusedWidget);
		FString SourceName = TEXT("None");
		FString SourceType = TEXT("");
		FString SourceState = TEXT("");
		TArray<FInputLogRichTextPart> Parts;
		GenerateWidgetContextParts(FocusedWidget, Parts, SourceName);
		UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(FocusedWidget);
		UObject* SourceObj = UObj;
		SourceType = UObj ? UObj->GetClass()->GetName() : FocusedWidget->GetTypeAsString();
		bool bIsBtn = InputFlowHelpers::IsButtonWidget(FocusedWidget);
		SourceState = GetWidgetStateDescription(FocusedWidget);

		AddLog("Slate | KeyDown", InKeyEvent.GetKey().ToString(), FColor::Green, SourceType, SourceName, SourceState, bIsBtn, SourceObj, Parts);
	}
	else
	{
		AddLog("Slate | KeyDown", InKeyEvent.GetKey().ToString(), FColor::Green);
	}
	
	if (bCaptureFocusEvents)
	{
		EUINavigation NavDir = SlateApp.GetNavigationConfig()->GetNavigationDirectionFromKey(InKeyEvent);

		if (NavDir != EUINavigation::Invalid)
		{
			FString SourceName = TEXT("None");
			FString SourceType = TEXT("");
			FString SourceState = TEXT("");
			bool bIsBtn = false;
			UObject* SourceObj = nullptr;
			TArray<FInputLogRichTextPart> Parts;

			if (FocusedWidget.IsValid())
			{
				GenerateWidgetContextParts(FocusedWidget, Parts, SourceName);
				UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(FocusedWidget);
				SourceObj = UObj;
				SourceType = UObj ? UObj->GetClass()->GetName() : FocusedWidget->GetTypeAsString();
				bIsBtn = InputFlowHelpers::IsButtonWidget(FocusedWidget);
				SourceState = GetWidgetStateDescription(FocusedWidget);
			}

			AddLog("Slate | Nav Attempt", GetNavigationDirectionString(NavDir), FColor::Magenta, SourceType, SourceName, SourceState, bIsBtn, SourceObj, Parts);
		}
	}
	return false; 
}

bool FInputFlowSpy::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (!Settings->IsCaptureKeyEventsEnabled()) return false;

	const TSharedPtr<SWidget> FocusedWidget = SlateApp.GetKeyboardFocusedWidget();
	if (FocusedWidget.IsValid() && !InputFlowHelpers::IsGameWorldWidget(FocusedWidget)) return false;
#if WITH_EDITOR
	if (!FocusedWidget.IsValid()) return false;
#endif

	if (FocusedWidget.IsValid())
	{
		FString SourceName = TEXT("None");
		FString SourceType = TEXT("");
		FString SourceState = TEXT("");
		bool bIsBtn = false;
		UObject* SourceObj = nullptr;
		TArray<FInputLogRichTextPart> Parts;
		GenerateWidgetContextParts(FocusedWidget, Parts, SourceName);
		UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(FocusedWidget);
		SourceObj = UObj;
		SourceType = UObj ? UObj->GetClass()->GetName() : FocusedWidget->GetTypeAsString();
		bIsBtn = InputFlowHelpers::IsButtonWidget(FocusedWidget);
		SourceState = GetWidgetStateDescription(FocusedWidget);
		AddLog("Slate | KeyUp", InKeyEvent.GetKey().ToString(), FColor::Red, SourceType, SourceName, SourceState, bIsBtn, SourceObj, Parts);
	}
	else
	{
		AddLog("Slate | KeyUp", InKeyEvent.GetKey().ToString(), FColor::Red);
	}

	return false; 
}

bool FInputFlowSpy::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	if (!Settings->IsCaptureAnalogEnabled()) return false;
	TSharedPtr<SWidget> FocusedWidget = SlateApp.GetKeyboardFocusedWidget();
	if (FocusedWidget.IsValid() && !InputFlowHelpers::IsGameWorldWidget(FocusedWidget)) return false;
#if WITH_EDITOR
	if (!FocusedWidget.IsValid()) return false;
#endif

	if (FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.15f)
	{
		static double LastLogTime = 0.0;
		if (FPlatformTime::Seconds() - LastLogTime > 0.1)
		{
			AddLog("Slate | Analog", FString::Printf(TEXT("%s : %.2f"), *InAnalogInputEvent.GetKey().ToString(), InAnalogInputEvent.GetAnalogValue()), FColor::Cyan);
			LastLogTime = FPlatformTime::Seconds();
		}
	}
	return false;
}

void FInputFlowSpy::OnFocusChanging(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldPath,
	const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& InNewPath,
	const TSharedPtr<SWidget>& NewFocusedWidget)
{
	if (!NewFocusedWidget.IsValid() || !InputFlowHelpers::IsGameWorldWidget(NewFocusedWidget))
	{
		return;
	}
	const UInputFlowSettings* Settings = UInputFlowSettings::Get();
	const bool bCaptureFocusEvents = Settings->IsCaptureFocusEnabled();
	const bool bCaptureHover = Settings->IsCaptureHoverEnabled();

	BindButtonObservation(NewFocusedWidget);
	OnFocusChangedDelegate.Broadcast(NewFocusedWidget, InFocusEvent);

	if (bCaptureHover && InFocusEvent.GetCause() == EFocusCause::Navigation)
	{
		TSharedPtr<SWidget> PreviousHovered = LastHoveredWidget.Pin();
		
		if (PreviousHovered.IsValid() && PreviousHovered != NewFocusedWidget)
		{
			if (InputFlowHelpers::IsGameWorldWidget(PreviousHovered))
			{
				FString Name = GetWidgetDisplayName(PreviousHovered);
				UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(PreviousHovered);
				FString TypePrefix = UObj ? UObj->GetClass()->GetName() : PreviousHovered->GetTypeAsString();
				bool bIsButton = InputFlowHelpers::IsButtonWidget(PreviousHovered);
				
				AddLog("Slate | Hover Leave", "Via Navigation", FColor(200, 160, 255), TypePrefix, Name, "", bIsButton, UObj);
			}
		}

		if (NewFocusedWidget.IsValid())
		{
			FString Name = GetWidgetDisplayName(NewFocusedWidget);
			FString StateDesc = GetWidgetStateDescription(NewFocusedWidget);
			UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(NewFocusedWidget);
			FString TypePrefix = UObj ? UObj->GetClass()->GetName() : NewFocusedWidget->GetTypeAsString();
			bool bIsButton = InputFlowHelpers::IsButtonWidget(NewFocusedWidget);

			AddLog("Slate | Hover Enter", "Via Navigation", FColor(200, 160, 255), TypePrefix, Name, StateDesc, bIsButton, UObj);
			
			LastHoveredWidget = NewFocusedWidget;
		}
		else
		{
			LastHoveredWidget.Reset();
		}
	}

	if (!bCaptureFocusEvents) return;

	FString CauseStr;
	switch(InFocusEvent.GetCause())
	{
	case EFocusCause::Mouse: CauseStr = TEXT("Mouse"); break;
	case EFocusCause::Navigation: CauseStr = TEXT("Navigation"); break;
	case EFocusCause::SetDirectly: CauseStr = TEXT("SetDirectly"); break;
	case EFocusCause::Cleared: CauseStr = TEXT("Cleared"); break;
	case EFocusCause::OtherWidgetLostFocus: CauseStr = TEXT("WidgetLostFocus"); break;
	case EFocusCause::WindowActivate: CauseStr = TEXT("WindowActivation"); break;
	default: CauseStr = TEXT("Unknown"); break;
	}

	FString SrcName, SrcType, SrcState;
	bool bIsSrcBtn = false;
	UObject* SrcObj = nullptr;
	TArray<FInputLogRichTextPart> SrcParts;

	if (OldFocusedWidget.IsValid())
	{
		GenerateWidgetContextParts(OldFocusedWidget, SrcParts, SrcName);
		SrcState = GetWidgetStateDescription(OldFocusedWidget);
		UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(OldFocusedWidget);
		SrcObj = UObj;
		SrcType = UObj ? UObj->GetClass()->GetName() : OldFocusedWidget->GetTypeAsString();
		bIsSrcBtn = InputFlowHelpers::IsButtonWidget(OldFocusedWidget);
	}

	FString DestName, DestType, DestState;
	bool bIsDestBtn = false;
	UObject* DestObj = nullptr;
	TArray<FInputLogRichTextPart> DestParts;

	if (NewFocusedWidget.IsValid())
	{
		GenerateWidgetContextParts(NewFocusedWidget, DestParts, DestName);
		DestState = GetWidgetStateDescription(NewFocusedWidget);
		UWidget* UObj = InputFlowHelpers::GetOwnerUWidget(NewFocusedWidget);
		DestObj = UObj;
		DestType = UObj ? UObj->GetClass()->GetName() : NewFocusedWidget->GetTypeAsString();
		bIsDestBtn = InputFlowHelpers::IsButtonWidget(NewFocusedWidget);
	}

	FColor LogColor = (InFocusEvent.GetCause() == EFocusCause::Navigation) ? FColor::Green : FColor(100, 150, 255);
	if (OldFocusedWidget.IsValid())
	{
		AddLog("Slate | Focus Lost", CauseStr, LogColor, SrcType, SrcName, SrcState, bIsSrcBtn, SrcObj, SrcParts);
	}
	AddLog("Slate | Focus Gained", CauseStr, LogColor, DestType, DestName, DestState, bIsDestBtn, DestObj, DestParts);
}

FString FInputFlowSpy::GetNavigationDirectionString(EUINavigation NavDir) const
{
	switch(NavDir)
	{
	case EUINavigation::Left: return TEXT("Left");
	case EUINavigation::Right: return TEXT("Right");
	case EUINavigation::Up: return TEXT("Up");
	case EUINavigation::Down: return TEXT("Down");
	case EUINavigation::Next: return TEXT("Next");
	case EUINavigation::Previous: return TEXT("Previous");
	default: return TEXT("Invalid");
	}
}