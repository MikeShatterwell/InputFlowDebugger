// Copyright Mike Desrosiers, All Rights Reserved.

#include "SEnhancedInputInspector.h"

#if WITH_PLUGIN_ENHANCEDINPUT
// Engine
#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>
#include <Engine/Level.h>
#include <Engine/LevelScriptActor.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>

// EnhancedInput
#include <EnhancedInputSubsystems.h>
#include <EnhancedPlayerInput.h>
#include <InputAction.h>
#include <InputMappingContext.h>
#include <InputModifiers.h>
#include <EnhancedInputComponent.h>

// Slate
#include <Styling/AppStyle.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Notifications/SProgressBar.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"

class SEnhancedInputContextRow : public STableRow<TSharedPtr<FEnhancedInputInfoItem>>
{
public:
	SLATE_BEGIN_ARGS(SEnhancedInputContextRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FEnhancedInputInfoItem> InItem, bool bInOverlay)
	{
		SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

		const FTableRowStyle& RowStyle = bInOverlay ?
			InputFlowHelpers::GetTranslucentRowStyle() :
			FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");

		STableRow<TSharedPtr<FEnhancedInputInfoItem>>::Construct(
			STableRow<TSharedPtr<FEnhancedInputInfoItem>>::FArguments()
			.Padding(FMargin(0, 2, 0, 2))
			.ShowSelection(false)
			.Style(&RowStyle),
			InOwnerTableView
		);

		if (bInOverlay)
		{
			SetBorderBackgroundColor(FLinearColor::Transparent);
		}

		this->ChildSlot
		[
			SNew(SBorder)
			.Padding(2)
			.BorderImage(InputFlowHelpers::GetBackgroundBrush(bInOverlay))
			.BorderBackgroundColor(bInOverlay ? FLinearColor(0,0,0,0.2f) : FLinearColor(0.1f, 0.1f, 0.1f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([InItem](){ return FText::FromString(FString::Printf(TEXT("%s (Pri: %d)"), *InItem->Name, InItem->Priority)); })
					.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.2f))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([InItem](){ return FText::FromString(InItem->OwnerStr); })
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				]
			]
		];
	}
};

void SEnhancedInputInspector::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
	
	DebugSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;

	const FTableViewStyle& TreeStyle = bIsOverlay 
	? InputFlowHelpers::GetTranslucentTableViewStyle() 
	: FCoreStyle::Get().GetWidgetStyle<FTableViewStyle>("TreeView");

	SetVisibility(EVisibility::SelfHitTestInvisible);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(bIsOverlay))
		.Visibility(EVisibility::SelfHitTestInvisible)
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2)
			[
				SNew(STextBlock)
				.Text(FText::FromString("Enhanced Input Actions"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(bIsOverlay ? FLinearColor(1.0f, 0.8f, 0.2f) : FLinearColor::White)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FEnhancedInputInfoItem>>)
				.TreeItemsSource(&SourceData)
				.OnGenerateRow(this, &SEnhancedInputInspector::GenerateRow)
				.OnGetChildren(this, &SEnhancedInputInspector::OnGetChildren)
				.SelectionMode(ESelectionMode::None)
				.TreeViewStyle(&TreeStyle)
				.Visibility(EVisibility::SelfHitTestInvisible)
			]
		]
	];
}

void SEnhancedInputInspector::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UpdateData(InDeltaTime);
}

