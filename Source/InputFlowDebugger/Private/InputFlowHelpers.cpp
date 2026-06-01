// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowHelpers.h"

// CoreUObject
#include <UObject/Package.h>

#if WITH_PLUGIN_COMMONUI
// CommonUI
#include <CommonActivatableWidget.h>
#include <CommonButtonBase.h>
#endif // WITH_PLUGIN_COMMONUI

// EnhancedInput
#if WITH_PLUGIN_ENHANCEDINPUT
#include <InputTriggers.h>
#include <EnhancedPlayerInput.h>
#include <InputMappingContext.h>
#endif

// Developer
#if WITH_EDITOR
#include <SourceCodeNavigation.h>
#endif

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
#include <Widgets/Input/SEditableText.h>
#include <Widgets/Text/SMultiLineEditableText.h>
#include <Widgets/SViewport.h>
#include <Widgets/Text/SRichTextBlock.h>
#include <Widgets/Text/STextBlock.h>

// UMG
#include <Blueprint/UserWidget.h>
#include <Components/Button.h>
#include <Components/EditableText.h>
#include <Components/EditableTextBox.h>
#include <Components/MultiLineEditableText.h>
#include <Components/MultiLineEditableTextBox.h>
#include <Components/RichTextBlock.h>
#include <Components/TextBlock.h>
#include <Components/Widget.h>

// Internal
#include "InputDebugSubsystem.h"
#include "LogInputFlow.h"

const FName InputFlowHelpers::InputFlowAnalyzerTag("InputFlowAnalyzerTag");

void InputFlowHelpers::SolveAABBCollisions(TArray<FInputFlowPhysicsItem>& Items, const FVector2D& Boundary,
	const int32 Iterations)
{
	const FVector2D Gutter(4.0f, 4.0f);

	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		bool bAnyMoved = false;

		for (int32 i = 0; i < Items.Num(); ++i)
		{
			// Constraint to Boundary
			if (!Items[i].bIsFixed)
			{
				Items[i].Position.X = FMath::Clamp(Items[i].Position.X, 0.0f, Boundary.X - Items[i].Size.X);
				Items[i].Position.Y = FMath::Clamp(Items[i].Position.Y, 0.0f, Boundary.Y - Items[i].Size.Y);
			}

			for (int32 j = i + 1; j < Items.Num(); ++j)
			{
				FInputFlowPhysicsItem& A = Items[i];
				FInputFlowPhysicsItem& B = Items[j];

				if (A.bIsFixed && B.bIsFixed) continue;

				FVector2D CenterA = A.Position + (A.Size * 0.5f);
				FVector2D CenterB = B.Position + (B.Size * 0.5f);
				FVector2D SizeA = A.Size + Gutter;
				FVector2D SizeB = B.Size + Gutter;

				const FVector2D Delta = CenterA - CenterB;
				const FVector2D AbsDelta = FVector2D(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
				const FVector2D HalfExtentsA = SizeA * 0.5f;
				const FVector2D HalfExtentsB = SizeB * 0.5f;

				FVector2D Overlap;
				Overlap.X = (HalfExtentsA.X + HalfExtentsB.X) - AbsDelta.X;
				Overlap.Y = (HalfExtentsA.Y + HalfExtentsB.Y) - AbsDelta.Y;

				if (Overlap.X > 0.0f && Overlap.Y > 0.0f)
				{
					// Resolve along shallowest axis
					const bool bResolveX = Overlap.X < Overlap.Y;
					FVector2D Push = FVector2D::ZeroVector;

					if (bResolveX)
						Push.X = (Delta.X > 0 ? Overlap.X : -Overlap.X);
					else
						Push.Y = (Delta.Y > 0 ? Overlap.Y : -Overlap.Y);

					if (A.bIsFixed)
					{
						B.Position -= Push;
					}
					else if (B.bIsFixed)
					{
						A.Position += Push;
					}
					else
					{
						A.Position += Push * 0.5f;
						B.Position -= Push * 0.5f;
					}
					bAnyMoved = true;
				}
			}
		}
		if (!bAnyMoved) break;
	}
}

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
	if (IsValid(GEngine))
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (IsValid(World))
			{
				UGameInstance* GameInstance = World->GetGameInstance();
				if (IsValid(GameInstance))
				{
					return GameInstance->GetSubsystem<UInputDebugSubsystem>();
				}
			}
		}
	}
	return nullptr;
}

