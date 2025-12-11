// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowHelpers.h"

// CommonUI
#include <CommonActivatableWidget.h>
#include <CommonButtonBase.h>

// Core
#include <InputTriggers.h>

// Developer
#include <SourceCodeNavigation.h>

// Editor
#if WITH_EDITOR
#include <Blueprint/WidgetTree.h>
#include <Editor.h>
#include <Subsystems/AssetEditorSubsystem.h>
#include <WidgetBlueprint.h>
#include <WidgetBlueprintEditor.h>
#include <WidgetReference.h>
#endif

// Engine
#include <Engine/Engine.h>
#include <Engine/GameInstance.h>

// Slate
#include <Slate/SObjectWidget.h>
#include <Styling/AppStyle.h>
#include <Widgets/SViewport.h>

// UMG
#include <Blueprint/UserWidget.h>
#include <Components/Button.h>
#include <Components/Widget.h>

// Internal
#include "InputDebugSubsystem.h"

const FName InputFlowHelpers::InputFlowAnalyzerTag("InputFlowAnalyzerTag");

const FSlateBrush* InputFlowHelpers::GetBackgroundBrush(bool bIsOverlay)
{
	if (bIsOverlay)
	{
		return FCoreStyle::Get().GetBrush("NoBrush");
	}
	return FAppStyle::GetBrush("ToolPanel.GroupBorder");
}

UInputDebugSubsystem* InputFlowHelpers::GetActiveDebugSubsystem()
{
#if WITH_EDITOR
	if (IsValid(GEditor) && IsValid(GEditor->PlayWorld) && IsValid(GEditor->PlayWorld->GetGameInstance()))
	{
		return GEditor->PlayWorld->GetGameInstance()->GetSubsystem<UInputDebugSubsystem>();
	}
#endif
	if (UWorld* World = GEngine->GetWorldFromContextObject(GetTransientPackage(), EGetWorldErrorMode::ReturnNull))
	{
		if (World->IsGameWorld() && IsValid(World->GetGameInstance()))
		{
			return World->GetGameInstance()->GetSubsystem<UInputDebugSubsystem>();
		}
	}
	return nullptr;
}

FString InputFlowHelpers::GetWidgetDisplayName(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid()) return TEXT("None");

	FString DisplayName;
	UWidget* Owner = GetOwnerUWidget(Widget); // Reuse existing helper
	
	// 1. Get Leaf Name
	if (Owner)
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

	// 2. Find Context (Parent UserWidget / ActivatableWidget)
	TSharedPtr<SWidget> Walker = Widget;
	UUserWidget* ImmediateUserParent = nullptr;
	UCommonActivatableWidget* ActivatableParent = nullptr;

	while (Walker.IsValid())
	{
		Walker = Walker->GetParentWidget();
		if (!Walker.IsValid()) break;

		UWidget* WalkerObj = GetOwnerUWidget(Walker);
		
		if (WalkerObj && WalkerObj != Owner)
		{
			if (UCommonActivatableWidget* FoundActivatable = Cast<UCommonActivatableWidget>(WalkerObj))
			{
				ActivatableParent = FoundActivatable;
				break;
			}
			
			if (!ImmediateUserParent && WalkerObj->IsA<UUserWidget>())
			{
				ImmediateUserParent = Cast<UUserWidget>(WalkerObj);
			}
		}
	}

	// 3. Append Context
	if (ImmediateUserParent && ActivatableParent)
	{
		if (ImmediateUserParent == ActivatableParent)
		{
			DisplayName += FString::Printf(TEXT("\n(from %s)"), *ActivatableParent->GetName());
		}
		else
		{
			DisplayName += FString::Printf(TEXT("\n(from %s in %s)"), *ImmediateUserParent->GetName(), *ActivatableParent->GetName());
		}
	}
	else if (ActivatableParent)
	{
		DisplayName += FString::Printf(TEXT("\n(from %s)"), *ActivatableParent->GetName());
	}
	else if (ImmediateUserParent)
	{
		DisplayName += FString::Printf(TEXT("\n(from %s)"), *ImmediateUserParent->GetName());
	}

	return DisplayName;
}

