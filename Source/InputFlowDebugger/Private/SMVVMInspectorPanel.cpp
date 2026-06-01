// Copyright Mike Desrosiers, All Rights Reserved. 

#include "SMVVMInspectorPanel.h"


#if WITH_PLUGIN_MODELVIEWVIEWMODEL

// Internal 
#include "InputFlowHelpers.h"
#include "InputDebugSubsystem.h"

// Engine
#include <Engine/Engine.h>
#include <Engine/GameInstance.h>
#include <Engine/GameViewportClient.h>

// UMG 
#include <Blueprint/UserWidget.h>
#include <Components/Widget.h>

// ModelViewViewModel 
#include <MVVMViewModelBase.h>
#include <View/MVVMView.h>
#include <View/MVVMViewClass.h>

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
// UMGWidgetPreview
#include <IWidgetPreviewToolkit.h>
#include <WidgetPreview.h>
#endif

// CoreUObject
#include <UObject/UObjectArray.h>
#include <UObject/Class.h>
#include <UObject/EnumProperty.h>
#include <UObject/TextProperty.h>
#include <UObject/UnrealType.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/UObjectIterator.h>
#include <UObject/ScriptDelegates.h>

// GameplayTags 
#include <GameplayTagContainer.h>

// Slate 
#include <Framework/Application/IInputProcessor.h>
#include <Framework/Application/SlateApplication.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <Layout/WidgetPath.h>
#include <Styling/AppStyle.h>
#include <Types/ReflectionMetadata.h>
#include <Widgets/Images/SImage.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SComboBox.h>
#include <Widgets/Input/SComboButton.h>
#include <Widgets/Input/SEditableTextBox.h>
#include <Widgets/Input/SSpinBox.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SSplitter.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Views/SExpanderArrow.h>
#include <Widgets/Input/SSearchBox.h>
#include <Widgets/Colors/SColorBlock.h>
#include <Widgets/SViewport.h>

/**
 * Input preprocessor installed while picking mode is active. Forwards mouse-move ticks to the panel
 * for hover updates, and consumes left-click (commit), right-click / Escape (cancel). Held as a
 * TSharedRef<IInputProcessor> by the Slate application; released on EndPicking.
 *
 * Weak-captures the panel so a late Tick after panel destruction is a no-op instead of a crash.
 */
class FMVVMInspectorPickerInputProcessor : public IInputProcessor
{
public:
	explicit FMVVMInspectorPickerInputProcessor(TWeakPtr<SMVVMInspectorPanel> InWeakPanel)
		: WeakPanel(MoveTemp(InWeakPanel))
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
		if (const TSharedPtr<SMVVMInspectorPanel> Panel = WeakPanel.Pin(); Panel.IsValid())
		{
			if (Panel->IsPicking())
			{
				Panel->UpdatePickingHover();
			}
		}
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SMVVMInspectorPanel> Panel = WeakPanel.Pin();
		if (!Panel.IsValid() || !Panel->IsPicking())
		{
			return false;
		}

		const FKey Btn = MouseEvent.GetEffectingButton();
		if (Btn == EKeys::LeftMouseButton)
		{
			Panel->CommitPick();
			return true;  // Consumed, game viewport never sees this click
		}
		if (Btn == EKeys::RightMouseButton)
		{
			Panel->CancelPick();
			return true;
		}
		return false;
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override
	{
		const TSharedPtr<SMVVMInspectorPanel> Panel = WeakPanel.Pin();
		if (!Panel.IsValid() || !Panel->IsPicking())
		{
			return false;
		}
		if (KeyEvent.GetKey() == EKeys::Escape)
		{
			Panel->CancelPick();
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("MVVMInspectorPicker"); }

private:
	TWeakPtr<SMVVMInspectorPanel> WeakPanel = nullptr;
};

namespace MVVMInspectorPanel
{
	// Common struct names we want to display as a single editable text line. 
	static const FName NAME_GameplayTag(TEXT("GameplayTag"));
	static const FName NAME_GameplayTagContainer(TEXT("GameplayTagContainer"));
	static const FName NAME_Vector(TEXT("Vector"));
	static const FName NAME_Vector2D(TEXT("Vector2D"));
	static const FName NAME_Rotator(TEXT("Rotator"));
	static const FName NAME_Color(TEXT("Color"));
	static const FName NAME_LinearColor(TEXT("LinearColor"));
	static const FName NAME_InstancedStruct(TEXT("InstancedStruct"));

	static constexpr FLinearColor WidgetPickerHighlightColor = FLinearColor(0.9f, 0.9f, 0.3f);

	static constexpr FLinearColor OverlayBackgroundDark      = FLinearColor(0.0f, 0.0f, 0.0f, 0.2f);
	static constexpr FLinearColor OverlayBackgroundMedium    = FLinearColor(0.0f, 0.0f, 0.0f, 0.15f);

	static constexpr FLinearColor TextPrimary                = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	static constexpr FLinearColor TextSecondary              = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);
	static constexpr FLinearColor TextTertiary               = FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);
	static constexpr FLinearColor TextMuted                  = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	static constexpr FLinearColor TextTransparent            = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	static constexpr FLinearColor CategoryHeaderColor        = FLinearColor(1.0f, 0.8f, 0.2f, 1.0f);
	static constexpr FLinearColor ComputedValueColor         = FLinearColor(0.4f, 0.8f, 1.0f, 1.0f);
	static constexpr FLinearColor ObjectValidColor           = FLinearColor(0.7f, 0.9f, 1.0f, 1.0f);
	static constexpr FLinearColor SoftObjectRefColor         = FLinearColor(0.6f, 1.0f, 0.6f, 1.0f);
	static constexpr FLinearColor WeakObjectRefColor         = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);
	static constexpr FLinearColor SubclassOfColor            = FLinearColor(1.0f, 0.5f, 0.8f, 1.0f);

	// Small helper to identify UWidget objects to ignore them in the property list 
	static bool IsWidgetObject(const UObject* Obj)
	{
		return IsValid(Obj) && Obj->IsA(UWidget::StaticClass());
	}

	// Helper to determine if we should inspect an object's internal properties
	// Currently only supports UMVVMViewModelBase-derived objects, but we might want to expand this in the future
	// when node expansion and property walking is optimized enough to handle more complex objects (Actors, Components, etc).
	static bool ShouldReflectObject(const UObject* Obj)
	{
		if (!IsValid(Obj))
		{
			return false;
		}
		
		return Obj->IsA(UMVVMViewModelBase::StaticClass());
	}

	// -----------------------------------------------------------------------------------
	// VM Mocking
	//
	// Instantiates stand-in ViewModel instances so the inspector can show something useful
	// when the real source slots are unpopulated (e.g., a resolver that returns null in the editor)
	// -----------------------------------------------------------------------------------

	/** Can we safely instantiate a mock of this class? */
	static bool IsMockableViewModelClass(const UClass* Class)
	{
		return IsValid(Class)
			&& Class->IsChildOf(UMVVMViewModelBase::StaticClass())
			&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
	}

	/** Forward declaration for mutual recursion between object/container walkers. */
	static void PopulateNestedViewModelsInContainer(uint8* ContainerPtr, const UStruct* StructType, UObject* Outer, TSet<UObject*>& Visited, int32 Depth);

	/**
	 * Recursively walk a ViewModel's properties and instantiate mock instances for any
	 * nested ViewModel-typed properties that are null.
	 */
	static void PopulateNestedViewModels(UObject* Owner, UObject* Outer, TSet<UObject*>& Visited, const int32 Depth = 0)
	{
		constexpr int32 MaxDepth = 8;
		if (!IsValid(Owner) || Depth >= MaxDepth)
		{
			return;
		}
		if (Visited.Contains(Owner))
		{
			return;
		}
		Visited.Add(Owner);

		PopulateNestedViewModelsInContainer(reinterpret_cast<uint8*>(Owner), Owner->GetClass(), Outer, Visited, Depth);
	}

	static void PopulateNestedViewModelsInContainer(uint8* ContainerPtr, const UStruct* StructType, UObject* Outer, TSet<UObject*>& Visited, int32 Depth)
	{
		if (ContainerPtr == nullptr || !IsValid(StructType) || !IsValid(Outer))
		{
			return;
		}

		for (TFieldIterator<FProperty> It(StructType); It; ++It)
		{
			FProperty* Prop = *It;
			uint8* ValuePtr = Prop->ContainerPtrToValuePtr<uint8>(ContainerPtr);

			// VM-typed object property: instantiate if null, recurse into existing otherwise.
			if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
			{
				if (!IsMockableViewModelClass(ObjProp->PropertyClass))
				{
					continue;
				}

				UObject* Existing = ObjProp->GetObjectPropertyValue(ValuePtr);
				if (!IsValid(Existing))
				{
					UObject* NewMock = NewObject<UObject>(Outer, ObjProp->PropertyClass, NAME_None, RF_Transient);
					ObjProp->SetObjectPropertyValue(ValuePtr, NewMock);
					PopulateNestedViewModels(NewMock, Outer, Visited, Depth + 1);
				}
				else
				{
					PopulateNestedViewModels(Existing, Outer, Visited, Depth + 1);
				}
			}
			// Structs may embed VM object pointers, walk their fields too.
			else if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				if (IsValid(StructProp->Struct))
				{
					PopulateNestedViewModelsInContainer(ValuePtr, StructProp->Struct, Outer, Visited, Depth);
				}
			}
			// Containers are skipped intentionally: they start empty on a fresh mock
			// and we have no basis to decide how many elements to populate.
		}
	}
} // namespace MVVMInspectorPanel 

// ---------------------------------------------------------------------------------- 
// SMVVMHierarchyRow 
// ---------------------------------------------------------------------------------- 

void SMVVMHierarchyRow::Construct(
	const FArguments& InArgs,
	const TSharedRef<STableViewBase>& InOwnerTableView,
	TSharedPtr<FMVVMHierarchyNode> InItem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!InItem.IsValid())
	{
		return;
	}

	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	const FTableRowStyle& RowStyle = InputFlowHelpers::GetTranslucentRowStyle();
	STableRow<TSharedPtr<FMVVMHierarchyNode>>::Construct(
		STableRow<TSharedPtr<FMVVMHierarchyNode>>::FArguments()
		.Padding(FMargin(0, 2))
		.ShowSelection(true)
		.Style(&RowStyle),
		InOwnerTableView
	);

	const FText HighlightText = InArgs._InspectorPanel
									? InArgs._InspectorPanel->GetHierarchySearchText()
									: FText::GetEmpty();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(2)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(/*bIsOverlay*/ true))
		.BorderBackgroundColor(MVVMInspectorPanel::OverlayBackgroundDark)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(InItem->WidgetName))
					.ColorAndOpacity(MVVMInspectorPanel::TextPrimary)
					.HighlightText(HighlightText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(InItem->ViewModelSummary))
					.ColorAndOpacity(MVVMInspectorPanel::TextSecondary)
					.HighlightText(HighlightText)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
		]
	];
}

// ---------------------------------------------------------------------------------- 
// SMVVMPropertyRow 
// ---------------------------------------------------------------------------------- 

void SMVVMPropertyRow::Construct(
	const FArguments& InArgs,
	const TSharedRef<STableViewBase>& InOwnerTableView,
	TSharedPtr<FMVVMPropertyNode> InItem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!InItem.IsValid())
	{
		return;
	}

	SetTag(InputFlowHelpers::InputFlowAnalyzerTag);

	const FTableRowStyle& RowStyle = InputFlowHelpers::GetTranslucentRowStyle();

	STableRow<TSharedPtr<FMVVMPropertyNode>>::Construct(
		STableRow<TSharedPtr<FMVVMPropertyNode>>::FArguments()
		.Padding(FMargin(0, 1))
		.ShowSelection(false)
		.Style(&RowStyle),
		InOwnerTableView
	);

	const FText HighlightText = InArgs._InspectorPanel
									? InArgs._InspectorPanel->GetPropertySearchText()
									: FText::GetEmpty();

	SetBorderBackgroundColor(MVVMInspectorPanel::TextTransparent);

	TSharedPtr<SWidget> ValueWidget = SNullWidget::NullWidget;
	if (InArgs._InspectorPanel && !InItem->bIsCategoryRoot)
	{
		ValueWidget = InArgs._InspectorPanel->CreateValueWidget(InItem);
	}

	// Outer text 
	FString OuterNameText;
	if (InItem->bIsCategoryRoot && InItem->EffectiveOwner.IsValid())
	{
		if (const UObject* OuterObj = InItem->EffectiveOwner->GetOuter())
		{
			OuterNameText = FString::Printf(TEXT("Outer: %s"), *OuterObj->GetName());
		}
	}

	const FLinearColor HeaderColor = InItem->bIsCategoryRoot ? MVVMInspectorPanel::CategoryHeaderColor : MVVMInspectorPanel::TextPrimary;
	const FLinearColor HeaderDim = MVVMInspectorPanel::TextTertiary;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(2)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(/*bIsOverlay*/ true))
		.BorderBackgroundColor(MVVMInspectorPanel::OverlayBackgroundMedium)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(InItem->DisplayName))
					.ToolTipText(FText::FromString(InItem->TooltipText))
					.ColorAndOpacity(HeaderColor)
					.HighlightText(HighlightText)
					.Font(InItem->bIsCategoryRoot
							  ? FCoreStyle::GetDefaultFontStyle("Bold", 9)
							  : FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(OuterNameText))
					.ColorAndOpacity(HeaderDim)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.Visibility(OuterNameText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			]

			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center).Padding(6, 0)
			[
				ValueWidget.ToSharedRef()
			]
		]
	];
}

// ---------------------------------------------------------------------------------- 
// SMVVMInspectorPanel 
// ---------------------------------------------------------------------------------- 

void SMVVMInspectorPanel::Construct(const FArguments& InArgs)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
	WeakPreviewToolkit = InArgs._PreviewToolkit;
	if (WeakPreviewToolkit.IsValid())
	{
		if (UWidgetPreview* Preview = WeakPreviewToolkit.Pin()->GetPreview())
		{
			PreviewWidgetChangedHandle = Preview->OnWidgetChanged().AddSP(this, &SMVVMInspectorPanel::OnPreviewWidgetChanged);
		}
	}