FString InputFlowHelpers::GetWidgetDisplayName(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid()) return TEXT("None");

	FString DisplayName;
	const UWidget* Owner = GetOwnerUWidget(Widget);
	
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
	// Find Parent UserWidget / ActivatableWidget
	TSharedPtr<SWidget> Walker = Widget;
	UUserWidget* ImmediateUserParent = nullptr;
#if WITH_PLUGIN_COMMONUI
	UCommonActivatableWidget* ActivatableParent = nullptr;
#endif // WITH_PLUGIN_COMMONUI

	while (Walker.IsValid())
	{
		Walker = Walker->GetParentWidget();
		if (!Walker.IsValid()) break;

		UWidget* WalkerObj = GetOwnerUWidget(Walker);
		
		if (IsValid(WalkerObj) && WalkerObj != Owner)
		{
#if WITH_PLUGIN_COMMONUI
			if (UCommonActivatableWidget* FoundActivatable = Cast<UCommonActivatableWidget>(WalkerObj))
			{
				ActivatableParent = FoundActivatable;
				break;
			}
#endif // WITH_PLUGIN_COMMONUI
			
			if (WalkerObj->IsA<UUserWidget>())
			{
				ImmediateUserParent = Cast<UUserWidget>(WalkerObj);
			}
		}
	}

	// Append Context
#if WITH_PLUGIN_COMMONUI
	if (IsValid(ImmediateUserParent) && IsValid(ActivatableParent))
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
	else if (IsValid(ActivatableParent))
	{
		DisplayName += FString::Printf(TEXT("\n(from %s)"), *ActivatableParent->GetName());
	}
#else // WITH_PLUGIN_COMMONUI
	if (IsValid(ImmediateUserParent))
	{
		DisplayName += FString::Printf(TEXT("\n(from %s)"), *ImmediateUserParent->GetName());
	}
#endif // WITH_PLUGIN_COMMONUI

	return DisplayName;
}

FString InputFlowHelpers::GetSimpleDirectionName(EUINavigation Direction)
{
	switch (Direction)
	{
		case EUINavigation::Up:       return TEXT("↑ UP");
		case EUINavigation::Down:     return TEXT("↓ DOWN");
		case EUINavigation::Left:     return TEXT("← LEFT");
		case EUINavigation::Right:    return TEXT("→ RIGHT");
		case EUINavigation::Next:     return TEXT("⇥ NEXT");
		case EUINavigation::Previous: return TEXT("⇤ PREVIOUS");
		default:                      return TEXT("UNKNOWN");
	}
}

TSharedPtr<SWidget> InputFlowHelpers::FindInteractiveDescendant(TSharedPtr<SWidget> Root)
{
	if (!Root.IsValid()) return nullptr;

	FChildren* Children = Root->GetChildren();
	if (!Children) return nullptr;

	for (int32 i = 0; i < Children->Num(); ++i)
	{
		TSharedPtr<SWidget> Child = Children->GetChildAt(i);
		if (!Child.IsValid()) continue;

		// If this child is interactive, we found our normalization target
		if (Child->IsEnabled() && Child->SupportsKeyboardFocus())
		{
			// Optional: Filter out intermediate containers that might claim focus but aren't "Buttons".
			// For now, accepting the first focusable descendant is usually the correct "Primary" element.
			return Child;
		}

		// Recurse deeper
		TSharedPtr<SWidget> Found = FindInteractiveDescendant(Child);
		if (Found.IsValid())
		{
			return Found;
		}
	}

	return nullptr;
}

bool InputFlowHelpers::IsDescendantOf(TSharedPtr<SWidget> Child, TSharedPtr<SWidget> PotentialParent)
{
	if (!Child.IsValid() || !PotentialParent.IsValid()) return false;
	if (Child == PotentialParent) return true;

	TSharedPtr<SWidget> Current = Child->GetParentWidget();
	while (Current.IsValid())
	{
		if (Current == PotentialParent) return true;
		Current = Current->GetParentWidget();
	}
	return false;
}