void InputFlowHelpers::TryOpenAsset(const TWeakObjectPtr<UObject>& ObjectWeak, const TWeakObjectPtr<UClass>& ClassWeak, bool bFindRootContext)
{
#if WITH_EDITOR
	UObject* AssetToOpen = nullptr;
	UObject* Instance = ObjectWeak.Get();
	UObject* OriginalObject = Instance;

	if (Instance)
	{
		// Case A: Find Container (Leaf Click)
		// We want to find the UserWidget that CONTAINS this instance.
		// We start searching from the Outer to skip the instance itself.
		// We stop at the FIRST UserWidget we find (Immediate Parent).
		if (bFindRootContext)
		{
			UObject* Current = Instance->GetOuter();
			while (Current)
			{
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Current))
				{
					Instance = UserWidget;
					break; // Found immediate container. Stop.
				}
				if (Current->IsA<UWorld>() || Current->IsA<ULevel>()) break;
				Current = Current->GetOuter();
			}
		}
		// Case B: Open Definition (Context Link)
		// We want to open this specific instance.
		// However, if it is a raw Native Component (e.g. UButton), we can't open it directly.
		// We must find its owner UserWidget.
		else if (Instance->IsA<UWidget>() && !Instance->IsA<UUserWidget>())
		{
			UObject* Current = Instance->GetOuter();
			while (Current)
			{
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Current))
				{
					Instance = UserWidget;
					break;
				}
				if (Current->IsA<UWorld>() || Current->IsA<ULevel>()) break;
				Current = Current->GetOuter();
			}
		}
	}

	// 1. Try to get class from instance (potentially updated above) if available, otherwise use backup class
	const UClass* ClassToInspect = Instance ? Instance->GetClass() : ClassWeak.Get();

	if (IsValid(ClassToInspect))
	{
		// 2. Check for Blueprint Generated Class
		if (ClassToInspect->ClassGeneratedBy)
		{
			AssetToOpen = ClassToInspect->ClassGeneratedBy;
		}
		else
		{
			// 3. Native C++ Class - Open Source
			if (FSourceCodeNavigation::NavigateToClass(ClassToInspect))
			{
				return;
			}
		}
	}

	// 4. Fallback: If we still have an instance and it's a specific UWidget type logic
	if (!AssetToOpen && Instance)
	{
		// Walk up outer chain (e.g. if we clicked a slot widget)
		UObject* Current = Instance;
		while (Current)
		{
			if (UUserWidget* UserWidget = Cast<UUserWidget>(Current))
			{
				if (UClass* UWClass = UserWidget->GetClass())
				{
					if (UWClass->ClassGeneratedBy)
					{
						AssetToOpen = UWClass->ClassGeneratedBy;
						break;
					}
				}
			}
			if (Current->IsA<UWorld>()) break;
			Current = Current->GetOuter();
		}
	}

	if (AssetToOpen && GEditor)
	{
		if (UAssetEditorSubsystem* AssetSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			if (AssetSub->OpenEditorForAsset(AssetToOpen))
			{
				// Only attempt selection if we performed a hierarchy walk (Leaf Click)
				// and the Asset is a Widget Blueprint.
				if (bFindRootContext && OriginalObject && OriginalObject != Instance && AssetToOpen->IsA<UWidgetBlueprint>())
				{
					IAssetEditorInstance* EditorInstance = AssetSub->FindEditorForAsset(AssetToOpen, true);
					
					// Static cast is safe here because we verified AssetToOpen is UWidgetBlueprint
					if (FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(EditorInstance))
					{
						if (UWidgetBlueprint* BP = WidgetEditor->GetWidgetBlueprintObj())
						{
							if (BP->WidgetTree)
							{
								// Find the template widget that matches the PIE instance name
								FName TargetName = OriginalObject->GetFName();
								UWidget* WidgetToSelect = nullptr;

								BP->WidgetTree->ForEachWidget([&](UWidget* TemplateWidget) {
									if (TemplateWidget->GetFName() == TargetName)
									{
										WidgetToSelect = TemplateWidget;
									}
								});

								if (WidgetToSelect)
								{
									TSet<FWidgetReference> Selection;
									FWidgetReference WidgetRef = WidgetEditor->GetReferenceFromTemplate(WidgetToSelect);
									if (WidgetRef.IsValid())
									{
										Selection.Add(WidgetRef);
										WidgetEditor->SelectWidgets(Selection, true);
									}
								}
							}
						}
					}
				}
			}
		}
	}