#endif // WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(/*bIsOverlay*/ true))
		.BorderBackgroundColor(MVVMInspectorPanel::OverlayBackgroundDark)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Widget Hierarchy (Left) 
			+ SSplitter::Slot()
			.Value(0.35f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(4)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(INVTEXT("Widget Hierarchy"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]

					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked_Lambda([this]()
						{
							RefreshHierarchy();
							return FReply::Handled();
						})
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.Refresh"))
						]
					]
				]

				// Widget Picker toolbar row
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.OnClicked(this, &SMVVMInspectorPanel::OnPickerButtonClicked)
						.IsEnabled_Lambda([this]()
						{
							// Only useful while a game instance is running, otherwise there's no viewport to pick from.
							return IsValid(InputFlowHelpers::GetActiveDebugSubsystem());
						})
						.ToolTipText_Lambda([this]() -> FText
						{
							if (!IsValid(InputFlowHelpers::GetActiveDebugSubsystem()))
							{
								return INVTEXT("Pick Widget disabled, no running game / PIE session.");
							}
							return IsPicking()
								? INVTEXT("Cancel picking (or press Esc / right-click).")
								: INVTEXT("Click a widget in the game viewport to select its owning UserWidget. Left-click to commit, right-click or Esc to cancel.");
						})
						.ButtonColorAndOpacity_Lambda([this]() -> FSlateColor
						{
							return IsPicking()
								? FSlateColor(MVVMInspectorPanel::WidgetPickerHighlightColor)
								: FSlateColor::UseStyle();
						})
						[
							SNew(STextBlock)
							.Text_Lambda([this]() -> FText
							{
								return IsPicking()
									? INVTEXT("Cancel Pick")
									: INVTEXT("Pick Widget");
							})
						]
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(6, 0, 0, 0)
					[
						// Status line, hidden unless picking is active. Shows the UMG widget currently under the cursor.
						SNew(STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(MVVMInspectorPanel::WidgetPickerHighlightColor)
						.Visibility_Lambda([this]()
						{
							return IsPicking() ? EVisibility::Visible : EVisibility::Collapsed;
						})
						.Text_Lambda([this]() -> FText
						{
							const UWidget* UMG = HoveredUMGWidget.Get();
							const UUserWidget* UW = HoveredUserWidget.Get();
							if (!IsValid(UMG) && !IsValid(UW))
							{
								return INVTEXT("Hover over a widget in the viewport...");
							}
							const FString UMGName = UMG ? UMG->GetName() : FString(TEXT("?"));
							const FString UWName = UW ? UW->GetName() : FString(TEXT("?"));
							return FText::FromString(FString::Printf(TEXT("%s  In  %s"), *UMGName, *UWName));
						})
					]
				]

				// Search Box 
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
				[
					SAssignNew(HierarchySearchBox, SSearchBox)
					.OnTextChanged(this, &SMVVMInspectorPanel::OnHierarchySearchChanged)
				]
				// Tree 
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(HierarchyTreeView, STreeView<TSharedPtr<FMVVMHierarchyNode>>)
					.TreeItemsSource(&HierarchyRootNodes)
					.OnGenerateRow(this, &SMVVMInspectorPanel::GenerateHierarchyRow)
					.OnGetChildren(this, &SMVVMInspectorPanel::OnGetHierarchyChildren)
					.OnSelectionChanged(this, &SMVVMInspectorPanel::OnHierarchySelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]
			]

			// ViewModel Properties (Right) 
			+ SSplitter::Slot()
			.Value(0.65f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(4)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(INVTEXT("Binding Sources & Properties"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				// Search Box 
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
				[
					SAssignNew(PropertySearchBox, SSearchBox)
					.OnTextChanged(this, &SMVVMInspectorPanel::OnPropertySearchChanged)
				]
				// Tree 
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(PropertyTreeView, STreeView<TSharedPtr<FMVVMPropertyNode>>)
					.TreeItemsSource(&PropertyRootNodes)
					.OnGenerateRow(this, &SMVVMInspectorPanel::GeneratePropertyRow)
					.OnGetChildren(this, &SMVVMInspectorPanel::OnGetPropertyChildren)
					.SelectionMode(ESelectionMode::None)
				]
			]
		]
	];

	RefreshHierarchy();
}

void SMVVMInspectorPanel::ResetListenedObjects()
{
	for (TWeakObjectPtr<UObject>& Elem : ListenedObjects)
	{
		if (UObject* Obj = Elem.Get(); IsValid(Obj))
		{
			if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Obj))
			{
				Notify->RemoveAllFieldValueChangedDelegates(this);
			}
		}
	}
	ListenedObjects.Empty();
}

SMVVMInspectorPanel::~SMVVMInspectorPanel()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	// Guarantee the input processor and overlay delegate bindings are torn down before the panel is gone.
	// Without this, a Tick from the processor could dereference a dead `this`.
	if (IsPicking())
	{
		EndPicking();
	}

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
	if (WeakPreviewToolkit.IsValid())
	{
		if (UWidgetPreview* Preview = WeakPreviewToolkit.Pin()->GetPreview())
		{
			Preview->OnWidgetChanged().Remove(PreviewWidgetChangedHandle);
		}
	}
#endif

	// Ensure we unregister all field-notify delegates we added. 
	ResetListenedObjects();
	
	PropertyRootNodes.Reset();
	HierarchyRootNodes.Reset();
}

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
void SMVVMInspectorPanel::OnPreviewWidgetChanged(EWidgetPreviewWidgetChangeType ChangeType)
{
	MockedViews.Empty();
	RefreshHierarchy();
}
#endif

// ---------------------------------------------------------------------------------- 
// Hierarchy (Left) 
// ---------------------------------------------------------------------------------- 

void SMVVMInspectorPanel::RefreshHierarchy()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	HierarchyRootNodes.Reset();

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	UWorld* TargetWorld = nullptr;
#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
	if (WeakPreviewToolkit.IsValid())
	{
		TargetWorld = WeakPreviewToolkit.Pin()->GetPreviewWorld();
	}
#endif

	// Collect all valid UserWidgets with MVVM views
	TMap<UUserWidget*, TSharedPtr<FMVVMHierarchyNode>> NodeMap;

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed) || Widget->IsUnreachable())
		{
			continue;
		}

		const UWorld* World = Widget->GetWorld();
		if (!IsValid(World))
		{
			continue;
		}

		// Route based on whether we're in the Editor preview or normal Game world
		if (IsValid(TargetWorld))
		{
			if (World != TargetWorld)
			{
				continue;
			}
		}
		else
		{
			if (!World->IsGameWorld())
			{
				continue;
			}
		}

		UMVVMView* FoundView = Widget->GetExtension<UMVVMView>();
		TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();

		if (!IsValid(FoundView) || !CachedWidget.IsValid())
		{
			continue;
		}

		TSharedPtr<FMVVMHierarchyNode> Node = MakeShared<FMVVMHierarchyNode>();
		Node->Widget = CachedWidget;
		Node->UserWidgetOwner = Widget;
		Node->MVVMView = FoundView;
		Node->WidgetName = Widget->GetName();

		// Inject mock ViewModels into any source slots that the resolver failed to populate
		if (IsValid(TargetWorld) && !MockedViews.Contains(FoundView))
		{
			bool bInjectedAny = false;
			const TArrayView<const FMVVMView_Source> ConstSources = FoundView->GetSources();
			FMVVMView_Source* MutableSources = const_cast<FMVVMView_Source*>(ConstSources.GetData());

			if (const UMVVMViewClass* ViewClass = FoundView->GetViewClass())
			{
				// First pass: check if any source slots need mocking
				for (int32 i = 0; i < ConstSources.Num(); ++i)
				{
					if (IsValid(MutableSources[i].Source))
					{
						continue;
					}

					const FMVVMViewClass_Source& ClassSource = ViewClass->GetSource(MutableSources[i].ClassKey);
					UClass* VMClass = ClassSource.GetSourceClass();
					if (!IsValid(VMClass) || !VMClass->IsChildOf<UMVVMViewModelBase>())
					{
						continue;
					}

					bInjectedAny = true;
					break;
				}

				if (bInjectedAny)
				{
					// Tear down current state so we can cleanly reinitialize
					FoundView->UninitializeSources();

					// Access the private ValidSources bitfield via reflection.
					// InitializeSource skips InitializeSourceInternal when bSetManually is true,
					// so the bitfield never learns about our injected source pointers.
					static FProperty* ValidSourcesProp = FindFProperty<FProperty>(UMVVMView::StaticClass(), TEXT("ValidSources"));
					uint64* ValidSourcesPtr = ValidSourcesProp ? ValidSourcesProp->ContainerPtrToValuePtr<uint64>(FoundView) : nullptr;

					// Shared visited set: if source A mocks a nested VM_X, and source B would
					// also mock a VM_X down its tree, we let B's path see A's instance instead
					// of creating a duplicate. Prevents exponential mock creation on graphs
					// that share nested types.
					TSet<UObject*> NestedVisited;

					// Inject mocks into the now-empty resolver source slots
					for (int32 i = 0; i < ConstSources.Num(); ++i)
					{
						if (IsValid(MutableSources[i].Source))
						{
							continue;
						}

						const FMVVMViewClass_Source& ClassSource = ViewClass->GetSource(MutableSources[i].ClassKey);
						UClass* VMClass = ClassSource.GetSourceClass();
						if (!IsValid(VMClass) || !VMClass->IsChildOf<UMVVMViewModelBase>())
						{
							continue;
						}

						UMVVMViewModelBase* MockVM = NewObject<UMVVMViewModelBase>(Widget, VMClass);

						// Recursively instantiate nested VM-typed properties so that deep field
						// paths (e.g. "Root.Sub.Leaf.Value") resolve correctly against the mock.
						MVVMInspectorPanel::PopulateNestedViewModels(MockVM, Widget, NestedVisited);

						MutableSources[i].Source = MockVM;
						MutableSources[i].bSetManually = true;

						if (ValidSourcesPtr != nullptr)
						{
							*ValidSourcesPtr |= MutableSources[i].ClassKey.GetBit();
						}

						// Bindings often resolve source objects through a property on the UserWidget
						// itself (not through the MVVMView's Sources array). Replicate what
						// InitializeSourceInternal would have done: copy the VM pointer into that
						// property so field paths like "MyViewModel.Name" resolve correctly.
						if (ClassSource.RequireSettingUserWidgetProperty())
						{
							if (FObjectPropertyBase* WidgetProp = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), ClassSource.GetUserWidgetPropertyName()))
							{
								if (MockVM->GetClass()->IsChildOf(WidgetProp->PropertyClass))
								{
									WidgetProp->SetObjectPropertyValue_InContainer(Widget, MockVM);
									MutableSources[i].bAssignedToUserWidgetProperty = true;
								}
							}
						}
					}

					// Reinitialize sources, bindings, and events through the public API.
					// InitializeSources may or may not set up bindings depending on
					// DoesInitializeBindingsOnConstruct(), so we call both explicitly.
					FoundView->InitializeSources();
					if (!FoundView->AreBindingsInitialized())
					{
						FoundView->InitializeBindings();
					}
					if (!FoundView->AreEventsInitialized())
					{
						FoundView->InitializeEvents();
					}

					// Force-execute all bindings to push mock VM default values to the widget.
					// InitializeSourceBindings uses bRunAllBindings=false, so only bindings
					// with ExecuteAtInitialization run. We need ALL bindings to execute.
					for (const FMVVMView_Source& VS : FoundView->GetSources())
					{
						if (IsValid(VS.Source))
						{
							const FMVVMViewClass_Source& CS = ViewClass->GetSource(VS.ClassKey);
							FoundView->ExecuteViewModelBindings(CS.GetName());
						}
					}
				}
			}

			MockedViews.Add(FoundView);
		}

		// Build Summary Text
		TArray<FString> SourceNames;
		for (const FMVVMView_Source& Source : FoundView->GetSources())
		{
			if (!MVVMInspectorPanel::ShouldReflectObject(Source.Source))
			{
				continue;
			}
			SourceNames.Add(GetNameSafe(Source.Source));
		}
		Node->ViewModelSummary = !SourceNames.IsEmpty() ? FString::Join(SourceNames, TEXT(", ")) : TEXT("No Sources");
		NodeMap.Add(Widget, Node);
	}

	// Build the tree by walking up the Slate hierarchy
	for (const auto& Pair : NodeMap)
	{
		TSharedPtr<FMVVMHierarchyNode> Node = Pair.Value;
		UUserWidget* ParentWidget = nullptr;
		if (!Node->Widget.IsValid())
		{
			continue;
		}
		TSharedPtr<SWidget> CurrentSlateWidget = Node->Widget.Pin()->GetParentWidget();
		while (CurrentSlateWidget.IsValid())
		{
			UWidget* OwnerWidget = InputFlowHelpers::GetOwnerUWidget(CurrentSlateWidget);
			if (IsValid(OwnerWidget))
			{
				UUserWidget* FoundParentWidget = Cast<UUserWidget>(OwnerWidget);
				if (IsValid(FoundParentWidget))
				{
					// If this parent is in our map, it's our direct ancestor
					if (NodeMap.Contains(FoundParentWidget))
					{
						ParentWidget = FoundParentWidget;
						break;
					}
				}
			}
			CurrentSlateWidget = CurrentSlateWidget->GetParentWidget();
		}

		// Link to parent or treat as a new root
		if (IsValid(ParentWidget) && NodeMap.Contains(ParentWidget))
		{
			NodeMap[ParentWidget]->Children.Add(Node);
		}
		else
		{
			HierarchyRootNodes.Add(Node);
		}
	}

	// Apply filtering
	if (!HierarchyFilterString.IsEmpty())
	{
		// Recursive filter
		auto ApplyFilter = [&](auto& Self, TSharedPtr<FMVVMHierarchyNode> Node) -> bool
		{
			if (!Node.IsValid())
			{
				return false;
			}
			const bool bMatchesSelf = Node->WidgetName.Contains(HierarchyFilterString) || Node->ViewModelSummary.Contains(HierarchyFilterString);
			bool bKeep = bMatchesSelf;
			for (int32 i = Node->Children.Num() - 1; i >= 0; --i)
			{
				if (!Self(Self, Node->Children[i]))
				{
					Node->Children.RemoveAt(i);
				}
				else
				{
					// Keep this node if any of its children matched
					bKeep = true;
				}
			}
			return bKeep;
		};

		for (int32 i = HierarchyRootNodes.Num() - 1; i >= 0; --i)
		{
			if (!ApplyFilter(ApplyFilter, HierarchyRootNodes[i]))
			{
				HierarchyRootNodes.RemoveAt(i);
			}
		}
	}

	if (HierarchyTreeView.IsValid())
	{
		HierarchyTreeView->RequestTreeRefresh();

		// If filtering, expand all; otherwise just expand roots 
		const bool bExpandRecursive = !HierarchyFilterString.IsEmpty();
		for (const TSharedPtr<FMVVMHierarchyNode>& Root : HierarchyRootNodes)
		{
			SetHierarchyExpansion(Root, bExpandRecursive);
		}
	}
}

void SMVVMInspectorPanel::RecursivelyBuildHierarchy(
	TSharedPtr<SWidget> InWidget,
	TSharedPtr<FMVVMHierarchyNode> ParentNode,
	TSet<UUserWidget*>& VisitedWidgets)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!InWidget.IsValid())
	{
		return;
	}

	TSharedPtr<FMVVMHierarchyNode> CurrentNode = nullptr;
	UMVVMView* FoundView = nullptr;
	UUserWidget* UserWidget = nullptr;

	// Check if this Slate Widget corresponds to a UUserWidget
	if (UWidget* OwnerUWidget = InputFlowHelpers::GetOwnerUWidget(InWidget); IsValid(OwnerUWidget))
	{
		if (UUserWidget* FoundUserWidget = Cast<UUserWidget>(OwnerUWidget); IsValid(FoundUserWidget) && !FoundUserWidget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed))
		{
			UserWidget = FoundUserWidget;
			FoundView = UserWidget->GetExtension<UMVVMView>();
		}
	}


	if (IsValid(UserWidget) && IsValid(FoundView))
	{
		if (VisitedWidgets.Contains(UserWidget))
		{
			return;
		}

		VisitedWidgets.Add(UserWidget);

		CurrentNode = MakeShared<FMVVMHierarchyNode>();
		CurrentNode->Widget = InWidget;
		CurrentNode->UserWidgetOwner = UserWidget;
		CurrentNode->MVVMView = FoundView;
		CurrentNode->WidgetName = UserWidget->GetName();

		// Build Summary Text
		TArray<FString> SourceNames;
		for (const FMVVMView_Source& Source : FoundView->GetSources())
		{
			if (MVVMInspectorPanel::ShouldReflectObject(Source.Source))
			{
				SourceNames.Add(GetNameSafe(Source.Source));
			}
		}

		CurrentNode->ViewModelSummary = !SourceNames.IsEmpty()
											? FString::Join(SourceNames, TEXT(", "))
											: TEXT("No Sources");

		if (ParentNode.IsValid())
		{
			ParentNode->Children.Add(CurrentNode);
		}
		else
		{
			// This is a top-level MVVM widget (attached directly to viewport or non-mvvm roots).
			HierarchyRootNodes.Add(CurrentNode);
		}
	}

	TSharedPtr<FMVVMHierarchyNode> NextParent = CurrentNode.IsValid() ? CurrentNode : ParentNode;

	// Recurse into Slate Children
	if (FChildren* Children = InWidget->GetChildren())
	{
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			RecursivelyBuildHierarchy(Children->GetChildAt(i), NextParent, VisitedWidgets);
		}
	}

	// Post-Recursion Filter Logic (Keep nodes only if they match search OR have matching children)
	if (CurrentNode.IsValid())
	{
		bool bMatches = HierarchyFilterString.IsEmpty();
		if (!bMatches)
		{
			bMatches = CurrentNode->WidgetName.Contains(HierarchyFilterString) ||
				CurrentNode->ViewModelSummary.Contains(HierarchyFilterString);
		}

		// If this node doesn't match search AND has no children that match, remove it from the tree.
		if (!bMatches && CurrentNode->Children.IsEmpty())
		{
			if (ParentNode.IsValid())
			{
				ParentNode->Children.Remove(CurrentNode);
			}
			else
			{
				HierarchyRootNodes.Remove(CurrentNode);
			}
		}
	}
}