const FTableRowStyle& InputFlowHelpers::GetTranslucentRowStyle()
{
	static FTableRowStyle Style;
	static bool bInitialized = false;

	if (!bInitialized)
	{
		// Start with the default style as a base
		Style = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");

		// 1. Clear the opaque background brushes
		Style.SetEvenRowBackgroundBrush(FSlateNoResource())
			 .SetOddRowBackgroundBrush(FSlateNoResource())
			 .SetParentRowBackgroundBrush(FSlateNoResource())
			 .SetParentRowBackgroundHoveredBrush(FSlateNoResource());

		// 2. Create a semi-transparent white brush for hover/selection states
		// We use a static brush to ensure the resource exists
		static FSlateBrush TranslucentHoverBrush;
		TranslucentHoverBrush.DrawAs = ESlateBrushDrawType::Box;
		TranslucentHoverBrush.TintColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.05f); // 5% opacity white
			
		static FSlateBrush TranslucentActiveBrush;
		TranslucentActiveBrush.DrawAs = ESlateBrushDrawType::Box;
		TranslucentActiveBrush.TintColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.1f); // 10% opacity white

		// 3. Apply the translucent brushes to interaction states
		Style.SetActiveBrush(TranslucentActiveBrush)
			 .SetActiveHoveredBrush(TranslucentActiveBrush)
			 .SetInactiveBrush(TranslucentActiveBrush)
			 .SetInactiveHoveredBrush(TranslucentActiveBrush)
			 .SetEvenRowBackgroundHoveredBrush(TranslucentHoverBrush)
			 .SetOddRowBackgroundHoveredBrush(TranslucentHoverBrush);

		// 4. Ensure text remains visible
		Style.SetTextColor(FLinearColor::White)
			 .SetSelectedTextColor(FLinearColor::White);

		bInitialized = true;
	}

	return Style;
}

const FTableViewStyle& InputFlowHelpers::GetTranslucentTableViewStyle()
{
	static FTableViewStyle Style;
	static bool bInitialized = false;

	if (!bInitialized)
	{
		// Start with the default engine TreeView style
		Style = FCoreStyle::Get().GetWidgetStyle<FTableViewStyle>("TreeView");
		
		// Force the background to be nothing/transparent
		Style.SetBackgroundBrush(FSlateNoResource());
		
		bInitialized = true;
	}
	
	return Style;
}