void SEnhancedInputInspector::UpdateData(float DeltaTime)
{
	// Recover subsystem if lost (re-PIE)
	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (!DebugSubsystem.IsValid()) return;

	const UGameInstance* GameInstance = DebugSubsystem->GetGameInstance();
	if (!IsValid(GameInstance)) return;

	auto LocalPlayer = GameInstance->GetFirstGamePlayer();
	if (!IsValid(LocalPlayer)) return;

	const UEnhancedInputLocalPlayerSubsystem* EISub = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!IsValid(EISub)) return;

	const UEnhancedPlayerInput* PlayerInput = EISub->GetPlayerInput();
	if (!IsValid(PlayerInput)) return;

	const UWorld* World = EISub->GetWorld();
	if (!IsValid(World)) return;
	
	// We mimic APlayerController::BuildInputStack logic to find active components.
	TArray<UInputComponent*> PotentialComponents;
	const APlayerController* PC = LocalPlayer->GetPlayerController(GameInstance->GetWorld());
	if (IsValid(PC))
	{
		const APawn* Pawn = PC->GetPawn();
		if (IsValid(Pawn) && Pawn->InputEnabled())
		{
			if (Pawn->InputComponent) PotentialComponents.Add(Pawn->InputComponent);
			
			// Check other components on pawn
			TArray<UInputComponent*> PawnComponents;
			Pawn->GetComponents(PawnComponents);
			for (UInputComponent* IC : PawnComponents)
			{
				if (IsValid(IC) && IC != Pawn->InputComponent) PotentialComponents.Add(IC);
			}
		}
		
		for (ULevel* Level : World->GetLevels())
		{
			if (ALevelScriptActor* ScriptActor = Level->GetLevelScriptActor())
			{
				if (ScriptActor->InputEnabled() && ScriptActor->InputComponent)
				{
					PotentialComponents.Add(ScriptActor->InputComponent);
				}
			}
		}
		
		if (PC->InputEnabled() && PC->InputComponent)
		{
			PotentialComponents.Add(PC->InputComponent);
		}
	}

	// Map Action -> List of Owner Names
	TMap<const UInputAction*, TArray<FString>> ActionOwners;
	for (UInputComponent* IC : PotentialComponents)
	{
		if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(IC))
		{
			FString OwnerName = IC->GetOwner() ? IC->GetOwner()->GetActorNameOrLabel() : TEXT("Unknown");
			
			// Iterate Bindings
			for (const auto& Binding : EIC->GetActionEventBindings())
			{
				if (const UInputAction* BoundAction = Binding->GetAction())
				{
					ActionOwners.FindOrAdd(BoundAction).AddUnique(OwnerName);
				}
			}
		}
	}

	// --- Sync Data ---
	// To preserve tree expansion state, we update existing items in-place where possible
	// and only modify the list structure when Contexts/Actions are added/removed.

	const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& ContextMap =
		InputFlowHelpers::GetInputContextData(PlayerInput);
	
	TArray<const UInputMappingContext*> SortedContexts;
	for (auto& Pair : ContextMap) { if(Pair.Key) SortedContexts.Add(Pair.Key); }
	
	SortedContexts.Sort([&](const UInputMappingContext& A, const UInputMappingContext& B){
		return ContextMap[&A].Priority > ContextMap[&B].Priority;
	});

	bool bStructureChanged = false;
	TArray<TSharedPtr<FEnhancedInputInfoItem>> NewRoots;

	for (const UInputMappingContext* IMC : SortedContexts)
	{
		// Find existing root or create new
		TSharedPtr<FEnhancedInputInfoItem> ContextItem = nullptr;
		const FString& IMCName = IMC->GetName();
		
		auto FoundItem = SourceData.FindByPredicate([&](const TSharedPtr<FEnhancedInputInfoItem>& Item){
			return Item->Name == IMCName && Item->bIsInputMappingContext;
		});

		if (FoundItem)
		{
			ContextItem = *FoundItem;
		}
		else
		{
			ContextItem = MakeShared<FEnhancedInputInfoItem>();
			ContextItem->Name = IMCName;
			ContextItem->bIsInputMappingContext = true;
			bStructureChanged = true;
		}

		// Update Context Properties
		ContextItem->Priority = ContextMap[IMC].Priority;

		TArray<FString> ContextOwnerList;
		for (const auto& Mapping : IMC->GetMappings())
		{
			if (Mapping.Action && ActionOwners.Contains(Mapping.Action))
			{
				for (const FString& Owner : ActionOwners[Mapping.Action])
				{
					ContextOwnerList.AddUnique(Owner);
				}
			}
		}
		if (ContextOwnerList.Num() > 0)
		{
			ContextItem->OwnerStr = FString::Join(ContextOwnerList, TEXT(", "));
		}
		else
		{
			ContextItem->OwnerStr = TEXT("Unbound");
		}
		
		// Process Children (Actions)
		TArray<TSharedPtr<FEnhancedInputInfoItem>> NewChildren;
		TSet<const UInputAction*> ActionsProcessedInContext;
		TSet<TSharedPtr<FEnhancedInputInfoItem>> ProcessedItems; // Track reused items

		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			if (!IsValid(Action)) continue;
			if (ActionsProcessedInContext.Contains(Action)) continue;

			ActionsProcessedInContext.Add(Action);

			if (const FInputActionInstance* Data = PlayerInput->FindActionInstanceData(Action))
			{
				ETriggerEvent Trigger = Data->GetTriggerEvent();
				FInputActionValue Val = PlayerInput->GetActionValue(Action);
				
				// Checking TriggerState or Magnitude > 0 to filter only active/relevant inputs
				// NOTE: We only show items that are 'active' to reduce noise. If an input doesn't appear here when pressed, it isn't being processed by Enhanced Input.
				if (Trigger != ETriggerEvent::None || Val.GetMagnitudeSq() > 0.001f)
				{
					FString ActionName = Action->GetName();
					TSharedPtr<FEnhancedInputInfoItem> ActionItem = nullptr;

					// Try to find existing child to update
					auto FoundChild = ContextItem->Children.FindByPredicate([&](const TSharedPtr<FEnhancedInputInfoItem>& Item){
						return Item->Name == ActionName;
					});

					if (FoundChild)
					{
						ActionItem = *FoundChild;
					}
					else
					{
						ActionItem = MakeShared<FEnhancedInputInfoItem>();
						ActionItem->Name = ActionName;
						ActionItem->bIsInputMappingContext = false;
					}
					
					// Reset Decay
					ActionItem->CurrentDecayTime = 0.0f;
					ProcessedItems.Add(ActionItem);

					// Update Action Values
					ActionItem->TriggerState = InputFlowHelpers::TriggerEventToString(static_cast<int32>(Trigger));
					ActionItem->StateColor = InputFlowHelpers::GetColorForTriggerEvent(static_cast<int32>(Trigger));
					ActionItem->ValueStr = Val.ToString();
					if (ActionOwners.Contains(Action))
					{
						ActionItem->OwnerStr = FString::Join(ActionOwners[Action], TEXT(", "));
					}

					// --- Modifier Simulation Logic ---
					// To visualize the steps, we need to find which key is actually driving this action.
					// We iterate all mappings for this action in this context and pick the one with the highest raw magnitude.
					FEnhancedActionKeyMapping BestMapping = Mapping;
					FInputActionValue BestRawValue(Action->ValueType, FVector::ZeroVector);
					
					for (const auto& SearchMapping : IMC->GetMappings())
					{
						if (SearchMapping.Action == Action)
						{
							FInputActionValue Raw = PlayerInput->GetKeyValue(SearchMapping.Key);
							if (Raw.GetMagnitudeSq() > BestRawValue.GetMagnitudeSq())
							{
								BestRawValue = Raw;
								BestMapping = SearchMapping;
							}
						}
					}

					// Build the Simulation String: [Key] -> [MapMods] -> [ActionMods]
					FString SimulationStr = FString::Printf(TEXT("[%s: %s]\n"), *BestMapping.Key.GetDisplayName().ToString(), *BestRawValue.ToString());
					
					// Mapping Modifiers
					FInputActionValue CurrentVal = BestRawValue;
					for (UInputModifier* Mod : BestMapping.Modifiers)
					{
						if (!IsValid(Mod)) continue;
						
						CurrentVal = Mod->ModifyRaw(PlayerInput, CurrentVal, DeltaTime);
						FString ClassName = Mod->GetClass()->GetName().Replace(TEXT("InputModifier"), TEXT(""));
						SimulationStr += FString::Printf(TEXT(" -> IMC: [%s: %s]\n"), *ClassName, *CurrentVal.ToString());
					}

					// Action Modifiers
					for (UInputModifier* Mod : Action->Modifiers)
					{
						if (Mod)
						{
							CurrentVal = Mod->ModifyRaw(PlayerInput, CurrentVal, DeltaTime);
							FString ClassName = Mod->GetClass()->GetName().Replace(TEXT("InputModifier"), TEXT(""));
							SimulationStr += FString::Printf(TEXT(" -> IA: [%s: %s]\n"), *ClassName, *CurrentVal.ToString());
						}
					}

					ActionItem->ModifierChainStr = SimulationStr;

					// Populate legacy/simple strings for other columns/tooltips if needed
					auto GetName = [](UObject* Obj) { 
						return Obj ? Obj->GetClass()->GetName().Replace(TEXT("InputModifier"), TEXT("")).Replace(TEXT("InputTrigger"), TEXT("")) : TEXT("Null"); 
					};

					FString TrigStr;
					for (const UInputTrigger* Trig : Action->Triggers) { if (Trig) TrigStr += (TrigStr.IsEmpty() ? "" : ", ") + FString::Printf(TEXT("%s (IA)"), *GetName((UObject*)Trig)); }
					for (const UInputTrigger* Trig : BestMapping.Triggers) { if (Trig) TrigStr += (TrigStr.IsEmpty() ? "" : ", ") + FString::Printf(TEXT("%s (IMC)"), *GetName((UObject*)Trig)); }
					ActionItem->TriggersStr = TrigStr;

					NewChildren.Add(ActionItem);
				}
			}
		}

		// Process lingering children for decay
		if (ContextItem.IsValid())
		{
			for (TSharedPtr<FEnhancedInputInfoItem>& OldChild : ContextItem->Children)
			{
				// If we haven't processed (re-added/updated) this child yet...
				if (!ProcessedItems.Contains(OldChild))
				{
					// Increment Decay
					OldChild->CurrentDecayTime += DeltaTime;

					// If still within decay window, keep it
					if (OldChild->CurrentDecayTime < OldChild->MaxDecayTime)
					{
						NewChildren.Add(OldChild);
					}
				}
			}
		}

		// Detect Child Changes
		if (NewChildren.Num() != ContextItem->Children.Num())
		{
			bStructureChanged = true;
		}
		else
		{
			for (int32 i=0; i<NewChildren.Num(); ++i)
			{
				if (NewChildren[i] != ContextItem->Children[i])
				{
					bStructureChanged = true; break;
				}
			}
		}
		
		ContextItem->Children = NewChildren;
		NewRoots.Add(ContextItem);
	}

	// Detect Root Changes
	if (NewRoots.Num() != SourceData.Num())
	{
		bStructureChanged = true;
	}
	else
	{
		for (int32 i=0; i<NewRoots.Num(); ++i)
		{
			if (NewRoots[i] != SourceData[i])
			{
				bStructureChanged = true; break;
			}
		}
	}

	SourceData = NewRoots;

	if (bStructureChanged)
	{
		TreeView->RequestTreeRefresh();
		
		// Auto-Expand all Contexts by default for visibility
		for (const auto& Root : SourceData)
		{
			TreeView->SetItemExpansion(Root, true);
		}
	}
}