TSharedRef<ITableRow> SMVVMInspectorPanel::GenerateHierarchyRow(
	TSharedPtr<FMVVMHierarchyNode> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMVVMHierarchyRow, OwnerTable, Item).InspectorPanel(this);
}

void SMVVMInspectorPanel::OnGetHierarchyChildren(
	TSharedPtr<FMVVMHierarchyNode> Item,
	TArray<TSharedPtr<FMVVMHierarchyNode>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void SMVVMInspectorPanel::OnHierarchySelectionChanged(
	TSharedPtr<FMVVMHierarchyNode> Item,
	ESelectInfo::Type /*SelectInfo*/)
{
	if (Item.IsValid())
	{
		CurrentSelection = Item;
		RebuildPropertyTree(Item);
		return;
	}

	CurrentSelection.Reset();
	PropertyRootNodes.Reset();

	if (PropertyTreeView.IsValid())
	{
		PropertyTreeView->RequestTreeRefresh();
	}
}

void SMVVMInspectorPanel::OnHierarchySearchChanged(const FText& InFilterText)
{
	HierarchyFilterString = InFilterText.ToString();
	RefreshHierarchy();
}

void SMVVMInspectorPanel::SetHierarchyExpansion(TSharedPtr<FMVVMHierarchyNode> Node, bool bExpand)
{
	if (!Node.IsValid())
	{
		return;
	}
	HierarchyTreeView->SetItemExpansion(Node, true); // Always expand root call 

	if (bExpand)
	{
		for (const auto& Child : Node->Children)
		{
			SetHierarchyExpansion(Child, true);
		}
	}
}

// ----------------------------------------------------------------------------------
// Property Tree (Right)
// ----------------------------------------------------------------------------------

void SMVVMInspectorPanel::SetupChangeListener(UObject* Object)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
		if (!IsValid(Object) || ListenedObjects.Contains(Object))
		{
			return;
		}

	if (INotifyFieldValueChanged* NotifyInterface = Cast<INotifyFieldValueChanged>(Object))
	{
		NotifyInterface->AddFieldValueChangedDelegate(
			UE::FieldNotification::FFieldId(),
			INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateSP(this, &SMVVMInspectorPanel::OnFieldChanged)
		);
		ListenedObjects.Add(Object);
	}
}

void SMVVMInspectorPanel::OnFieldChanged(UObject* Obj, UE::FieldNotification::FFieldId Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (CurrentSelection.IsValid())
	{
		RebuildPropertyTree(CurrentSelection.Pin());
	}
}

FReply SMVVMInspectorPanel::OnInvokeClicked(TSharedPtr<FMVVMPropertyNode> Node)
{
	if (!Node.IsValid())
	{
		return FReply::Handled();
	}

	if (Node->bIsFunction && Node->Function.IsValid() && Node->EffectiveOwner.IsValid())
	{
		// Fire UFunction
		uint8* ParamsMemory = Node->FunctionParams.IsValid() ? Node->FunctionParams->GetStructMemory() : nullptr;
		Node->EffectiveOwner->ProcessEvent(Node->Function.Get(), ParamsMemory);
		NotifyPropertyValueChanged(Node);
	}
	else if (Node->bIsDelegate && Node->Property.Get() && Node->Function.IsValid())
	{
		// Fire Delegate
		uint8* DelegateMemory = Node->GetRawValuePtr();
		if (DelegateMemory != nullptr)
		{
			if (const FMulticastDelegateProperty* MulticastProp = CastField<FMulticastDelegateProperty>(Node->Property.Get()))
			{
				if (const FMulticastScriptDelegate* MulticastDelegate = const_cast<FMulticastScriptDelegate*>(MulticastProp->GetMulticastDelegate(DelegateMemory)))
				{
					uint8* ParamsMemory = Node->FunctionParams.IsValid() ? Node->FunctionParams->GetStructMemory() : nullptr;
					MulticastDelegate->ProcessMulticastDelegate<UObject>(ParamsMemory);
				}
			}
			else if (const FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(Node->Property.Get()))
			{
				if (const FScriptDelegate* ScriptDelegate = DelegateProp->GetPropertyValuePtr(DelegateMemory))
				{
					if (ScriptDelegate->IsBound())
					{
						uint8* ParamsMemory = Node->FunctionParams.IsValid() ? Node->FunctionParams->GetStructMemory() : nullptr;
						ScriptDelegate->ProcessDelegate<UObject>(ParamsMemory);
					}
				}
			}
		}
		NotifyPropertyValueChanged(Node);
	}

	return FReply::Handled();
}

// ----------------------------------------------------------------------------------
// Structural Mutation (Array add/remove, Object nullify/instantiate)
// ----------------------------------------------------------------------------------

bool SMVVMInspectorPanel::IsArrayElementNode(TSharedPtr<FMVVMPropertyNode> Node)
{
	// Map value entries also carry ArrayIndex; we only treat TArray elements as "array elements" here
	// because maps don't support index-based add/remove without a key.
	if (!Node.IsValid() || Node->ArrayIndex == INDEX_NONE)
	{
		return false;
	}
	const TSharedPtr<FMVVMPropertyNode> Parent = Node->ParentNode.Pin();
	if (!Parent.IsValid())
	{
		return false;
	}
	return CastField<FArrayProperty>(Parent->Property.Get()) != nullptr;
}

void SMVVMInspectorPanel::GatherConcreteSubclasses(const UClass* BaseClass, TArray<UClass*>& OutClasses)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	OutClasses.Reset();
	if (!IsValid(BaseClass))
	{
		return;
	}

	constexpr EClassFlags Disallowed = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!IsValid(Candidate) || !Candidate->IsChildOf(BaseClass) || Candidate->HasAnyClassFlags(Disallowed))
		{
			continue;
		}
		
		if (Candidate->GetName().StartsWith(TEXT("SKEL_")) || Candidate->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		OutClasses.Add(Candidate);
	}

	// Stable sort by name so the menu order doesn't shift between invocations.
	OutClasses.Sort([](UClass& A, UClass& B)
	{
		return A.GetName() < B.GetName();
	});
}

bool SMVVMInspectorPanel::CanModifyNode(TSharedPtr<FMVVMPropertyNode> Node) const
{
	if (!Node.IsValid() || !Node->EffectiveOwner.IsValid())
	{
		return false;
	}

	// Widget instances are not mutated through the inspector - matches the existing value-edit policy.
	if (Node->EffectiveOwner->IsA(UWidget::StaticClass()))
	{
		return false;
	}

	if (const FProperty* Prop = Node->Property.Get())
	{
		// Function return values are computed, not settable.
		if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			return false;
		}
	}

	// If any ancestor is a Getter function/delegate, our parameters are effectively read-only inputs
	// used only to drive the read. Mirrors the existing bCanEdit logic in CreateValueWidget.
	for (TSharedPtr<FMVVMPropertyNode> Walker = Node->ParentNode.Pin(); Walker.IsValid(); Walker = Walker->ParentNode.Pin())
	{
		if (Walker->bIsFunction || Walker->bIsDelegate)
		{
			if (!Walker->Function.IsValid())
			{
				return false;
			}
			bool bIsGetter = Walker->Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
#if WITH_METADATA
			if (!bIsGetter)
			{
				bIsGetter = Walker->Function->HasMetaData(TEXT("Getter")) ||
							Walker->Function->HasMetaData(TEXT("BlueprintGetter"));
			}
#endif
			if (bIsGetter)
			{
				return false;
			}
			break;
		}
	}

	return true;
}

void SMVVMInspectorPanel::AddArrayElement(TSharedPtr<FMVVMPropertyNode> ArrayNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!ArrayNode.IsValid() || !CanModifyNode(ArrayNode))
	{
		return;
	}

	const FArrayProperty* ArrayProp = CastField<FArrayProperty>(ArrayNode->Property.Get());
	if (ArrayProp == nullptr || ArrayProp->Inner == nullptr)
	{
		return;
	}

	uint8* ArrayPtr = ArrayNode->GetRawValuePtr();
	if (ArrayPtr == nullptr)
	{
		return;
	}

	// Capture before the notify chain, since any FieldNotify-triggered rebuild would invalidate ArrayNode.
	const FString ArrayPath = GetNodePath(ArrayNode);
	
	FScriptArrayHelper Helper(ArrayProp, ArrayPtr);
	Helper.AddValue();

	NotifyPropertyValueChanged(ArrayNode);
	// Expand the array so the freshly-added element is reachable without a manual click.
	RebuildAfterStructuralMutation(ArrayPath, /*bExpand*/ true);
}

void SMVVMInspectorPanel::RemoveArrayElement(TSharedPtr<FMVVMPropertyNode> ElementNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!ElementNode.IsValid() || !CanModifyNode(ElementNode))
	{
		return;
	}
	if (ElementNode->ArrayIndex == INDEX_NONE)
	{
		return;
	}

	const TSharedPtr<FMVVMPropertyNode> ArrayNode = ElementNode->ParentNode.Pin();
	if (!ArrayNode.IsValid())
	{
		return;
	}

	const FArrayProperty* ArrayProp = CastField<FArrayProperty>(ArrayNode->Property.Get());
	if (ArrayProp == nullptr)
	{
		return;
	}

	uint8* ArrayPtr = ArrayNode->GetRawValuePtr();
	if (ArrayPtr == nullptr)
	{
		return;
	}

	FScriptArrayHelper Helper(ArrayProp, ArrayPtr);
	if (!Helper.IsValidIndex(ElementNode->ArrayIndex))
	{
		// Stale click: the tree may have rebuilt between the user's click and this call.
		return;
	}

	// Capture the parent array's path - the removed element's own path will no longer exist.
	const FString ArrayPath = GetNodePath(ArrayNode);

	Helper.RemoveValues(ElementNode->ArrayIndex, 1);

	// Broadcast on the array's owning class property, not the element's Inner (which has no UClass owner).
	NotifyPropertyValueChanged(ArrayNode);
	// Scroll to (but don't forcibly expand) the containing array - preserved-expansion handles the rest.
	RebuildAfterStructuralMutation(ArrayPath, /*bExpand*/ false);
}

void SMVVMInspectorPanel::NullifyObjectProperty(TSharedPtr<FMVVMPropertyNode> ObjectNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!ObjectNode.IsValid() || !CanModifyNode(ObjectNode))
	{
		return;
	}

	const FObjectProperty* ObjProp = CastField<FObjectProperty>(ObjectNode->Property.Get());
	if (ObjProp == nullptr)
	{
		return;
	}

	uint8* ValuePtr = ObjectNode->GetRawValuePtr();
	if (ValuePtr == nullptr)
	{
		return;
	}

	const FString ObjectPath = GetNodePath(ObjectNode);

	// Previously-held object (if any) becomes unreferenced from this slot and will be collected
	// on the next GC pass unless something else holds a reference.
	ObjProp->SetObjectPropertyValue(ValuePtr, /*Value*/ nullptr);

	NotifyPropertyValueChanged(ObjectNode);
	// No expand, the slot is now null and has no children to show.
	RebuildAfterStructuralMutation(ObjectPath, /*bExpand*/ false);
}

void SMVVMInspectorPanel::InstantiateObjectProperty(TSharedPtr<FMVVMPropertyNode> ObjectNode, const UClass* ClassToUse)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!ObjectNode.IsValid() || !IsValid(ClassToUse) || !CanModifyNode(ObjectNode))
	{
		return;
	}

	const FObjectProperty* ObjProp = CastField<FObjectProperty>(ObjectNode->Property.Get());
	if (ObjProp == nullptr || !IsValid(ObjProp->PropertyClass))
	{
		return;
	}

	if (!ClassToUse->IsChildOf(ObjProp->PropertyClass))
	{
		return;
	}
	if (ClassToUse->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return;
	}

	UObject* Outer = ObjectNode->EffectiveOwner.Get();
	if (!IsValid(Outer))
	{
		return;
	}

	uint8* ValuePtr = ObjectNode->GetRawValuePtr();
	if (ValuePtr == nullptr)
	{
		return;
	}

	const FString ObjectPath = GetNodePath(ObjectNode);
	
	UObject* NewInstance = NewObject<UObject>(Outer, ClassToUse, NAME_None, RF_Transient);
	if (!IsValid(NewInstance))
	{
		return;
	}

	ObjProp->SetObjectPropertyValue(ValuePtr, NewInstance);

	NotifyPropertyValueChanged(ObjectNode);
	// Expand so the new instance's properties are immediately visible.
	RebuildAfterStructuralMutation(ObjectPath, /*bExpand*/ true);
}

// ----------------------------------------------------------------------------------
// Tree Expansion Persistence
// ----------------------------------------------------------------------------------

FString SMVVMInspectorPanel::GetNodePath(TSharedPtr<FMVVMPropertyNode> Node) const
{
	if (!Node.IsValid())
	{
		return FString();
	}

	// Walk leaf-to-root, collecting segments. VM roots prefix "ROOT:", function nodes prefix
	// "FN:", regular properties use the property FName (+ index for array/map entries).
	TArray<FString, TInlineAllocator<16>> Segments;
	for (TSharedPtr<FMVVMPropertyNode> Walker = Node; Walker.IsValid(); Walker = Walker->ParentNode.Pin())
	{
		FString Seg;
		if (Walker->bIsCategoryRoot)
		{
			Seg = FString::Printf(TEXT("ROOT:%s"), *Walker->DisplayName);
		}
		else if (Walker->bIsFunction && Walker->Function.IsValid())
		{
			Seg = FString::Printf(TEXT("FN:%s"), *Walker->Function->GetName());
		}
		else if (const FProperty* Prop = Walker->Property.Get())
		{
			if (Walker->ArrayIndex != INDEX_NONE)
			{
				Seg = FString::Printf(TEXT("%s[%d]"), *Prop->GetName(), Walker->ArrayIndex);
			}
			else
			{
				Seg = Prop->GetName();
			}
		}
		else
		{
			Seg = Walker->DisplayName;
		}
		Segments.Add(MoveTemp(Seg));
	}

	int32 TotalLen = 0;
	for (const FString& S : Segments)
	{
		TotalLen += S.Len() + 1;
	}
	FString Result;
	Result.Reserve(TotalLen);
	for (int32 i = Segments.Num() - 1; i >= 0; --i)
	{
		if (i < Segments.Num() - 1)
		{
			Result += TEXT("/");
		}
		Result += Segments[i];
	}
	return Result;
}

