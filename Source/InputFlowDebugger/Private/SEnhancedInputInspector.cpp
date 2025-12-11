// Copyright Mike Desrosiers, All Rights Reserved.

#include "SEnhancedInputInspector.h"

// Engine
#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>

// EnhancedInput
#include <EnhancedInputSubsystems.h>
#include <EnhancedPlayerInput.h>
#include <InputAction.h>
#include <InputMappingContext.h>

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

void SEnhancedInputInspector::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);
	
	DebugSubsystem = InSubsystem;
	bIsOverlay = InArgs._IsOverlay;

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
				SAssignNew(ListView, SListView<TSharedPtr<FEnhancedInputInfoItem>>)
				.ListItemsSource(&SourceData)
				.OnGenerateRow(this, &SEnhancedInputInspector::GenerateRow)
				.SelectionMode(ESelectionMode::None)
				.IsFocusable(!bIsOverlay)
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

	TArray<TSharedPtr<FEnhancedInputInfoItem>> NewData;

	const auto& ContextMap = FInputFlowDebugAccessor::GetContextData(PlayerInput);
	
	TArray<const UInputMappingContext*> SortedContexts;
	for (auto& Pair : ContextMap) { SortedContexts.Add(Pair.Key); }
	SortedContexts.Sort([&](const UInputMappingContext& A, const UInputMappingContext& B){
		return ContextMap[&A].Priority > ContextMap[&B].Priority;
	});

	for (const UInputMappingContext* IMC : SortedContexts)
	{
		TSharedPtr<FEnhancedInputInfoItem> Header = MakeShared<FEnhancedInputInfoItem>();
		Header->Name = IMC->GetName();
		Header->Priority = ContextMap[IMC].Priority;
		Header->bIsContext = true;
		NewData.Add(Header);

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
				if (Trigger != ETriggerEvent::None || Val.GetMagnitudeSq() > 0.001f)
				{
					TSharedPtr<FEnhancedInputInfoItem> ActionItem = MakeShared<FEnhancedInputInfoItem>();
					ActionItem->Name = Action->GetName();
					ActionItem->TriggerState = InputFlowHelpers::TriggerEventToString((int32)Trigger);
					ActionItem->StateColor = InputFlowHelpers::GetColorForTriggerEvent((int32)Trigger);
					ActionItem->ValueStr = Val.ToString();
					ActionItem->bIsContext = false;
					NewData.Add(ActionItem);
				}
			}
		}
	}

	bool bEqual = (NewData.Num() == SourceData.Num());
	if (bEqual)
	{
		for(int32 i=0; i<NewData.Num(); ++i)
		{
			if (*NewData[i] == *SourceData[i] == false)
			{
				bEqual = false; break;
			}
		}
	}

	if (!bEqual)
	{
		SourceData = NewData;
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SEnhancedInputInspector::GenerateRow(TSharedPtr<FEnhancedInputInfoItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Item->bIsContext)
	{
		return SNew(STableRow<TSharedPtr<FEnhancedInputInfoItem>>, OwnerTable)
		.Padding(FMargin(0, 4, 0, 2))
		[
			SNew(SBorder)
			.Padding(2)
			.BorderImage(bIsOverlay ? FCoreStyle::Get().GetBrush("NoBrush") : FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(bIsOverlay ? FLinearColor(0,0,0,0.2f) : FLinearColor(0.1f, 0.1f, 0.1f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s (Pri: %d)"), *Item->Name, Item->Priority)))
				.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.2f))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
		];
	}
	else
	{
		return SNew(STableRow<TSharedPtr<FEnhancedInputInfoItem>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 5, 0)
			[
				SNew(STextBlock).Text(FText::FromString(Item->Name))
				.ColorAndOpacity(bIsOverlay ? FLinearColor::White : FLinearColor::Black)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
				[
					SNew(STextBlock).Text(FText::FromString(Item->TriggerState))
					.ColorAndOpacity(Item->StateColor)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				]
			]
		];
	}
}