#endif
}

bool InputFlowHelpers::IsGameWorldWidget(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid()) return false;

	TSharedPtr<SWidget> Current = Widget;
	while(Current.IsValid())
	{
		if (Current->GetTag() == InputFlowAnalyzerTag)
		{
			return false;
		}
		Current = Current->GetParentWidget();
	}

	UWidget* Owner = GetOwnerUWidget(Widget);
	if (Owner)
	{
		UWorld* World = Owner->GetWorld();
		if (World && World->IsGameWorld())
		{
			return true;
		}
		return false; 
	}

#if WITH_EDITOR
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SViewport> GameViewport = FSlateApplication::Get().GetGameViewport();
		if (GameViewport.IsValid())
		{
			Current = Widget;
			while (Current.IsValid())
			{
				if (Current == GameViewport) return true;
				Current = Current->GetParentWidget();
			}
			return false; 
		}
	}
	return false;
#else
	return true; 
#endif
}

UWidget* InputFlowHelpers::GetOwnerUWidget(const TSharedPtr<SWidget>& Widget)
{
	if (TSharedPtr<FReflectionMetaData> MetaData = Widget->GetMetaData<FReflectionMetaData>())
	{
		UObject* Object = MetaData->SourceObject.Get();
		if (IsValid(Object))
		{
			return Cast<UWidget>(Object);
		}
	}

	if (Widget->GetTypeAsString() == TEXT("SObjectWidget"))
	{
		TSharedPtr<SObjectWidget> ObjectWidget = StaticCastSharedPtr<SObjectWidget>(Widget);
		if (ObjectWidget.IsValid())
		{
			UWidget* WidgetObj = ObjectWidget->GetWidgetObject();
			if (IsValid(WidgetObj))
			{
				return WidgetObj;
			}
		}
	}

	return nullptr;
}

bool InputFlowHelpers::IsButtonWidget(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid()) return false;
	
	if (UWidget* UObj = GetOwnerUWidget(Widget))
	{
		return UObj->IsA<UCommonButtonBase>() || UObj->IsA<UButton>();
	}
	return false;
}

FString InputFlowHelpers::TriggerEventToString(int32 EventType)
{
	ETriggerEvent Event = (ETriggerEvent)EventType;
	switch (Event)
	{
	case ETriggerEvent::None:      return TEXT("None");
	case ETriggerEvent::Triggered: return TEXT("TRIGGERED");
	case ETriggerEvent::Started:   return TEXT("Started");
	case ETriggerEvent::Ongoing:   return TEXT("Ongoing");
	case ETriggerEvent::Canceled:  return TEXT("Canceled");
	case ETriggerEvent::Completed: return TEXT("Completed");
	default: return TEXT("Unknown");
	}
}

FColor InputFlowHelpers::GetColorForTriggerEvent(int32 EventType)
{
	ETriggerEvent Event = (ETriggerEvent)EventType;
	switch (Event)
	{
	case ETriggerEvent::Triggered: return FColor::Green;
	case ETriggerEvent::Ongoing:   return FColor::Yellow;
	case ETriggerEvent::Canceled:  return FColor::Red;
	case ETriggerEvent::Started:   return FColor::Cyan;
	default: return FColor::White;
	}
}