void SMVVMInspectorPanel::CaptureExpandedNodePaths(TSet<FString>& OutPaths) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!PropertyTreeView.IsValid())
	{
		return;
	}

	// Iterative depth-first walk. Avoids recursion on potentially-deep trees.
	TArray<TSharedPtr<FMVVMPropertyNode>> Pending;
	Pending.Reserve(64);
	for (const TSharedPtr<FMVVMPropertyNode>& Root : PropertyRootNodes)
	{
		Pending.Add(Root);
	}

	for (int32 i = 0; i < Pending.Num(); ++i)
	{
		const TSharedPtr<FMVVMPropertyNode>& Node = Pending[i];
		if (!Node.IsValid())
		{
			continue;
		}

		if (PropertyTreeView->IsItemExpanded(Node))
		{
			FString Path = GetNodePath(Node);
			if (!Path.IsEmpty())
			{
				OutPaths.Add(MoveTemp(Path));
			}
		}

		for (const TSharedPtr<FMVVMPropertyNode>& Child : Node->Children)
		{
			Pending.Add(Child);
		}
	}
}

void SMVVMInspectorPanel::ApplyExpandedNodePaths(const TSet<FString>& Paths)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!PropertyTreeView.IsValid() || Paths.IsEmpty())
	{
		return;
	}

	TArray<TSharedPtr<FMVVMPropertyNode>> Pending;
	Pending.Reserve(64);
	for (const TSharedPtr<FMVVMPropertyNode>& Root : PropertyRootNodes)
	{
		Pending.Add(Root);
	}

	for (int32 i = 0; i < Pending.Num(); ++i)
	{
		const TSharedPtr<FMVVMPropertyNode>& Node = Pending[i];
		if (!Node.IsValid())
		{
			continue;
		}

		if (Paths.Contains(GetNodePath(Node)))
		{
			PropertyTreeView->SetItemExpansion(Node, true);
		}

		for (const TSharedPtr<FMVVMPropertyNode>& Child : Node->Children)
		{
			Pending.Add(Child);
		}
	}
}

void SMVVMInspectorPanel::FocusNodeByPath(const FString& Path, const bool bExpand)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!PropertyTreeView.IsValid() || Path.IsEmpty())
	{
		return;
	}

	TArray<TSharedPtr<FMVVMPropertyNode>> Pending;
	Pending.Reserve(64);
	for (const TSharedPtr<FMVVMPropertyNode>& Root : PropertyRootNodes)
	{
		Pending.Add(Root);
	}

	for (int32 i = 0; i < Pending.Num(); ++i)
	{
		const TSharedPtr<FMVVMPropertyNode>& Node = Pending[i];
		if (!Node.IsValid())
		{
			continue;
		}

		if (GetNodePath(Node) == Path)
		{
			// Expand the chain of ancestors so the node is actually reachable in the list virtualizer.
			// Without this, RequestScrollIntoView has nothing to scroll to.
			for (TSharedPtr<FMVVMPropertyNode> Parent = Node->ParentNode.Pin(); Parent.IsValid(); Parent = Parent->ParentNode.Pin())
			{
				PropertyTreeView->SetItemExpansion(Parent, true);
			}

			if (bExpand)
			{
				PropertyTreeView->SetItemExpansion(Node, true);
			}

			PropertyTreeView->RequestScrollIntoView(Node);
			return;
		}

		for (const TSharedPtr<FMVVMPropertyNode>& Child : Node->Children)
		{
			Pending.Add(Child);
		}
	}
}

void SMVVMInspectorPanel::RebuildAfterStructuralMutation(const FString& FocusPath, const bool bExpand)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	RebuildPropertyTree(CurrentSelection.Pin());
	FocusNodeByPath(FocusPath, bExpand);
}

// ----------------------------------------------------------------------------------
// Widget Picker
// ----------------------------------------------------------------------------------

bool SMVVMInspectorPanel::IsPathInGameViewport(const FWidgetPath& Path)
{
	static const FName TypeGameLayerManager(TEXT("SGameLayerManager"));
	static const FName TypeViewport(TEXT("SViewport"));

	for (int32 i = 0; i < Path.Widgets.Num(); ++i)
	{
		const FName Type = Path.Widgets[i].Widget->GetType();
		if (Type == TypeGameLayerManager || Type == TypeViewport)
		{
			return true;
		}
	}
	return false;
}

UUserWidget* SMVVMInspectorPanel::ResolvePickedUserWidget(const FWidgetPath& WidgetPath)
{
	if (!WidgetPath.IsValid() || WidgetPath.Widgets.Num() == 0)
	{
		return nullptr;
	}

	// Walk leaf-to-root. The deepest SWidget with a reflection-metadata UWidget source is our pick.
	// From there the innermost owning UUserWidget wins (outer-chain walk stops at first match).
	for (int32 i = WidgetPath.Widgets.Num() - 1; i >= 0; --i)
	{
		UWidget* W = InputFlowHelpers::GetOwnerUWidget(WidgetPath.Widgets[i].Widget);
		if (!IsValid(W))
		{
			continue;
		}

		// Fast path: helper already resolved a UserWidget (e.g. root of a widget tree).
		if (UUserWidget* UW = Cast<UUserWidget>(W))
		{
			return UW;
		}

		// Common case: helper resolved a child UWidget. Walk outward for the owning UserWidget.
		for (UObject* Outer = W->GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			if (UUserWidget* Owner = Cast<UUserWidget>(Outer))
			{
				return Owner;
			}
		}
	}
	return nullptr;
}

namespace MVVMInspectorPanelPicker_Detail
{
	// Depth-first search through the hierarchy, building the ancestor path to Target as we go.
	// On a match, OutPath contains [root, ..., match]; otherwise it's untouched by a successful early exit.
	static bool FindAncestorPath(
		TSharedPtr<FMVVMHierarchyNode> Current,
		const UUserWidget* Target,
		TArray<TSharedPtr<FMVVMHierarchyNode>>& OutPath)
	{
		if (!Current.IsValid())
		{
			return false;
		}

		OutPath.Add(Current);

		if (Current->UserWidgetOwner.Get() == Target)
		{
			return true;
		}

		for (const TSharedPtr<FMVVMHierarchyNode>& Child : Current->Children)
		{
			if (FindAncestorPath(Child, Target, OutPath))
			{
				return true;
			}
		}

		OutPath.Pop();
		return false;
	}
}

void SMVVMInspectorPanel::SelectUserWidgetInHierarchy(const UUserWidget* Target)
{
	if (!IsValid(Target) || !HierarchyTreeView.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FMVVMHierarchyNode>> AncestorPath;
	for (const TSharedPtr<FMVVMHierarchyNode>& Root : HierarchyRootNodes)
	{
		AncestorPath.Reset();
		if (MVVMInspectorPanelPicker_Detail::FindAncestorPath(Root, Target, AncestorPath))
		{
			// Expand every ancestor so the selection is reachable.
			for (const TSharedPtr<FMVVMHierarchyNode>& Node : AncestorPath)
			{
				HierarchyTreeView->SetItemExpansion(Node, true);
			}
			const TSharedPtr<FMVVMHierarchyNode>& Match = AncestorPath.Last();
			TWeakPtr<STreeView<TSharedPtr<FMVVMHierarchyNode>>> WeakTree = HierarchyTreeView;

			// Wait a tick for expansion
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([WeakTree, Match](double, float) -> EActiveTimerReturnType
			{
				if (const TSharedPtr<STreeView<TSharedPtr<FMVVMHierarchyNode>>> Tree = WeakTree.Pin())
				{
					Tree->SetSelection(Match);
					Tree->RequestScrollIntoView(Match);
				}
				return EActiveTimerReturnType::Stop;
			}));
			HierarchyTreeView->SetSelection(Match);
			HierarchyTreeView->RequestScrollIntoView(Match);
			return;
		}
	}

	// No match in the current tree, the picked widget isn't part of any MVVM-annotated UserWidget in our hierarchy.
}

void SMVVMInspectorPanel::BeginPicking(const EMVVMPickingMode NewMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (NewMode == EMVVMPickingMode::None || PickingMode != EMVVMPickingMode::None)
	{
		// Already picking or nothing to do.
		return;
	}

	UInputDebugSubsystem* Sub = InputFlowHelpers::GetActiveDebugSubsystem();
	if (!IsValid(Sub))
	{
		return;
	}

	PickingMode = NewMode;

	// Bind to overlay draw/label hooks to use the existing SInputFlowOverlay renders
	BoundPickerSubsystem = Sub;
	PickerDrawOverlayHandle = BoundPickerSubsystem->GetOnDrawOverlay().AddSP(this, &SMVVMInspectorPanel::OnPickerDrawOverlay);
	PickerGatherLabelsHandle = BoundPickerSubsystem->GetOnGatherLabels().AddSP(this, &SMVVMInspectorPanel::OnPickerGatherLabels);

	// Install the input preprocessor. SharedThis keeps us alive for the processor's lifetime, and
	// the processor weak-captures us so late ticks after teardown no-op.
	PickerProcessor = MakeShared<FMVVMInspectorPickerInputProcessor>(SharedThis(this));
	FSlateApplication::Get().RegisterInputPreProcessor(PickerProcessor);
}

void SMVVMInspectorPanel::EndPicking()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (PickingMode == EMVVMPickingMode::None)
	{
		return;
	}

	// Clear mode first so processor ticks during teardown early-out.
	PickingMode = EMVVMPickingMode::None;

	if (PickerProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(PickerProcessor);
	}
	PickerProcessor.Reset();

	// Unbind overlay hooks using the subsystem we originally bound to
	if (UInputDebugSubsystem* Sub = BoundPickerSubsystem.Get())
	{
		if (PickerDrawOverlayHandle.IsValid())
		{
			Sub->GetOnDrawOverlay().Remove(PickerDrawOverlayHandle);
		}
		if (PickerGatherLabelsHandle.IsValid())
		{
			Sub->GetOnGatherLabels().Remove(PickerGatherLabelsHandle);
		}
	}
	PickerDrawOverlayHandle.Reset();
	PickerGatherLabelsHandle.Reset();
	BoundPickerSubsystem.Reset();

	HoveredSlateWidget.Reset();
	HoveredUserWidget.Reset();
	HoveredUMGWidget.Reset();
}

void SMVVMInspectorPanel::UpdatePickingHover()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!IsPicking())
	{
		return;
	}

	FSlateApplication& SlateApp = FSlateApplication::Get();
	const FVector2f CursorPos = SlateApp.GetCursorPos();

	FWidgetPath WidgetPath = SlateApp.LocateWindowUnderMouse(
		CursorPos,
		SlateApp.GetInteractiveTopLevelWindows(),
		/*bIgnoreEnabledStatus*/ true
	);
	
	if (!WidgetPath.IsValid() || !IsPathInGameViewport(WidgetPath))
	{
		HoveredSlateWidget.Reset();
		HoveredUserWidget.Reset();
		HoveredUMGWidget.Reset();
		return;
	}

	// Determine the deepest UMG widget and its owning UserWidget.
	UWidget* DeepestUMG = nullptr;
	TSharedPtr<SWidget> DeepestSlate;
	for (int32 i = WidgetPath.Widgets.Num() - 1; i >= 0; --i)
	{
		TSharedRef<SWidget> SlateWidget = WidgetPath.Widgets[i].Widget;
		if (UWidget* Widget = InputFlowHelpers::GetOwnerUWidget(SlateWidget))
		{
			DeepestUMG = Widget;
			DeepestSlate = SlateWidget;
			break;
		}
	}

	UUserWidget* OwningUW = ResolvePickedUserWidget(WidgetPath);

	HoveredSlateWidget = DeepestSlate;
	HoveredUMGWidget = DeepestUMG;
	HoveredUserWidget = OwningUW;
}

void SMVVMInspectorPanel::CommitPick()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	const UUserWidget* Target = HoveredUserWidget.Get();

	// Always exit picking mode, regardless of whether we found a valid target. The user has decided
	// by clicking; leaving them stuck in picking mode after a missed click is worse than silently
	// doing nothing.
	EndPicking();

	if (IsValid(Target))
	{
		RefreshHierarchy();
		SelectUserWidgetInHierarchy(Target);
	}
}

void SMVVMInspectorPanel::CancelPick()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	EndPicking();
}

FReply SMVVMInspectorPanel::OnPickerButtonClicked()
{
	if (IsPicking())
	{
		EndPicking();
	}
	else
	{
		BeginPicking(EMVVMPickingMode::HitTestable);
	}
	return FReply::Handled();
}

void SMVVMInspectorPanel::OnPickerDrawOverlay(UInputDebugSubsystem* /*Subsystem*/, FInputFlowDrawAPI& DrawAPI)
{
	if (!IsPicking())
	{
		return;
	}
	const TSharedPtr<SWidget> Hovered = HoveredSlateWidget.Pin();
	if (!Hovered.IsValid())
	{
		return;
	}
	DrawAPI.DrawWidgetHighlight(Hovered, MVVMInspectorPanel::WidgetPickerHighlightColor, /*Thickness*/ 2.5f);
}

void SMVVMInspectorPanel::OnPickerGatherLabels(UInputDebugSubsystem* /*Subsystem*/, FInputFlowLabelAPI& LabelAPI)
{
	if (!IsPicking())
	{
		return;
	}
	TSharedPtr<SWidget> Hovered = HoveredSlateWidget.Pin();
	if (!Hovered.IsValid())
	{
		return;
	}

	const UWidget* UMG = HoveredUMGWidget.Get();
	const UUserWidget* UW = HoveredUserWidget.Get();

	FString Label;
	if (IsValid(UMG) && IsValid(UW) && UMG != UW)
	{
		Label = FString::Printf(TEXT("%s  In  %s"), *UMG->GetName(), *UW->GetName());
	}
	else if (IsValid(UW))
	{
		Label = UW->GetName();
	}
	else if (IsValid(UMG))
	{
		Label = UMG->GetName();
	}
	else
	{
		// Resolved to a slate widget but no UMG source - still show what it is for debugging.
		Label = Hovered->GetTypeAsString();
	}

	LabelAPI.QueueWidgetLabel(Hovered, Label, MVVMInspectorPanel::WidgetPickerHighlightColor);
}

