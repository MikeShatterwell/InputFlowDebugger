// Copyright Mike Desrosiers, All Rights Reserved.

#include "SEnhancedInputInspector.h"

#if WITH_PLUGIN_ENHANCEDINPUT

// Engine
#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>

// EnhancedInput
#include <EnhancedInputSubsystems.h>
#include <EnhancedPlayerInput.h>
#include <InputAction.h>
#include <InputMappingContext.h>
#include <InputModifiers.h>

// Slate
#include <Styling/AppStyle.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Text/STextBlock.h>

// Internal
#include "InputDebugSubsystem.h"
#include "InputFlowHelpers.h"

// Accessor hack to get internal Context Data
class FInputFlowDebugAccessor : public UEnhancedPlayerInput
{
public:
	static const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& GetContextData(const UEnhancedPlayerInput* PlayerInput)
	{
		return static_cast<const FInputFlowDebugAccessor*>(PlayerInput)->GetAppliedInputContextData();
	}
};

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

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(bIsOverlay))
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
			]
		]
	];
}

void SEnhancedInputInspector::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UpdateData();
}

void SEnhancedInputInspector::UpdateData()
{
	// Recover subsystem if lost (re-PIE)
	if (!DebugSubsystem.IsValid())
	{
		DebugSubsystem = InputFlowHelpers::GetActiveDebugSubsystem();
	}

	if (!DebugSubsystem.IsValid()) return;

	UGameInstance* GameInstance = DebugSubsystem->GetGameInstance();
	if (!GameInstance) return;
	
	ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
	if (!LocalPlayer) return;

	UEnhancedInputLocalPlayerSubsystem* EISub = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!EISub) return;

	UEnhancedPlayerInput* PlayerInput = EISub->GetPlayerInput();
	if (!PlayerInput) return;

	// --- Sync Data ---
	// To preserve tree expansion state, we update existing items in-place where possible
	// and only modify the list structure when Contexts/Actions are added/removed.

	const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& ContextMap =
		FInputFlowDebugAccessor::GetContextData(PlayerInput);
	
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
		FString IMCName = IMC->GetName();
		
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
		
		// Process Children (Actions)
		TArray<TSharedPtr<FEnhancedInputInfoItem>> NewChildren;
		TSet<const UInputAction*> ActionsProcessedInContext;

		for (const auto& Mapping : IMC->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			if (!Action) continue;
			if (ActionsProcessedInContext.Contains(Action)) continue;

			ActionsProcessedInContext.Add(Action);

			if (const FInputActionInstance* Data = PlayerInput->FindActionInstanceData(Action))
			{
				ETriggerEvent Trigger = Data->GetTriggerEvent();
				FInputActionValue Val = PlayerInput->GetActionValue(Action);
				
				// Checking TriggerState or Magnitude > 0 to filter only active/relevant inputs
				// NOTE: We only show items that are 'active' to reduce noise, similar to original logic
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
						// Child added, but parent structure doesn't change from View perspective unless we set flag
						// However, we rebuild the child list below, so we detect diffs there.
					}

					// Update Action Values
					ActionItem->TriggerState = InputFlowHelpers::TriggerEventToString((int32)Trigger);
					ActionItem->StateColor = InputFlowHelpers::GetColorForTriggerEvent((int32)Trigger);
					ActionItem->ValueStr = Val.ToString();

					// Modifiers
					FString ModStr;
					for (const UInputModifier* Mod : Data->GetModifiers())
					{
						if (Mod)
						{
							if (!ModStr.IsEmpty()) ModStr += TEXT(", ");
							FString ClassName = Mod->GetClass()->GetName();
							ClassName.RemoveFromStart("InputModifier");
							ModStr += ClassName;
						}
					}
					ActionItem->ModifiersStr = ModStr;

					NewChildren.Add(ActionItem);
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

	// Action Row with Modifier Support
	return SNew(STableRow<TSharedPtr<FEnhancedInputInfoItem>>, OwnerTable)
	.Padding(FMargin(16, 0, 0, 0)) // Indent actions
	[
		SNew(SVerticalBox)
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
			.Visibility_Lambda([Item](){ return Item->ModifiersStr.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([Item](){ return FText::FromString(Item->ModifiersStr); })
				.ColorAndOpacity(FLinearColor(0.4f, 0.6f, 1.0f))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 7))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([Item](){ return FText::FromString(FString::Printf(TEXT("=> %s"), *Item->ValueStr)); })
				.ColorAndOpacity(FLinearColor(0.4f, 0.6f, 1.0f))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 7))
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