TSharedRef<ITableRow> SEnhancedInputInspector::GenerateRow(TSharedPtr<FEnhancedInputInfoItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Item->bIsInputMappingContext)
	{
		return SNew(SEnhancedInputContextRow, OwnerTable, Item, bIsOverlay);
	}

	const FTableRowStyle& RowStyle = bIsOverlay ?
	InputFlowHelpers::GetTranslucentRowStyle() :
	FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");

	// Action Row with Modifier Support
	return SNew(STableRow<TSharedPtr<FEnhancedInputInfoItem>>, OwnerTable)
	.Style(&RowStyle)
	[
		SNew(SOverlay)
		// Decay Progress Bar
		+ SOverlay::Slot().Padding(0, 2, 0, 0).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SProgressBar)
			.Percent_Lambda([Item]()
			{
				// Invert logic: Full when active (Decay=0), Empty when decayed (Decay=Max)
				return FMath::Clamp(1.0f - (Item->CurrentDecayTime / Item->MaxDecayTime), 0.0f, 1.0f);
			})
			.FillColorAndOpacity(FLinearColor(0.1f, 0.1f, 0.1f, 0.2f))
			.Visibility(EVisibility::HitTestInvisible)
			.Style(FAppStyle::Get(), "ProgressBar")
		]
		// Main Content
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)
			// Top Row: Name, Value, State
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([Item](){ return FText::FromString(Item->Name); })
					.ColorAndOpacity(FLinearColor::White)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([Item](){ return FText::FromString(Item->ValueStr); })
						.ColorAndOpacity(FLinearColor::Gray)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([Item](){ return FText::FromString(Item->TriggerState); })
						.ColorAndOpacity_Lambda([Item](){ return Item->StateColor; })
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					]
				]
			]
			// Modifiers Row
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Item](){ return Item->ModifierChainStr.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString("Chain: "))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text_Lambda([Item](){ return FText::FromString(Item->ModifierChainStr); })
					.ColorAndOpacity(FLinearColor(0.4f, 0.8f, 1.0f))
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 7))
				]
			]
			// Triggers Row
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Item](){ return Item->TriggersStr.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString("Trigs: "))
					.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text_Lambda([Item](){ return FText::FromString(Item->TriggersStr); })
					.ColorAndOpacity(FLinearColor(1.0f, 0.4f, 0.4f)) // Light Red
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 7))
				]
			]
		]
	];
}
void SEnhancedInputInspector::OnGetChildren(TSharedPtr<FEnhancedInputInfoItem> Item,
	TArray<TSharedPtr<FEnhancedInputInfoItem>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

#endif //WITH_PLUGIN_ENHANCEDINPUT