void SMVVMInspectorPanel::RebuildPropertyTree(TSharedPtr<FMVVMHierarchyNode> SelectedNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	// Preserve expansion only when we're rebuilding the same view with the same filter - i.e. notify-
	// or mutation-triggered refreshes. Selection changes and filter edits fall back to filter-based
	// defaults so they don't carry stale expansion from an unrelated tree.
	const bool bPreserveExpansion =
		SelectedNode.IsValid() &&
		SelectedNode == LastRebuiltSelection.Pin() &&
		PropertyFilterString == LastRebuiltFilterString;

	TSet<FString> PreservedExpandedPaths;
	if (bPreserveExpansion)
	{
		CaptureExpandedNodePaths(PreservedExpandedPaths);
	}

	ResetListenedObjects();
	PropertyRootNodes.Reset();

	if (!SelectedNode.IsValid())
	{
		if (PropertyTreeView.IsValid()) PropertyTreeView->RequestTreeRefresh();
		LastRebuiltSelection = SelectedNode;
		LastRebuiltFilterString = PropertyFilterString;
		return;
	}

	// Regenerate Tree
	if (SelectedNode->MVVMView.IsValid())
	{
		const UMVVMView* View = SelectedNode->MVVMView.Get();
		if (IsValid(View))
		{
			for (const FMVVMView_Source& Source : View->GetSources())
			{
				UObject* VM = Source.Source;
				// Note: We skip Widget objects to avoid clutter
				if (!IsValid(VM) || MVVMInspectorPanel::IsWidgetObject(VM))
				{
					continue;
				}

				const TSharedPtr<FMVVMPropertyNode> VMRoot = MakeShared<FMVVMPropertyNode>();
				VMRoot->DisplayName = FString::Printf(TEXT("ViewModel: %s"), *VM->GetName());
				VMRoot->TypeName = VM->GetClass()->GetName();
				VMRoot->TooltipText = FString::Printf(TEXT("%s (%s)"), *VM->GetName(), *VM->GetClass()->GetName());
				VMRoot->EffectiveOwner = VM;
				VMRoot->bIsCategoryRoot = true;

				// TODO: Future optimization could be to only track values for nodes that are actually expanded
				// Depending on the VM complexity, the current approach can result in a big hitch
				VMRoot->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(VM), VM->GetClass(), VM, VMRoot);

				bool bMatches = PropertyFilterString.IsEmpty();
				if (!bMatches)
				{
					bMatches = VMRoot->DisplayName.Contains(PropertyFilterString) || VMRoot->TypeName.Contains(
						PropertyFilterString);
				}

				if (bMatches || !VMRoot->Children.IsEmpty())
				{
					PropertyRootNodes.Add(VMRoot);
					SetupChangeListener(VM);
				}
			}
		}
	}

	// Apply Expansion State
	if (PropertyTreeView.IsValid())
	{
		PropertyTreeView->RequestTreeRefresh();

		if (bPreserveExpansion && !PreservedExpandedPaths.IsEmpty())
		{
			// Carry expansion across rebuilds
			ApplyExpandedNodePaths(PreservedExpandedPaths);
		}
		else
		{
			// Fresh build (first time, new selection, or filter changed): use filter-driven defaults.
			const bool bIsFiltering = !PropertyFilterString.IsEmpty();
			for (const TSharedPtr<FMVVMPropertyNode>& Node : PropertyRootNodes)
			{
				SetPropertyExpansion(Node, bIsFiltering);
			}
		}
	}

	LastRebuiltSelection = SelectedNode;
	LastRebuiltFilterString = PropertyFilterString;
}

// ---- Special Struct Handling ----

bool SMVVMInspectorPanel::IsSpecialStruct(const UScriptStruct* Struct) const
{
	if (!IsValid(Struct))
	{
		return false;
	}

	const FName StructName = Struct->GetFName();
	return StructName == MVVMInspectorPanel::NAME_GameplayTag
		|| StructName == MVVMInspectorPanel::NAME_GameplayTagContainer
		|| StructName == MVVMInspectorPanel::NAME_Vector
		|| StructName == MVVMInspectorPanel::NAME_Vector2D
		|| StructName == MVVMInspectorPanel::NAME_Rotator
		|| StructName == MVVMInspectorPanel::NAME_Color
		|| StructName == MVVMInspectorPanel::NAME_LinearColor;
}

FString SMVVMInspectorPanel::GetSpecialStructValue(const UScriptStruct* Struct, const void* ValuePtr) const
{
	if (!IsValid(Struct) || ValuePtr == nullptr)
	{
		return TEXT("Error");
	}

	const FName StructName = Struct->GetFName();

	if (StructName == MVVMInspectorPanel::NAME_GameplayTag)
	{
		const FGameplayTag* GameplayTag = static_cast<const FGameplayTag*>(ValuePtr);
		return GameplayTag->ToString();
	}

	if (StructName == MVVMInspectorPanel::NAME_GameplayTagContainer)
	{
		const FGameplayTagContainer* TagContainer = static_cast<const FGameplayTagContainer*>(ValuePtr);
		return TagContainer->ToString();
	}

	FString OutString;
	Struct->ExportText(OutString, ValuePtr, ValuePtr, nullptr, PPF_None, nullptr);
	return OutString;
}

void SMVVMInspectorPanel::SetSpecialStructValue(TSharedPtr<FMVVMPropertyNode> Node, uint8* TargetPtr, const FString& NewStringValue)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!Node.IsValid() || TargetPtr == nullptr)
	{
		return;
	}

	const FStructProperty* StructProp = CastField<FStructProperty>(Node->Property.Get());

	if (StructProp == nullptr || !IsValid(StructProp->Struct))
	{
		return;
	}

	const FName StructName = StructProp->Struct->GetFName();

	// Handle FGameplayTag
	if (StructName == MVVMInspectorPanel::NAME_GameplayTag && NewStringValue != TEXT("None"))
	{
		FString CleanValue = NewStringValue;
		CleanValue.TrimStartAndEndInline();

		const FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(FName(*CleanValue));
		StructProp->Struct->CopyScriptStruct(TargetPtr, &NewTag);
	}
	// Handle FGameplayTagContainer
	else if (StructName == MVVMInspectorPanel::NAME_GameplayTagContainer)
	{
		FGameplayTagContainer NewContainer;

		TArray<FString> TagStrings;
		NewStringValue.ParseIntoArray(TagStrings, TEXT(","), true);

		for (FString& TagStr : TagStrings)
		{
			// Trim each individual tag in the comma-separated list
			TagStr.TrimStartAndEndInline();
			if (!TagStr.IsEmpty())
			{
				NewContainer.AddTag(FGameplayTag::RequestGameplayTag(FName(*TagStr)));
			}
		}

		StructProp->Struct->CopyScriptStruct(TargetPtr, &NewContainer);
	}
	// Fallback for others (Vector, etc)
	else
	{
		Node->Property->ImportText_Direct(*NewStringValue, TargetPtr, Node->EffectiveOwner.Get(), PPF_None);
	}
}

// ---- Reflection Walking ----

TArray<TSharedPtr<FMVVMPropertyNode>> SMVVMInspectorPanel::GeneratePropertyNodes(
	uint8* BaseAddress,
	const UStruct* StructLayout,
	UObject* OwnerObject,
	TSharedPtr<FMVVMPropertyNode> Parent,
	int32 CurrentDepth,
	int32 MaxDepth)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	TArray<TSharedPtr<FMVVMPropertyNode>> Nodes;
	// Depth guard 
	if (CurrentDepth >= MaxDepth)
	{
		const TSharedPtr<FMVVMPropertyNode> DepthNode = MakeShared<FMVVMPropertyNode>();
		DepthNode->DisplayName = TEXT("Max Depth Reached");
		DepthNode->TypeName = FString::Printf(TEXT("Stopped at depth %d (MaxDepth=%d)"), CurrentDepth, MaxDepth);
		DepthNode->EffectiveOwner = OwnerObject;
		DepthNode->ParentNode = Parent;
		Nodes.Add(DepthNode);
		return Nodes;
	}

	if (BaseAddress == nullptr || !IsValid(StructLayout) || !IsValid(OwnerObject) || !Parent.IsValid())
	{
		return Nodes;
	}

	// Hard stop: never recurse into widget instances. 
	if (MVVMInspectorPanel::IsWidgetObject(OwnerObject))
	{
		return Nodes;
	}

	// Detect cycles when following UObject references. 
	auto IsObjectInAncestry = [&](const UObject* Obj) -> bool
	{
		if (!IsValid(Obj))
		{
			return false;
		}

		if (Obj == OwnerObject)
		{
			return true;
		}

		TSharedPtr<FMVVMPropertyNode> Ancestor = Parent;
		while (Ancestor.IsValid())
		{
			if (Ancestor->EffectiveOwner.Get() == Obj)
			{
				return true;
			}
			Ancestor = Ancestor->ParentNode.Pin();
		}
		return false;
	};

	for (TFieldIterator<FProperty> PropIt(StructLayout); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (Prop == nullptr)
		{
			continue;
		}

		// Skip widget-typed properties/arrays, and also skip generic UObject* values that currently point to widgets. 
		// I'm only interested in ViewModel data, and in practice a ViewModel pointing to a widget is unusual. 
		bool bSkip = false;
		if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			if (IsValid(ObjProp->PropertyClass) && ObjProp->PropertyClass->IsChildOf(UWidget::StaticClass()))
			{
				bSkip = true;
			}
			else
			{
				const uint8* TempValPtr = Prop->ContainerPtrToValuePtr<uint8>(BaseAddress);
				const UObject* TempObj = ObjProp->GetObjectPropertyValue(TempValPtr);
				if (MVVMInspectorPanel::IsWidgetObject(TempObj))
				{
					bSkip = true;
				}
			}
		}
		else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			if (const FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
			{
				if (IsValid(InnerObjProp->PropertyClass) && InnerObjProp->PropertyClass->IsChildOf(UWidget::StaticClass()))
				{
					bSkip = true;
				}
			}
		}

		if (bSkip)
		{
			continue;
		}

		const TSharedPtr<FMVVMPropertyNode> Node = MakeShared<FMVVMPropertyNode>();
		Node->DisplayName = Prop->GetName();
		Node->TypeName = Prop->GetCPPType();
		Node->EffectiveOwner = OwnerObject;
		Node->Property = Prop;
#if WITH_EDITORONLY_DATA
		Node->TooltipText = Prop->GetToolTipText().ToString();
#endif
		Node->ParentNode = Parent;

		uint8* PropValuePtr = Prop->ContainerPtrToValuePtr<uint8>(BaseAddress);

		// Delegates
		if (FMulticastDelegateProperty* MulticastProp = CastField<FMulticastDelegateProperty>(Prop))
		{
			Node->bIsDelegate = true;
			Node->Function = MulticastProp->SignatureFunction;
			Node->TypeName = FString::Printf(TEXT("MulticastDelegate (%s)"), Node->Function.IsValid() ? *Node->Function->GetName() : TEXT("Unknown"));

			if (Node->Function.IsValid() && Node->Function->GetStructureSize() > 0)
			{
				Node->FunctionParams = MakeShared<FStructOnScope>(Node->Function.Get());
				Node->Children = GeneratePropertyNodes(Node->FunctionParams->GetStructMemory(), Node->Function.Get(), OwnerObject, Node, CurrentDepth + 1, MaxDepth);
#if WITH_EDITORONLY_DATA
				Node->TooltipText = Node->Function->GetToolTipText().ToString();
#endif
			}
		}
		else if (FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(Prop))
		{
			Node->bIsDelegate = true;
			Node->Function = DelegateProp->SignatureFunction;
			Node->TypeName = FString::Printf(TEXT("Delegate (%s)"), Node->Function.IsValid() ? *Node->Function->GetName() : TEXT("Unknown"));

			if (Node->Function.IsValid() && Node->Function->GetStructureSize() > 0)
			{
				Node->FunctionParams = MakeShared<FStructOnScope>(Node->Function.Get());
				Node->Children = GeneratePropertyNodes(Node->FunctionParams->GetStructMemory(), Node->Function.Get(), OwnerObject, Node, CurrentDepth + 1, MaxDepth);
#if WITH_EDITORONLY_DATA
				Node->TooltipText = Node->Function->GetToolTipText().ToString();
#endif
			}
		}
		// Arrays 
		else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrayProp, PropValuePtr);
			Node->TypeName = FString::Printf(TEXT("Array[%d] of %s"), Helper.Num(), *ArrayProp->Inner->GetCPPType());

			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				const TSharedPtr<FMVVMPropertyNode> ElementNode = MakeShared<FMVVMPropertyNode>();
				ElementNode->DisplayName = FString::Printf(TEXT("[%d]"), i);
				ElementNode->TypeName = ArrayProp->Inner->GetCPPType();
				ElementNode->EffectiveOwner = OwnerObject;
				ElementNode->Property = ArrayProp->Inner;
#if WITH_EDITORONLY_DATA
				ElementNode->TooltipText = ArrayProp->Inner->GetToolTipText().ToString();