void InputFlowHelpers::TryOpenAsset(const TWeakObjectPtr<UObject>& ObjectWeak, const TWeakObjectPtr<UClass>& ClassWeak, bool bFindRootContext)
{
#if WITH_EDITOR
	UObject* AssetToOpen = nullptr;
	UObject* Instance = ObjectWeak.Get();
	UObject* OriginalObject = Instance;

	if (IsValid(Instance))
	{
		// Case A: Find Container (Leaf Click)
		// We want to find the UserWidget that CONTAINS this instance.
		// We start searching from the Outer to skip the instance itself.
		// We stop at the FIRST UserWidget we find (Immediate Parent).
		if (bFindRootContext)
		{
			UObject* Current = Instance->GetOuter();
			while (IsValid(Current))
			{
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Current); IsValid(UserWidget))
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
			while (IsValid(Current))
			{
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Current); IsValid(UserWidget))
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
	if (!AssetToOpen && IsValid(Instance))
	{
		// Walk up outer chain (e.g. if we clicked a slot widget)
		UObject* Current = Instance;
		while (IsValid(Current))
		{
			if (UUserWidget* UserWidget = Cast<UUserWidget>(Current); IsValid(UserWidget))
			{
				if (UClass* UWClass = UserWidget->GetClass(); IsValid(UWClass))
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

	if (IsValid(AssetToOpen) && IsValid(GEditor))
	{
		if (UAssetEditorSubsystem* AssetSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>(); IsValid(AssetSub))
		{
			if (AssetSub->OpenEditorForAsset(AssetToOpen))
			{
				// Only attempt selection if we performed a hierarchy walk (Leaf Click)
				// and the Asset is a Widget Blueprint.
				if (bFindRootContext && IsValid(OriginalObject) && OriginalObject != Instance && AssetToOpen->IsA<UWidgetBlueprint>())
				{
					IAssetEditorInstance* EditorInstance = AssetSub->FindEditorForAsset(AssetToOpen, true);
					
					// Static cast is safe here because we verified AssetToOpen is UWidgetBlueprint
					if (FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(EditorInstance))
					{
						if (UWidgetBlueprint* BP = WidgetEditor->GetWidgetBlueprintObj(); IsValid(BP))
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

								if (IsValid(WidgetToSelect))
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
	if (IsValid(Owner))
	{
		UWorld* World = Owner->GetWorld();
		if (IsValid(World) && World->IsGameWorld())
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
#if WITH_PLUGIN_COMMONUI
		// UCommonButtonBase is not a button. It is a UserWidget that contains a button and acts as a wrapper.
		if (UObj->IsA<UCommonButtonBase>()) return true;
#endif
		return UObj->IsA<UButton>();
	}
	return false;
}

FString InputFlowHelpers::TriggerEventToString(int32 EventType)
{
#if WITH_PLUGIN_ENHANCEDINPUT
	switch (static_cast<ETriggerEvent>(EventType))
	{
	case ETriggerEvent::None:      return TEXT("None");
	case ETriggerEvent::Triggered: return TEXT("Triggered");
	case ETriggerEvent::Started:   return TEXT("Started");
	case ETriggerEvent::Ongoing:   return TEXT("Ongoing");
	case ETriggerEvent::Canceled:  return TEXT("Canceled");
	case ETriggerEvent::Completed: return TEXT("Completed");
	default: return TEXT("Unknown");
	}
#else
	return FString::Printf(TEXT("Unknown(%d)"), EventType);
#endif // WITH_PLUGIN_ENHANCEDINPUT
}

FColor InputFlowHelpers::GetColorForTriggerEvent(int32 EventType)
{
#if WITH_PLUGIN_ENHANCEDINPUT
	switch (static_cast<ETriggerEvent>(EventType))
	{
	case ETriggerEvent::Triggered: return FColor::Green;
	case ETriggerEvent::Ongoing:   return FColor::Yellow;
	case ETriggerEvent::Canceled:  return FColor::Red;
	case ETriggerEvent::Started:   return FColor::Cyan;
	default: return FColor::White;
	}
#else
	return FColor::White;
#endif // WITH_PLUGIN_ENHANCEDINPUT
}

FString InputFlowHelpers::NavDirToString(EUINavigation Dir)
{
	switch (Dir)
	{
	case EUINavigation::Up:       return TEXT("Up");
	case EUINavigation::Down:     return TEXT("Down");
	case EUINavigation::Left:     return TEXT("Left");
	case EUINavigation::Right:    return TEXT("Right");
	case EUINavigation::Next:     return TEXT("Next");
	case EUINavigation::Previous: return TEXT("Previous");
	default:                      return TEXT("Unknown");
	}
}

FString InputFlowHelpers::NavRuleToString(EUINavigationRule Rule)
{
	switch (Rule)
	{
	case EUINavigationRule::Escape:   return TEXT("Escape");
	case EUINavigationRule::Explicit: return TEXT("Explicit");
	case EUINavigationRule::Wrap:     return TEXT("Wrap");
	case EUINavigationRule::Stop:     return TEXT("Stop");
	case EUINavigationRule::Custom:   return TEXT("Custom");
	case EUINavigationRule::Invalid:  return TEXT("Invalid");
	default:                          return TEXT("Unknown");
	}
}

FString InputFlowHelpers::NavSimResultToString(ENavSimResult Result)
{
	switch (Result)
	{
	case ENavSimResult::Normal:  return TEXT("Normal");
	case ENavSimResult::Stopped: return TEXT("Stopped");
	case ENavSimResult::Handled: return TEXT("Handled");
	default:                     return TEXT("Unknown");
	}
}

FString InputFlowHelpers::WidgetDesc(const TSharedPtr<SWidget>& W)
{
	if (!W.IsValid()) return TEXT("None");
	return FString::Printf(TEXT("%s | %s | Enabled=%s Focusable=%s"),
		*InputFlowHelpers::GetWidgetDisplayName(W),
		*W->GetTypeAsString(),
		W->IsEnabled() ? TEXT("Y") : TEXT("N"),
		W->SupportsKeyboardFocus() ? TEXT("Y") : TEXT("N"));
}

bool InputFlowHelpers::IsTableViewWidget(const TSharedPtr<SWidget>& InWidget, FString InTypeStr)
{
	if (!InWidget.IsValid()) return false;
	if (InTypeStr.IsEmpty()) InTypeStr = InWidget->GetTypeAsString();

	const bool bIsView =
		InTypeStr.Contains(TEXT("ListView")) ||
		InTypeStr.Contains(TEXT("TileView")) ||
		InTypeStr.Contains(TEXT("TreeView")) ||
		InTypeStr.Contains(TEXT("TableView"));

	return bIsView && !InTypeStr.Contains(TEXT("Row"));
}

void InputFlowHelpers::LogWidgetPathVerbose(const FWidgetPath& Path, const TCHAR* Prefix)
{
	UE_LOG(LogInputFlow, Verbose, TEXT("%s Widgets=%d (root->leaf):"), Prefix ? Prefix : TEXT("WidgetPath"), Path.Widgets.Num());
	for (int32 i = 0; i < Path.Widgets.Num(); ++i)
	{
		TSharedPtr<SWidget> W = Path.Widgets[i].Widget;
		UE_LOG(LogInputFlow, Verbose, TEXT("  [%02d] %s"), i, *WidgetDesc(W));
	}
}

int32 InputFlowHelpers::FindWidgetIndexInPath(const FWidgetPath& Path, const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid()) return INDEX_NONE;

	for (int32 i = 0; i < Path.Widgets.Num(); ++i)
	{
		if (Path.Widgets[i].Widget == Widget.ToSharedRef())
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FArrangedWidget InputFlowHelpers::ToWindowSpace(const FArrangedWidget& InArranged, const TSharedRef<SWindow>& Window)
{
	FArrangedWidget Out = InArranged;

	const FSlateLayoutTransform WindowInverse =
		Window->GetWindowGeometryInScreen().GetAccumulatedLayoutTransform().Inverse();

	Out.Geometry.AppendTransform(WindowInverse);
	return Out;
}

bool InputFlowHelpers::ExtractTextFromWidget(const TSharedPtr<SWidget>& Widget, FText& OutText)
{
	if (!Widget.IsValid())
	{
		return false;
	}
	
	const FName WidgetType = Widget->GetType();
	
	static const FName Type_STextBlock("STextBlock");
	static const FName Type_SRichTextBlock("SRichTextBlock");
	static const FName Type_SEditableText("SEditableText");
	static const FName Type_SMultiLineEditableText("SMultiLineEditableText");

	if (WidgetType == Type_STextBlock)
	{
		OutText = StaticCastSharedPtr<STextBlock>(Widget)->GetText();
		return true;
	}
	if (WidgetType == Type_SRichTextBlock)
	{
		OutText = StaticCastSharedPtr<SRichTextBlock>(Widget)->GetText();
		return true;
	}
	if (WidgetType == Type_SEditableText)
	{
		OutText = StaticCastSharedPtr<SEditableText>(Widget)->GetText();
		return true;
	}
	if (WidgetType == Type_SMultiLineEditableText)
	{
		OutText = StaticCastSharedPtr<SMultiLineEditableText>(Widget)->GetText();
		return true;
	}

	return false;
}

void InputFlowHelpers::ForEachTextWidget(
	const TSharedPtr<SWidget>& Root,
	TFunctionRef<void(const TSharedPtr<SWidget>&)> Callback,
	const FName ExcludeTag)
{
	if (!Root.IsValid())
	{
		return;
	}

	if (!Root->GetVisibility().IsVisible())
	{
		return;
	}

	if (!ExcludeTag.IsNone() && Root->GetTag() == ExcludeTag)
	{
		return;
	}

	FText Unused;
	if (ExtractTextFromWidget(Root, Unused))
	{
		Callback(Root);
	}

	// Recurse
	if (FChildren* Children = Root->GetChildren())
	{
		const int32 NumChildren = Children->Num();
		for (int32 i = 0; i < NumChildren; ++i)
		{
			const TSharedPtr<SWidget> Child = Children->GetChildAt(i);
			ForEachTextWidget(Child, Callback, ExcludeTag);
		}
	}
}

#if WITH_PLUGIN_ENHANCEDINPUT

// The accessor class is now defined ONLY here.
class FInputFlowDebugAccessor : public UEnhancedPlayerInput
{
public:
	static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& GetContextData(const UEnhancedPlayerInput* PlayerInput)
	{
		// Safe cast because we are just accessing memory layout, not changing it
		return static_cast<const FInputFlowDebugAccessor*>(PlayerInput)->GetAppliedInputContextData();
	}
};

const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& InputFlowHelpers::GetInputContextData(const UEnhancedPlayerInput* PlayerInput)
{
	if (!PlayerInput)
	{
		static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData> EmptyMap;
		return EmptyMap;
	}
	return FInputFlowDebugAccessor::GetContextData(PlayerInput);
}

#endif


/** Finds the first focusable descendant using a Depth-First Search. */
TSharedPtr<SWidget> InputFlowHelpers::ResolveFocusableDescendant(TSharedPtr<SWidget> Root)
{
	if (!Root.IsValid() || !Root->IsEnabled()) return nullptr;

	if (FChildren* Children = Root->GetChildren())
	{
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			TSharedRef<SWidget> Child = Children->GetChildAt(i);
			TSharedPtr<SWidget> Result = ResolveFocusableDescendant(Child);
			if (Result.IsValid())
			{
				return Result;
			}
		}
	}

	if (Root->SupportsKeyboardFocus())
	{
		return Root;
	}

	return nullptr;
}