#endif
				ElementNode->ParentNode = Node;
				ElementNode->ArrayIndex = i;

				uint8* ElementAddr = Helper.GetRawPtr(i);

				if (FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner))
				{
					if (IsValid(InnerStructProp->Struct) && !IsSpecialStruct(InnerStructProp->Struct))
					{
						// Instanced Struct inside array element: reflect the payload, not the wrapper struct
						if (InnerStructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_InstancedStruct)
						{
							FInstancedStruct* InstancedStruct = reinterpret_cast<FInstancedStruct*>(ElementAddr);
							if (InstancedStruct != nullptr && InstancedStruct->IsValid())
							{
								const UScriptStruct* ScriptStruct = InstancedStruct->GetScriptStruct();
								uint8* Memory = InstancedStruct->GetMutableMemory();

								ElementNode->TypeName = FString::Printf(TEXT("InstancedStruct (%s)"), *ScriptStruct->GetName());
								ElementNode->Children = GeneratePropertyNodes(Memory, ScriptStruct, OwnerObject,
																			  ElementNode, CurrentDepth + 1);
							}
							else
							{
								ElementNode->TypeName = TEXT("InstancedStruct (Empty)");
							}
						}
						else
						{
							ElementNode->Children = GeneratePropertyNodes(ElementAddr, InnerStructProp->Struct, OwnerObject,
																		  ElementNode, CurrentDepth + 1);
						}
					}
				}

				else if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
				{
					UObject* SubObj = InnerObjProp->GetObjectPropertyValue(ElementAddr);
					if (MVVMInspectorPanel::ShouldReflectObject(SubObj))
					{
						if (!IsObjectInAncestry(SubObj))
						{
							ElementNode->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(SubObj),
																		  SubObj->GetClass(), SubObj, ElementNode,
																		  CurrentDepth + 1);
							SetupChangeListener(SubObj);
						}
						else
						{
							ElementNode->TypeName += TEXT(" (Cycle Detected)");
							ElementNode->Children.Empty();
						}
					}
				}

				// Filter Logic: Array element only shows if it has matching children (path to match) 
				// OR if the search is empty (show everything). 
				if (ElementNode->Children.Num() > 0 || PropertyFilterString.IsEmpty())
				{
					Node->Children.Add(ElementNode);
				}
			}
		}

		// Maps
		else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			FScriptMapHelper Helper(MapProp, PropValuePtr);
			Node->TypeName = FString::Printf(TEXT("Map[%d]"), Helper.Num());

			for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
			{
				if (Helper.IsValidIndex(i) && MapProp->ValueProp != nullptr)
				{
					FProperty* KeyProp = Helper.GetKeyProperty();
					if (KeyProp == nullptr)
					{
						continue;
					}
					const TSharedPtr<FMVVMPropertyNode> EntryNode = MakeShared<FMVVMPropertyNode>();
					FString KeyStr;
					KeyProp->ExportText_Direct(KeyStr, Helper.GetKeyPtr(i), nullptr, nullptr, PPF_None);

					EntryNode->DisplayName = FString::Printf(TEXT("[%s]"), *KeyStr);
					EntryNode->TypeName = MapProp->ValueProp->GetCPPType();
					EntryNode->EffectiveOwner = OwnerObject;
					EntryNode->Property = MapProp->ValueProp;
#if WITH_EDITORONLY_DATA
					EntryNode->TooltipText = MapProp->ValueProp->GetToolTipText().ToString();
#endif
					EntryNode->ParentNode = Node;
					EntryNode->ArrayIndex = i;

					uint8* ValueAddr = Helper.GetValuePtr(i);
					if (FStructProperty* ValueStructProp = CastField<FStructProperty>(MapProp->ValueProp))
					{
						if (IsValid(ValueStructProp->Struct) && !IsSpecialStruct(ValueStructProp->Struct))
						{
							// Instanced Struct inside map element: reflect the payload, not the wrapper struct
							if (ValueStructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_InstancedStruct)
							{
								FInstancedStruct* InstancedStruct = reinterpret_cast<FInstancedStruct*>(ValueAddr);
								if (InstancedStruct != nullptr && InstancedStruct->IsValid())
								{
									const UScriptStruct* ScriptStruct = InstancedStruct->GetScriptStruct();
									uint8* Memory = InstancedStruct->GetMutableMemory();

									EntryNode->TypeName = FString::Printf(TEXT("InstancedStruct (%s)"), *ScriptStruct->GetName());
									EntryNode->Children = GeneratePropertyNodes(Memory, ScriptStruct, OwnerObject,
																				EntryNode, CurrentDepth + 1);
								}
								else
								{
									EntryNode->TypeName = TEXT("InstancedStruct (Empty)");
								}
							}
							else
							{
								EntryNode->Children = GeneratePropertyNodes(ValueAddr, ValueStructProp->Struct, OwnerObject,
																			EntryNode, CurrentDepth + 1);
							}
						}
					}

					else if (FObjectProperty* ValueObjProp = CastField<FObjectProperty>(MapProp->ValueProp))
					{
						UObject* SubObj = ValueObjProp->GetObjectPropertyValue(ValueAddr);
						if (MVVMInspectorPanel::ShouldReflectObject(SubObj) && !IsObjectInAncestry(SubObj))
						{
							
							EntryNode->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(SubObj),
																		SubObj->GetClass(), SubObj, EntryNode,
																		CurrentDepth + 1);
							SetupChangeListener(SubObj);
						}
					}

					if (EntryNode->Children.Num() > 0 || PropertyFilterString.IsEmpty())
					{
						Node->Children.Add(EntryNode);
					}
				}
			}
		}

		// Structs 
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (IsValid(StructProp->Struct) && !IsSpecialStruct(StructProp->Struct))
			{
				// Instanced Structs
				if (StructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_InstancedStruct)
				{
					FInstancedStruct* InstancedStruct = reinterpret_cast<FInstancedStruct*>(PropValuePtr);
					if (InstancedStruct != nullptr && InstancedStruct->IsValid())
					{
						const UScriptStruct* ScriptStruct = InstancedStruct->GetScriptStruct();
						uint8* Memory = InstancedStruct->GetMutableMemory();
						Node->TypeName = FString::Printf(TEXT("InstancedStruct (%s)"), *ScriptStruct->GetName());
						Node->Children = GeneratePropertyNodes(Memory, ScriptStruct, OwnerObject, Node,
															   CurrentDepth + 1);
					}
					else
					{
						Node->TypeName = FString::Printf(TEXT("InstancedStruct (Empty)"));
					}
				}
				else
				{
					Node->Children = GeneratePropertyNodes(PropValuePtr, StructProp->Struct, OwnerObject, Node,
														   CurrentDepth + 1);
				}
			}
		}

		// Objects 
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			UObject* SubObj = ObjProp->GetObjectPropertyValue(PropValuePtr);
			if (IsValid(SubObj))
			{
				if (MVVMInspectorPanel::ShouldReflectObject(SubObj))
				{
					Node->TypeName = FString::Printf(TEXT("Object (%s)"), *SubObj->GetName());

					if (!IsObjectInAncestry(SubObj))
					{
						Node->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(SubObj), SubObj->GetClass(),
															   SubObj, Node, CurrentDepth + 1);
						SetupChangeListener(SubObj);
					}
					else
					{
						const TSharedPtr<FMVVMPropertyNode> CycleNode = MakeShared<FMVVMPropertyNode>();
						CycleNode->DisplayName = TEXT("Cycle Detected");
						CycleNode->TypeName = TEXT("Recursive Reference");
						CycleNode->ParentNode = Node;
						Node->Children.Add(CycleNode);
					}
				}
			}
			else
			{
				Node->TypeName = TEXT("Object (None)");
			}
		}

		// Filter Logic: 
		// Add this property if it matches the name/type, OR if it has children that matched. 
		bool bMatches = PropertyFilterString.IsEmpty();
		if (!bMatches)
		{
			bMatches = Node->DisplayName.Contains(PropertyFilterString);
		}

		if (bMatches || !Node->Children.IsEmpty())
		{
			Nodes.Add(Node);
		}
	}

	// Process Functions
	if (const UClass* ClassLayout = Cast<UClass>(StructLayout))
	{
		for (TFieldIterator<UFunction> FuncIt(ClassLayout, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			
			// Skip actual underlying delegate functions, handled naturally by the delegate FProperty block above.
			if (Func->HasAnyFunctionFlags(FUNC_Delegate | FUNC_MulticastDelegate))
			{
				continue;
			}
			
			// Skip generic & noisy base class methods
			UClass* FuncOwner = Func->GetOwnerClass();
			if (FuncOwner == UObject::StaticClass() || FuncOwner == UMVVMViewModelBase::StaticClass())
			{
				continue;
			}

			const TSharedPtr<FMVVMPropertyNode> FuncNode = MakeShared<FMVVMPropertyNode>();
			FuncNode->DisplayName = Func->GetName();
			FuncNode->TypeName = TEXT("Function");
			FuncNode->EffectiveOwner = OwnerObject;
			FuncNode->ParentNode = Parent;
			FuncNode->Property = nullptr;
			FuncNode->bIsFunction = true;
			FuncNode->Function = Func;
#if WITH_EDITORONLY_DATA
			FuncNode->TooltipText = Func->GetToolTipText().ToString();
#endif
			if (Func->GetStructureSize() > 0)
			{
				FuncNode->FunctionParams = MakeShared<FStructOnScope>(FuncNode->Function.Get());
				
				// Generate parameter properties passing the memory buffer
				FuncNode->Children = GeneratePropertyNodes(FuncNode->FunctionParams->GetStructMemory(), Func, OwnerObject, FuncNode, CurrentDepth + 1, MaxDepth);
			}

			bool bMatches = PropertyFilterString.IsEmpty();
			if (!bMatches) bMatches = FuncNode->DisplayName.Contains(PropertyFilterString);

			if (bMatches || !FuncNode->Children.IsEmpty())
			{
				Nodes.Add(FuncNode);
			}
		}
	}

	return Nodes;
}

TSharedRef<ITableRow> SMVVMInspectorPanel::GeneratePropertyRow(
	TSharedPtr<FMVVMPropertyNode> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMVVMPropertyRow, OwnerTable, Item)
		.InspectorPanel(this);
}

void SMVVMInspectorPanel::OnGetPropertyChildren(
	TSharedPtr<FMVVMPropertyNode> Item,
	TArray<TSharedPtr<FMVVMPropertyNode>>& OutChildren)
{
	OutChildren = Item->Children;
}

void SMVVMInspectorPanel::OnPropertySearchChanged(const FText& InFilterText)
{
	PropertyFilterString = InFilterText.ToString();
	if (CurrentSelection.IsValid())
	{
		RebuildPropertyTree(CurrentSelection.Pin());
	}
}

void SMVVMInspectorPanel::SetPropertyExpansion(TSharedPtr<FMVVMPropertyNode> Node, bool bExpand)
{
	if (!Node.IsValid())
	{
		return;
	}
	PropertyTreeView->SetItemExpansion(Node, true);

	if (bExpand)
	{
		for (const auto& Child : Node->Children)
		{
			SetPropertyExpansion(Child, true);
		}
	}
}

// ---------------------------------------------------------------------------------- 
// FMVVMPropertyNode Pointer Resolution 
// ---------------------------------------------------------------------------------- 

uint8* FMVVMPropertyNode::GetContainerPtr() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	// Category roots store the effective owner as the container. 
	if (bIsCategoryRoot)
	{
		return reinterpret_cast<uint8*>(EffectiveOwner.Get());
	}

	const TSharedPtr<FMVVMPropertyNode> Parent = ParentNode.Pin();
	if (!Parent.IsValid() || Parent->bIsCategoryRoot)
	{
		return reinterpret_cast<uint8*>(EffectiveOwner.Get());
	}

	// If the parent is a function or delegate, the memory container for our property is the parameter buffer
	if (Parent->bIsFunction || Parent->bIsDelegate)
	{
		uint8* ParamsMemory = Parent->FunctionParams.IsValid() ? Parent->FunctionParams->GetStructMemory() : nullptr;
		return ParamsMemory;
	}

	uint8* ParentValueAddress = Parent->GetRawValuePtr();
	if (ParentValueAddress == nullptr)
	{
		return nullptr;
	}

	// If parent is an object property node, ParentValueAddress points at the UObject* storage, 
	// so we need to dereference it to the actual UObject instance. 
	if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Parent->Property.Get()))
	{
		UObject* ActualObject = ObjProp->GetObjectPropertyValue(ParentValueAddress);
		return reinterpret_cast<uint8*>(ActualObject);
	}

	if (const FStructProperty* ParentStructProp = CastField<FStructProperty>(Parent->Property.Get()))
	{
		if (IsValid(ParentStructProp->Struct) && ParentStructProp->Struct->GetFName() ==
			MVVMInspectorPanel::NAME_InstancedStruct)
		{
			if (FInstancedStruct* InstancedStruct = reinterpret_cast<FInstancedStruct*>(ParentValueAddress); InstancedStruct != nullptr)
			{
				if (InstancedStruct->IsValid())
				{
					return InstancedStruct->GetMutableMemory();
				}
				return nullptr; // Payload is null/invalid
			}
		}
	}

	// Struct members use the parent's value address as the container. 
	return ParentValueAddress;
}

uint8* FMVVMPropertyNode::GetRawValuePtr() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (bIsCategoryRoot)
	{
		return nullptr;
	}

	// Array element nodes use their parent array node to resolve the element pointer. 
	if (ArrayIndex != INDEX_NONE)
	{
		const TSharedPtr<FMVVMPropertyNode> Parent = ParentNode.Pin();
		if (!Parent.IsValid())
		{
			return nullptr;
		}

		const uint8* ArrayDataPtr = Parent->GetRawValuePtr();
		if (ArrayDataPtr == nullptr)
		{
			return nullptr;
		}

		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Parent->Property.Get()))
		{
			FScriptArrayHelper Helper(ArrayProp, ArrayDataPtr);
			return Helper.IsValidIndex(ArrayIndex) ? Helper.GetRawPtr(ArrayIndex) : nullptr;
		}

		if (const FMapProperty* MapProp = CastField<FMapProperty>(Parent->Property.Get()))
		{
			FScriptMapHelper Helper(MapProp, ArrayDataPtr);
			return Helper.IsValidIndex(ArrayIndex) ? Helper.GetValuePtr(ArrayIndex) : nullptr;
		}

		return nullptr;
	}

	uint8* Container = GetContainerPtr();
	const FProperty* Prop = Property.Get();
	if (Container == nullptr || Prop == nullptr)
	{
		return nullptr;
	}
	return Prop->ContainerPtrToValuePtr<uint8>(Container);
}

// ---------------------------------------------------------------------------------- 
// Getter / Setter Helper Invocations 
// ---------------------------------------------------------------------------------- 

bool SMVVMInspectorPanel::TryCallGetterSafe(TSharedPtr<FMVVMPropertyNode> Node, uint8* OutRawValue)
{
	if (!Node.IsValid() || Node->ArrayIndex != INDEX_NONE || !Node->EffectiveOwner.IsValid())
	{
		return false;
	}
	FProperty* Prop = Node->Property.Get();
	UObject* Owner = Node->EffectiveOwner.Get();
	
#if WITH_METADATA
	if (Prop->HasMetaData(TEXT("BlueprintGetter")))
	{
		if (UFunction* Func = Owner->GetClass()->FindFunctionByName(FName(*Prop->GetMetaData(TEXT("BlueprintGetter")))))
		{
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Func->GetStructureSize());
			
			// Initialize params
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				It->InitializeValue_InContainer(Buffer.GetData());
			}
			
			Owner->ProcessEvent(Func, Buffer.GetData());
			
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					It->CopyCompleteValue(OutRawValue, It->ContainerPtrToValuePtr<uint8>(Buffer.GetData()));
					break;
				}
			}
			
			// Destroy params
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				It->DestroyValue_InContainer(Buffer.GetData());
			}
			return true;
		}
	}
#endif

	if (Prop->HasGetter())
	{
		Prop->CallGetter(Node->GetContainerPtr(), OutRawValue);
		return true;
	}
	return false;
}

bool SMVVMInspectorPanel::TryCallSetterSafe(TSharedPtr<FMVVMPropertyNode> Node, const uint8* InRawValue)
{
	if (!Node.IsValid() || Node->ArrayIndex != INDEX_NONE || !Node->EffectiveOwner.IsValid())
	{
		return false;
	}
	FProperty* Prop = Node->Property.Get();
	UObject* Owner = Node->EffectiveOwner.Get();
	
#if WITH_METADATA
	if (Prop->HasMetaData(TEXT("BlueprintSetter")))
	{
		if (UFunction* Func = Owner->GetClass()->FindFunctionByName(FName(*Prop->GetMetaData(TEXT("BlueprintSetter")))))
		{
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Func->GetStructureSize());
			
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				It->InitializeValue_InContainer(Buffer.GetData());
			}
			
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					It->CopyCompleteValue(It->ContainerPtrToValuePtr<uint8>(Buffer.GetData()), InRawValue);
					break; 
				}
			}
			
			Owner->ProcessEvent(Func, Buffer.GetData());
			
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				It->DestroyValue_InContainer(Buffer.GetData());
			}
			return true;
		}
	}
#endif

	if (Prop->HasSetter())
	{
		Prop->CallSetter(Node->GetContainerPtr(), InRawValue);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------- 
// Value Widgets 
// ----------------------------------------------------------------------------------

void SMVVMInspectorPanel::NotifyPropertyValueChanged(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!Node.IsValid() || !Node->EffectiveOwner.IsValid())
	{
		return;
	}

	// Skip firing field notifies for UI edits made strictly inside temporary function parameters.
	// We start from ParentNode because if the Node itself IS the function being invoked, we DO want to fire the notify.
	for (TSharedPtr<FMVVMPropertyNode> Walker = Node->ParentNode.Pin(); Walker.IsValid(); Walker = Walker->ParentNode.Pin())
	{
		if (Walker->bIsFunction || Walker->bIsDelegate)
		{
			return;
		}
	}

	INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Node->EffectiveOwner.Get());
	if (Notify == nullptr)
	{
		return;
	}

	FName FieldName = NAME_None;
	const UClass* DefinitionClass = nullptr;

	if (Node->bIsFunction && Node->Function.IsValid())
	{
		DefinitionClass = Node->Function->GetOwnerClass();
		FieldName = Node->Function->GetFName();
	}
	else if (Node->Property != nullptr)
	{
		const FProperty* NotifyProp = Node->Property.Get();

		// If we're editing a nested struct member (e.g. Vector.X), we need to find the 
		// top-level UObject property (e.g. the Vector itself) to broadcast the change.
		// We walk up the tree until we find a property owned by a UClass (not a UStruct).
		for (TSharedPtr<FMVVMPropertyNode> Walker = Node; Walker.IsValid() && Walker->Property.Get(); Walker = Walker->ParentNode.Pin())
		{
			// FProperty::GetOwnerClass() returns the UClass owner, or nullptr if owned by a UStruct.
			if (const UClass* PropOwner = Walker->Property->GetOwnerClass())
			{
				// We found the top-level property on the object.
				NotifyProp = Walker->Property.Get();
				break;
			}
		}
		
		if (NotifyProp != nullptr)
		{
			DefinitionClass = NotifyProp->GetOwnerClass();
			FieldName = NotifyProp->GetFName();
		}
	}

	if (DefinitionClass && !FieldName.IsNone())
	{
		const UE::FieldNotification::FFieldId FieldId =
			Notify->GetFieldNotificationDescriptor().GetField(DefinitionClass, FieldName);

		if (FieldId.IsValid())
		{
			Notify->BroadcastFieldValueChanged(FieldId);
		}
	}

	// For mocked views, directly execute the ViewModel's bindings as a reliable fallback.
	TSharedPtr<FMVVMHierarchyNode> Selection = CurrentSelection.Pin();
	if (Selection.IsValid() && Selection->MVVMView.IsValid())
	{
		UMVVMView* View = Selection->MVVMView.Get();
		if (MockedViews.Contains(TWeakObjectPtr<UMVVMView>(View)))
		{
			if (const UMVVMViewClass* ViewClass = View->GetViewClass())
			{
				for (const FMVVMView_Source& VS : View->GetSources())
				{
					if (VS.Source == Node->EffectiveOwner.Get())
					{
						const FMVVMViewClass_Source& CS = ViewClass->GetSource(VS.ClassKey);
						View->ExecuteViewModelBindings(CS.GetName());
						break;
					}
				}
			}
		}
	}
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateValueWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	// Create Invocation buttons for Delegates
	if (Node->bIsDelegate)
	{
		return SNew(SButton)
			.Text(INVTEXT("Invoke"))
			.VAlign(VAlign_Center)
			.ToolTipText(FText::FromString(Node->TooltipText))
			.OnClicked(this, &SMVVMInspectorPanel::OnInvokeClicked, Node);
	}

	// Handle Functions
	if (Node->bIsFunction && Node->Function.IsValid())
	{
		bool bIsGetter = Node->Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
#if WITH_METADATA
		if (!bIsGetter)
		{
			bIsGetter = Node->Function->HasMetaData(TEXT("FieldNotify"));
		}
#endif
		FProperty* ReturnProp = Node->Function->GetReturnProperty();

		// Check if it's a getter with NO input parameters (only the return param)
		bool bHasNoInputs = true;
		for (TFieldIterator<FProperty> It(Node->Function.Get()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				bHasNoInputs = false;
				break;
			}
		}

		if (bIsGetter && ReturnProp && bHasNoInputs && Node->EffectiveOwner.IsValid())
		{
			// Render pure functions/FieldNotifies as dynamically evaluating read-only values
			return SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(MVVMInspectorPanel::ComputedValueColor)
				.Text_Lambda([Node, ReturnProp]() -> FText
				{
					if (!Node.IsValid() || !Node->EffectiveOwner.IsValid() || !Node->Function.IsValid())
					{
						return FText::GetEmpty();
					}
					UObject* Owner = Node->EffectiveOwner.Get();
					UFunction* Func = Node->Function.Get();

					TArray<uint8> Buffer;
					Buffer.SetNumZeroed(Func->GetStructureSize());
					
					// Initialize properties in the buffer to be safe
					for (TFieldIterator<FProperty> It(Func); It; ++It)
					{
						It->InitializeValue_InContainer(Buffer.GetData());
					}

					Owner->ProcessEvent(Func, Buffer.GetData());
					
					FString ValStr;
					ReturnProp->ExportText_Direct(ValStr, ReturnProp->ContainerPtrToValuePtr<uint8>(Buffer.GetData()), nullptr, nullptr, PPF_None);
					
					// Destroy properties
					for (TFieldIterator<FProperty> It(Func); It; ++It)
					{
						It->DestroyValue_InContainer(Buffer.GetData());
					}
					
					return FText::FromString(ValStr);
				});
		}
		else
		{
			// Standard Invoke Button for state-mutating methods or methods with inputs
			return SNew(SButton)
				.Text(INVTEXT("Invoke"))
				.VAlign(VAlign_Center)
				.ToolTipText(FText::FromString(Node->TooltipText))
				.OnClicked(this, &SMVVMInspectorPanel::OnInvokeClicked, Node);
		}
	}

	FProperty* Prop = Node ? Node->Property.Get() : nullptr;
	if (Prop == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	// Shared edit-gating: widget owners, return params, and getter-chain ancestors are all read-only.
	const bool bCanEdit = CanModifyNode(Node);

	// If this row represents a TArray element, every value-widget return must be paired with a Remove
	// button so the user can shrink the array. The Wrap helper applies that decoration once, at the
	// single set of return points below, and is a no-op for non-element rows.
	auto Wrap = [this, Node](TSharedRef<SWidget> Inner) -> TSharedRef<SWidget>
	{
		return IsArrayElementNode(Node) ? WrapWithArrayElementControls(Inner, Node) : Inner;
	};

	// Bool
	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		return Wrap(CreateBoolWidget(Node, BoolProp, bCanEdit));
	}

	// Enums / Bytes
	if (Prop->IsA<FEnumProperty>() || Prop->IsA<FByteProperty>())
	{
		// Enums and Byte properties with Enums are handled together
		if (Prop->IsA<FEnumProperty>() || (CastField<FByteProperty>(Prop) && CastField<FByteProperty>(Prop)->Enum))
		{
			return Wrap(CreateEnumWidget(Node, Prop, bCanEdit));
		}
	}

	// Numerics
	if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
	{
		return Wrap(CreateNumericWidget(Node, NumProp, bCanEdit));
	}

	// Strings
	if (Prop->IsA<FStrProperty>() || Prop->IsA<FTextProperty>() || Prop->IsA<FNameProperty>())
	{
		return Wrap(CreateStringWidget(Node, bCanEdit));
	}

	// Special Structs (GameplayTags, Vectors, Colors)
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (IsValid(StructProp->Struct) && IsSpecialStruct(StructProp->Struct))
		{
			return Wrap(CreateSpecialStructWidget(Node, StructProp, bCanEdit));
		}
	}

	// Containers (Arrays / Sets)
	if (Prop->IsA<FArrayProperty>() || Prop->IsA<FSetProperty>())
	{
		return Wrap(CreateContainerWidget(Node));
	}

	// Object-like properties (Soft, Weak, Class). Note: FClassProperty extends FObjectProperty,
	// so this branch must run before the strict-FObjectProperty check below.
	if (Prop->IsA<FSoftObjectProperty>() || Prop->IsA<FWeakObjectProperty>() || Prop->IsA<FClassProperty>())
	{
		return Wrap(CreateObjectLikeWidget(Node, Prop));
	}

	// Strong Object Property (UObject* direct references). Catches plain FObjectProperty;
	// FClassProperty already returned above.
	if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
	{
		return Wrap(CreateObjectPropertyWidget(Node, ObjProp, bCanEdit));
	}

	// Fallback (unknown / unhandled property types)
	return Wrap(CreateFallbackWidget(Node));
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateBoolWidget(TSharedPtr<FMVVMPropertyNode> Node,
														  const FBoolProperty* BoolProp, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	return SNew(SCheckBox)
		.IsEnabled(bCanEdit)
		.IsChecked_Lambda([Node]()
		{
			if (!Node.IsValid())
			{
				return ECheckBoxState::Unchecked;
			}
			const FBoolProperty* Prop = CastField<FBoolProperty>(Node->Property.Get());
			if (Prop == nullptr)
			{
				return ECheckBoxState::Unchecked;
			}
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (Ptr == nullptr)
			{
				return ECheckBoxState::Unchecked;
			}
			return Prop->GetPropertyValue(Ptr) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, Node](ECheckBoxState NewState)
		{
			if (!Node.IsValid())
			{
				return;
			}
			if (const FBoolProperty* Prop = CastField<FBoolProperty>(Node->Property.Get()))
			{
				bool bNewValue = (NewState == ECheckBoxState::Checked);
				
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				Prop->SetPropertyValue(Buffer.GetData(), bNewValue);

				if (!TryCallSetterSafe(Node, Buffer.GetData()))
				{
					if (uint8* Ptr = Node->GetRawValuePtr())
					{
						Prop->SetPropertyValue(Ptr, bNewValue);
					}
				}
				NotifyPropertyValueChanged(Node);
			}
		});
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateEnumWidget(TSharedPtr<FMVVMPropertyNode> Node, const FProperty* Prop,
														  bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	UEnum* EnumDef = nullptr;
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		EnumDef = EnumProp->GetEnum();
	}
	else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		EnumDef = ByteProp->Enum;
	}

	if (!IsValid(EnumDef))
	{
		return SNullWidget::NullWidget;
	}

	// Populate cache if empty
	if (Node->EnumOptionValues.IsEmpty())
	{
		// Skip the implicit _MAX entry (typically last). 
		for (int32 i = 0; i < EnumDef->NumEnums() - 1; ++i)
		{
#if WITH_METADATA
			if (EnumDef->HasMetaData(TEXT("Hidden"), i))
			{
				continue;
			}
#endif // WITH_METADATA
			Node->EnumOptionValues.Add(MakeShared<int64>(EnumDef->GetValueByIndex(i)));
		}
	}

	// Determine current selection
	TSharedPtr<int64> CurrentItem;
	TArray<uint8> Buffer;
	Buffer.SetNumZeroed(Prop->GetSize());
	if (const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr())
	{
		int64 CurrentVal = 0;
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			CurrentVal = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
		}
		else if (const FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			CurrentVal = static_cast<int64>(*Ptr);
		}

		for (const TSharedPtr<int64>& Item : Node->EnumOptionValues)
		{
			if (Item.IsValid() && *Item == CurrentVal)
			{
				CurrentItem = Item;
				break;
			}
		}
	}

	return SNew(SComboBox<TSharedPtr<int64>>)
		.IsEnabled(bCanEdit)
		.OptionsSource(&Node->EnumOptionValues)
		.InitiallySelectedItem(CurrentItem)
		.OnGenerateWidget_Lambda([EnumDef](TSharedPtr<int64> Item)
		{
			return SNew(STextBlock)
				.AutoWrapText(true)
				.Text(Item.IsValid() ? EnumDef->GetDisplayNameTextByValue(*Item) : FText::GetEmpty());
		})
		.OnSelectionChanged_Lambda([this, Node](TSharedPtr<int64> NewValue, ESelectInfo::Type)
		{
			if (!NewValue.IsValid() || !Node.IsValid())
			{
				return;
			}
			FProperty* CurrentProp = Node->Property.Get();
			if (CurrentProp == nullptr)
			{
				return;
			}
			const int64 Val = *NewValue;
			TArray<uint8> LocalBuffer;
			LocalBuffer.SetNumZeroed(CurrentProp->GetSize());

			if (FEnumProperty* EP = CastField<FEnumProperty>(CurrentProp))
			{
				EP->GetUnderlyingProperty()->SetIntPropertyValue(LocalBuffer.GetData(), Val);
			}
			else if (FByteProperty* BP = CastField<FByteProperty>(CurrentProp))
			{
				*LocalBuffer.GetData() = static_cast<uint8>(Val);
			}

			if (!TryCallSetterSafe(Node, LocalBuffer.GetData()))
			{
				if (uint8* Ptr = Node->GetRawValuePtr())
				{
					if (FEnumProperty* EP = CastField<FEnumProperty>(CurrentProp))
					{
						EP->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Val);
					}
					else if (FByteProperty* BP = CastField<FByteProperty>(CurrentProp))
					{
						*Ptr = static_cast<uint8>(Val);
					}
				}
			}
			NotifyPropertyValueChanged(Node);
		})
		.Content()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([Node, EnumDef]()
			{
				if (!Node.IsValid())
				{
					return FText::FromString("Invalid Node");
				}
				FProperty* CurrentProp = Node->Property.Get();
				if (CurrentProp == nullptr)
				{
					return FText::FromString(TEXT("None"));
				}
				TArray<uint8> LocalBuffer;
				LocalBuffer.SetNumZeroed(CurrentProp->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, LocalBuffer.GetData()) ? LocalBuffer.GetData() : Node->GetRawValuePtr();
				if (Ptr == nullptr)
				{
					return FText::FromString(TEXT("None"));
				}
				int64 Val = 0;
				if (FEnumProperty* EP = CastField<FEnumProperty>(CurrentProp))
				{
					Val = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
				}
				else if (FByteProperty* BP = CastField<FByteProperty>(CurrentProp))
				{
					Val = static_cast<int64>(*Ptr);
				}
				return EnumDef->GetDisplayNameTextByValue(Val);
			})
		];
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateNumericWidget(TSharedPtr<FMVVMPropertyNode> Node,
															 const FNumericProperty* NumProp, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (NumProp->IsFloatingPoint())
	{
		return SNew(SSpinBox<double>)
			.IsEnabled(bCanEdit)
			.Value_Lambda([Node]()
			{
				if (!Node.IsValid())
				{
					return 0.0;
				}
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (Prop == nullptr)
				{
					return 0.0;
				}
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
				if (Ptr == nullptr)
				{
					return 0.0;
				}
				return static_cast<double>(Prop->GetFloatingPointPropertyValue(Ptr));
			})
			.OnValueChanged_Lambda([this, Node](const double NewVal)
			{
				if (!Node.IsValid())
				{
					return;
				}
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (Prop == nullptr)
				{
					return;
				}
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				Prop->SetFloatingPointPropertyValue(Buffer.GetData(), NewVal);

				if (!TryCallSetterSafe(Node, Buffer.GetData()))
				{
					if (uint8* Ptr = Node->GetRawValuePtr())
					{
						Prop->SetFloatingPointPropertyValue(Ptr, NewVal);
					}
				}
				NotifyPropertyValueChanged(Node);
			});
	}

	if (NumProp->IsInteger())
	{
		return SNew(SSpinBox<int64>)
			.IsEnabled(bCanEdit)
			.Value_Lambda([Node]() -> int64
			{
				if (!Node.IsValid())
				{
					return 0;
				}
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (Prop == nullptr)
				{
					return 0;
				}
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
				if (Ptr == nullptr)
				{
					return 0;
				}
				return Prop->GetSignedIntPropertyValue(Ptr);
			})
			.OnValueChanged_Lambda([this, Node](int64 NewVal)
			{
				if (!Node.IsValid())
				{
					return;
				}
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (Prop == nullptr)
				{
					return;
				}
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				Prop->SetIntPropertyValue(Buffer.GetData(), NewVal);

				if (!TryCallSetterSafe(Node, Buffer.GetData()))
				{
					if (uint8* Ptr = Node->GetRawValuePtr())
					{
						Prop->SetIntPropertyValue(Ptr, NewVal);
					}
				}
				NotifyPropertyValueChanged(Node);
			});
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateStringWidget(TSharedPtr<FMVVMPropertyNode> Node, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	return SNew(SEditableTextBox)
		.IsEnabled(bCanEdit)
		.Text_Lambda([Node]()
		{
			if (!Node.IsValid() || !Node->Property.Get())
			{
				return FText::GetEmpty();
			}
			FProperty* Prop = Node->Property.Get();

			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (Ptr == nullptr)
			{
				return FText::GetEmpty();
			}
			if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				return TextProp->GetPropertyValue(Ptr);
			}

			FString ValStr;
			Prop->ExportText_Direct(ValStr, Ptr, nullptr, nullptr, PPF_None);
			return FText::FromString(ValStr);
		})
		.OnTextCommitted_Lambda([this, Node](const FText& NewText, ETextCommit::Type)
		{
			if (!Node.IsValid())
			{
				return;
			}
			FProperty* Prop = Node->Property.Get();
			if (Prop == nullptr)
			{
				return;
			}
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			Prop->InitializeValue(Buffer.GetData());
			Prop->ImportText_Direct(*NewText.ToString(), Buffer.GetData(), Node->EffectiveOwner.Get(), PPF_None);

			if (!TryCallSetterSafe(Node, Buffer.GetData()))
			{
				if (uint8* Ptr = Node->GetRawValuePtr())
				{
					Prop->ImportText_Direct(*NewText.ToString(), Ptr, Node->EffectiveOwner.Get(), PPF_None);
				}
			}
			Prop->DestroyValue(Buffer.GetData());
			NotifyPropertyValueChanged(Node);
		});
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateSpecialStructWidget(TSharedPtr<FMVVMPropertyNode> Node,
																   const FStructProperty* StructProp, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	TSharedRef<SHorizontalBox> StructBox = SNew(SHorizontalBox);

	const FName StructName = IsValid(StructProp->Struct) ? StructProp->Struct->GetFName() : NAME_None;
	if (StructName == MVVMInspectorPanel::NAME_Color || StructName == MVVMInspectorPanel::NAME_LinearColor)
	{
		StructBox->AddSlot()
		.AutoWidth()
		.Padding(0, 0, 6, 0)
		.VAlign(VAlign_Center)
		[
			SNew(SColorBlock)
			.Color_Lambda([Node, StructProp]() -> FLinearColor
			{
				if (!Node.IsValid())
				{
					return MVVMInspectorPanel::TextTransparent;
				}
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(StructProp->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
				if (Ptr == nullptr)
				{
					return MVVMInspectorPanel::TextTransparent;
				}
				if (StructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_Color)
				{
					return FLinearColor(*reinterpret_cast<const FColor*>(Ptr));
				}
				else if (StructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_LinearColor)
				{
					return *reinterpret_cast<const FLinearColor*>(Ptr);
				}
				return MVVMInspectorPanel::TextTransparent;
			})
			.Size(FVector2D(16.0f, 16.0f))
			.ShowBackgroundForAlpha(true)
		];
	}

	StructBox->AddSlot()
	.FillWidth(1.0f)
	[
		SNew(SEditableTextBox)
		.IsEnabled(bCanEdit)
		.Text_Lambda([this, Node]()
		{
			if (!Node.IsValid())
			{
				return FText::GetEmpty();
			}
			const FStructProperty* Prop = CastField<FStructProperty>(Node->Property.Get());
			if (Prop == nullptr)
			{
				return FText::FromString(TEXT("Error"));
			}
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (Ptr == nullptr)
			{
				return FText::FromString(TEXT("Error"));
			}
			return FText::FromString(GetSpecialStructValue(Prop->Struct, Ptr));
		})
		.OnTextCommitted_Lambda([this, Node](const FText& NewText, ETextCommit::Type)
		{
			if (!Node.IsValid())
			{
				return;
			}
			const FStructProperty* Prop = CastField<FStructProperty>(Node->Property.Get());
			if (Prop == nullptr)
			{
				return;
			}
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			
			// Grab current value to only override specified string representation part
			const uint8* GetterPtr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (GetterPtr != nullptr && GetterPtr != Buffer.GetData())
			{
				Prop->CopyCompleteValue(Buffer.GetData(), GetterPtr);
			}

			SetSpecialStructValue(Node, Buffer.GetData(), NewText.ToString());

			if (!TryCallSetterSafe(Node, Buffer.GetData()))
			{
				if (uint8* RealPtr = Node->GetRawValuePtr())
				{
					SetSpecialStructValue(Node, RealPtr, NewText.ToString());
				}
			}
			NotifyPropertyValueChanged(Node);
		})
		.ForegroundColor(MVVMInspectorPanel::ComputedValueColor)
	];

	return StructBox;
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateContainerWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	const FProperty* Prop = Node->Property.Get();

	// Arrays
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		// Binary arrays display as opaque blobs - no per-element edit or structural mutation.
		if (ArrayProp->Inner->IsA<FByteProperty>() || ArrayProp->Inner->IsA<FInt8Property>())
		{
			return SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("Binary Data")))
				.ColorAndOpacity(MVVMInspectorPanel::TextMuted);
		}

		if (!Node->GetRawValuePtr())
		{
			// No resolvable storage (likely a stale node). Can't add into it.
			return SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("Array (Null)")))
				.ColorAndOpacity(MVVMInspectorPanel::TextMuted);
		}

		const bool bCanModify = CanModifyNode(Node);

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				// Dynamic label so the count updates without a tree rebuild being strictly required.
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(MVVMInspectorPanel::TextMuted)
				.Text_Lambda([Node]() -> FText
				{
					if (!Node.IsValid())
					{
						return FText::GetEmpty();
					}
					const FArrayProperty* AP = CastField<FArrayProperty>(Node->Property.Get());
					const uint8* P = Node->GetRawValuePtr();
					if (AP == nullptr || P == nullptr)
					{
						return FText::FromString(TEXT("Array"));
					}
					const FScriptArrayHelper H(AP, P);
					const int32 N = H.Num();
					return FText::FromString(FString::Printf(TEXT("Array (%d %s)"),
						N, N == 1 ? TEXT("item") : TEXT("items")));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.IsEnabled(bCanModify)
				.Text(INVTEXT("+"))
				.ToolTipText(INVTEXT("Append a new default-initialized element to this array."))
				.OnClicked_Lambda([this, Node]() -> FReply
				{
					AddArrayElement(Node);
					return FReply::Handled();
				})
			];
	}

	// Sets
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(MVVMInspectorPanel::TextMuted)
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid())
				{
					return FText::GetEmpty();
				}
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FSetProperty* SProp = CastField<FSetProperty>(Node->Property.Get()))
					{
						FScriptSetHelper Helper(SProp, Ptr);
						return FText::FromString(FString::Printf(TEXT("Set (%d items, expand to view)"), Helper.Num()));
					}
				}
				return FText::FromString(TEXT("Set (Invalid)"));
			});
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateObjectLikeWidget(TSharedPtr<FMVVMPropertyNode> Node,
																const FProperty* Prop)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	// Soft Object References
	if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(MVVMInspectorPanel::SoftObjectRefColor)
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid())
				{
					return FText::GetEmpty();
				}
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FSoftObjectProperty* SProp = CastField<FSoftObjectProperty>(Node->Property.Get()))
					{
						const FSoftObjectPtr SoftPtr = SProp->GetPropertyValue(Ptr);
						const FString Path = SoftPtr.ToString();
						if (Path.IsEmpty())
						{
							return FText::FromString(TEXT("None"));
						}
						FString AssetName = FPaths::GetBaseFilename(Path);
						return FText::FromString(AssetName);
					}
				}
				return FText::FromString(TEXT("None"));
			})
			.ToolTipText_Lambda([Node]() -> FText
			{
				if (!Node.IsValid())
				{
					return FText::GetEmpty();
				}
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FSoftObjectProperty* SProp = CastField<FSoftObjectProperty>(Node->Property.Get()))
					{
						return FText::FromString(SProp->GetPropertyValue(Ptr).ToString());
					}
				}
				return FText::GetEmpty();
			});
	}

	// Weak Object Ptr
	if (const FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(Prop))
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(MVVMInspectorPanel::WeakObjectRefColor)
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid())
				{
					return FText::GetEmpty();
				}
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FWeakObjectProperty* WProp = CastField<FWeakObjectProperty>(Node->Property.Get()))
					{
						FWeakObjectPtr WeakPtr = WProp->GetPropertyValue(Ptr);
						if (WeakPtr.IsValid())
						{
							return FText::FromString(WeakPtr.Get()->GetName());
						}
						else if (WeakPtr.IsStale())
						{
							return FText::FromString(TEXT("None (Stale)"));
						}
					}
				}
				return FText::FromString(TEXT("None"));
			});
	}

	// TSubclassOf
	if (const FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(MVVMInspectorPanel::SubclassOfColor)
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid())
				{
					return FText::GetEmpty();
				}
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FClassProperty* CProp = CastField<
						FClassProperty>(Node->Property.Get()))
					{
						UObject* ClassObj = CProp->GetObjectPropertyValue(Ptr);
						if (IsValid(ClassObj))
						{
							return FText::FromString(
								FString::Printf(TEXT("Class: %s"), *ClassObj->GetName()));
						}
					}
				}
				return FText::FromString(TEXT("None"));
			});
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateFallbackWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	return SNew(STextBlock)
		.AutoWrapText(true)
		.Text_Lambda([Node]()
		{
			if (!Node.IsValid() || !Node->EffectiveOwner.IsValid())
			{
				return FText::GetEmpty();
			}
			if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Node->Property.Get()))
			{
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					const UObject* Obj = ObjectProperty->GetObjectPropertyValue(Ptr);
					
					if (Obj != nullptr && !GUObjectArray.IsValid(Obj))
					{
						return FText::FromString(TEXT("Object (Invalid Memory)"));
					}
					
					if (!IsValid(Obj))
					{
						return FText::FromString(TEXT("Object (None)"));
					}
					return FText::FromString(FString::Printf(TEXT("Object (%s)"), *GetNameSafe(Obj)));
				}
			}
			return FText::FromString(Node->TypeName);
		})
		.ColorAndOpacity(MVVMInspectorPanel::TextMuted);
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateObjectPropertyWidget(TSharedPtr<FMVVMPropertyNode> Node,
																	const FObjectProperty* ObjProp, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!Node.IsValid() || ObjProp == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	// Read the current slot once to decide whether to render "Clear" (has object) or "+ Add" (null).
	// The tree rebuilds on FieldNotify after each mutation, so construction-time sampling is correct.
	const uint8* CurrentPtr = Node->GetRawValuePtr();
	UObject* CurrentObj = (CurrentPtr != nullptr) ? ObjProp->GetObjectPropertyValue(CurrentPtr) : nullptr;
	const bool bHasObject = IsValid(CurrentObj);

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	// --- Label slot (reacts to live data regardless of rebuild cadence) ---
	Box->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.ColorAndOpacity_Lambda([Node]() -> FSlateColor
		{
			if (!Node.IsValid())
			{
				return FSlateColor(MVVMInspectorPanel::TextMuted);
			}
			const FObjectProperty* P = CastField<FObjectProperty>(Node->Property.Get());
			const uint8* Ptr = Node->GetRawValuePtr();
			if (P == nullptr || Ptr == nullptr)
			{
				return FSlateColor(MVVMInspectorPanel::TextMuted);
			}
			UObject* Obj = P->GetObjectPropertyValue(Ptr);
			return IsValid(Obj) ? FSlateColor(MVVMInspectorPanel::ObjectValidColor) : FSlateColor(MVVMInspectorPanel::TextMuted);
		})
		.Text_Lambda([Node]() -> FText
		{
			if (!Node.IsValid())
			{
				return FText::GetEmpty();
			}
			const FObjectProperty* P = CastField<FObjectProperty>(Node->Property.Get());
			const uint8* Ptr = Node->GetRawValuePtr();
			if (P == nullptr || Ptr == nullptr)
			{
				return FText::FromString(TEXT("(None)"));
			}
			UObject* Obj = P->GetObjectPropertyValue(Ptr);
			if (!IsValid(Obj))
			{
				return FText::FromString(TEXT("(None)"));
			}
			return FText::FromString(FString::Printf(TEXT("%s (%s)"),
				*Obj->GetName(), *Obj->GetClass()->GetName()));
		})
	];

	// --- Action slot ---
	if (bHasObject)
	{
		// Populated slot: single "Clear" button that nullifies the reference.
		Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 0, 0, 0)
		[
			SNew(SButton)
			.IsEnabled(bCanEdit)
			.Text(INVTEXT("Clear"))
			.ToolTipText(INVTEXT("Set this reference to null. The pointed-to object may be garbage-collected if nothing else holds it."))
			.OnClicked_Lambda([this, Node]() -> FReply
			{
				NullifyObjectProperty(Node);
				return FReply::Handled();
			})
		];
	}
	else
	{
		// Null slot: only offer an add-flow for ViewModel-derived properties.
		// Non-ViewModel object references are displayed but not instantiable from the inspector.
		if (IsValid(ObjProp->PropertyClass) && ObjProp->PropertyClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
		{
			TArray<UClass*> Candidates;
			GatherConcreteSubclasses(ObjProp->PropertyClass, Candidates);

			if (Candidates.Num() == 0)
			{
				// Nothing can be instantiated for this property type (e.g., purely abstract interface-like base).
				Box->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.IsEnabled(false)
					.Text(INVTEXT("+ Add"))
					.ToolTipText(FText::FromString(FString::Printf(
						TEXT("No concrete, non-deprecated subclass of %s is available."),
						IsValid(ObjProp->PropertyClass) ? *ObjProp->PropertyClass->GetName() : TEXT("(unknown)"))))
				];
			}
			else if (Candidates.Num() == 1)
			{
				// Exactly one candidate: shortcut past the picker and instantiate directly.
				UClass* OnlyClass = Candidates[0];
				Box->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.IsEnabled(bCanEdit)
					.Text(FText::FromString(FString::Printf(TEXT("+ Add %s"), *OnlyClass->GetName())))
					.ToolTipText(FText::FromString(FString::Printf(
						TEXT("Instantiate a new %s transient object and assign it here."),
						*OnlyClass->GetName())))
					.OnClicked_Lambda([this, Node, OnlyClass]() -> FReply
					{
						InstantiateObjectProperty(Node, OnlyClass);
						return FReply::Handled();
					})
				];
			}
			else
			{
				// Multiple candidates: combo-button-driven picker. Menu content is built lazily each
				// time the menu opens, so class sets discovered at runtime stay current.
				TWeakPtr<FMVVMPropertyNode> WeakNode = Node;
				Box->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SComboButton)
					.IsEnabled(bCanEdit)
					.ContentPadding(FMargin(4, 2))
					.ToolTipText(INVTEXT("Pick a class to instantiate."))
					.ButtonContent()
					[
						SNew(STextBlock).Text(INVTEXT("+ Add..."))
					]
					.OnGetMenuContent_Lambda([this, WeakNode, ObjProp]() -> TSharedRef<SWidget>
					{
						TSharedPtr<FMVVMPropertyNode> PinnedNode = WeakNode.Pin();
						if (!PinnedNode.IsValid() || ObjProp == nullptr)
						{
							return SNullWidget::NullWidget;
						}

						TArray<UClass*> MenuCandidates;
						GatherConcreteSubclasses(ObjProp->PropertyClass, MenuCandidates);

						FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
						MenuBuilder.BeginSection(NAME_None, INVTEXT("Select class"));

						for (UClass* Candidate : MenuCandidates)
						{
							if (!IsValid(Candidate))
							{
								continue;
							}
							UClass* CapturedClass = Candidate;
							TSharedPtr<FMVVMPropertyNode> CapturedNode = PinnedNode;

							MenuBuilder.AddMenuEntry(
								FText::FromString(Candidate->GetName()),
								FText::FromString(FString::Printf(TEXT("Instantiate a new %s."), *Candidate->GetName())),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([this, CapturedNode, CapturedClass]()
								{
									InstantiateObjectProperty(CapturedNode, CapturedClass);
								}))
							);
						}

						MenuBuilder.EndSection();
						return MenuBuilder.MakeWidget();
					})
				];
			}
		} // end ViewModel add-flow
	}

	return Box;
}

TSharedRef<SWidget> SMVVMInspectorPanel::WrapWithArrayElementControls(TSharedRef<SWidget> InnerWidget,
																	 TSharedPtr<FMVVMPropertyNode> ElementNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	const bool bCanModify = CanModifyNode(ElementNode);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			InnerWidget
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4, 0, 0, 0)
		[
			SNew(SButton)
			.IsEnabled(bCanModify)
			.Text(INVTEXT("-"))
			.ToolTipText_Lambda([ElementNode]() -> FText
			{
				if (!ElementNode.IsValid())
				{
					return INVTEXT("Remove element.");
				}
				return FText::FromString(FString::Printf(
					TEXT("Remove element at index %d. Subsequent elements shift down by one."),
					ElementNode->ArrayIndex));
			})
			.OnClicked_Lambda([this, ElementNode]() -> FReply
			{
				RemoveArrayElement(ElementNode);
				return FReply::Handled();
			})
		];
}

#endif // WITH_PLUGIN_MODELVIEWVIEWMODEL