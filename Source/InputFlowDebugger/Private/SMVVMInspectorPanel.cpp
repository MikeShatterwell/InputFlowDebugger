// Copyright Mike Desrosiers, All Rights Reserved. 

#include "SMVVMInspectorPanel.h"


#if WITH_PLUGIN_MODELVIEWVIEWMODEL

// Internal 
#include "InputFlowHelpers.h"

// Engine
#include <Engine/Engine.h>
#include <Engine/GameViewportClient.h>

// UMG 
#include <Blueprint/UserWidget.h>
#include <Components/Widget.h>

// ModelViewViewModel 
#include <MVVMViewModelBase.h>
#include <View/MVVMView.h>

#if WITH_EDITOR
// UMGWidgetPreview
#include <IWidgetPreviewToolkit.h>
#include <WidgetPreview.h>

// ModelViewViewModel
#include <View/MVVMViewClass.h>
#endif

// CoreUObject 
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
#include <Framework/Application/SlateApplication.h>
#include <Styling/AppStyle.h>
#include <Widgets/Images/SImage.h>
#include <Widgets/Input/SButton.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SComboBox.h>
#include <Widgets/Input/SEditableTextBox.h>
#include <Widgets/Input/SSpinBox.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SSplitter.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/Views/SExpanderArrow.h>
#include <Widgets/Input/SSearchBox.h>
#include <Widgets/Colors/SColorBlock.h>
#include <Widgets/SViewport.h>

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

	// Small helper to identify UWidget objects to ignore them in the property list 
	static bool IsWidgetObject(const UObject* Obj)
	{
		if (!IsValid(Obj)) return false;

		return Obj->IsA(UWidget::StaticClass());
	}

	// Helper to determine if we should inspect an object's internal properties
	// Currently only supports UMVVMViewModelBase-derived objects, but we might want to expand this in the future
	// when node expansion and property walking is optimized enough to handle more complex objects (Actors, Components, etc).
	static bool ShouldReflectObject(const UObject* Obj)
	{
		if (!IsValid(Obj)) return false;
		
		return Obj->IsA(UMVVMViewModelBase::StaticClass());
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
		.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.2f))
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
					.ColorAndOpacity(FLinearColor::White)
					.HighlightText(HighlightText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(InItem->ViewModelSummary))
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
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

	SetBorderBackgroundColor(FLinearColor::Transparent);

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

	const FLinearColor HeaderColor = InItem->bIsCategoryRoot ? FLinearColor(1.0f, 0.8f, 0.2f) : FLinearColor::White;
	const FLinearColor HeaderDim = FLinearColor(0.6f, 0.6f, 0.6f);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(2)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(/*bIsOverlay*/ true))
		.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.15f))
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

#if WITH_EDITOR
	WeakPreviewToolkit = InArgs._PreviewToolkit;
	if (WeakPreviewToolkit.IsValid())
	{
		if (UWidgetPreview* Preview = WeakPreviewToolkit.Pin()->GetPreview())
		{
			PreviewWidgetChangedHandle = Preview->OnWidgetChanged().AddSP(this, &SMVVMInspectorPanel::OnPreviewWidgetChanged);
		}
	}
#endif

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0)
		.BorderImage(InputFlowHelpers::GetBackgroundBrush(/*bIsOverlay*/ true))
		.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.2f))
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

#if WITH_EDITOR
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

#if WITH_EDITOR
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
#if WITH_EDITOR
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
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
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
			if (World != TargetWorld) continue;
		}
		else
		{
			if (!World->IsGameWorld()) continue;
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

#if WITH_EDITOR
		// In the designer preview, inject mock ViewModels into any source slots that failed to populate
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
					if (IsValid(MutableSources[i].Source)) continue;

					const FMVVMViewClass_Source& ClassSource = ViewClass->GetSource(MutableSources[i].ClassKey);
					const UClass* VMClass = ClassSource.GetSourceClass();
					if (!IsValid(VMClass) || !VMClass->IsChildOf<UMVVMViewModelBase>()) continue;

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
					uint64* ValidSourcesPtr = (ValidSourcesProp != nullptr) ? ValidSourcesProp->ContainerPtrToValuePtr<uint64>(FoundView) : nullptr;

					// Inject mocks into the now-empty resolver source slots
					for (int32 i = 0; i < ConstSources.Num(); ++i)
					{
						if (MutableSources[i].Source != nullptr) continue;

						const FMVVMViewClass_Source& ClassSource = ViewClass->GetSource(MutableSources[i].ClassKey);
						UClass* VMClass = ClassSource.GetSourceClass();
						if (!IsValid(VMClass) || !VMClass->IsChildOf<UMVVMViewModelBase>()) continue;

						UMVVMViewModelBase* MockVM = NewObject<UMVVMViewModelBase>(Widget, VMClass);
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
						if (VS.Source != nullptr)
						{
							const FMVVMViewClass_Source& CS = ViewClass->GetSource(VS.ClassKey);
							FoundView->ExecuteViewModelBindings(CS.GetName());
						}
					}
				}
			}

			MockedViews.Add(FoundView);
		}
#endif

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
		if (UUserWidget* FoundUserWidget = Cast<UUserWidget>(OwnerUWidget); IsValid(FoundUserWidget))
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
	if (!Node.IsValid()) return;
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
	if (!Node.IsValid()) return FReply::Handled();

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
		if (DelegateMemory)
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

void SMVVMInspectorPanel::RebuildPropertyTree(TSharedPtr<FMVVMHierarchyNode> SelectedNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	ResetListenedObjects();
	PropertyRootNodes.Reset();

	if (!SelectedNode.IsValid())
	{
		if (PropertyTreeView.IsValid()) PropertyTreeView->RequestTreeRefresh();
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
				if (!IsValid(VM) || MVVMInspectorPanel::IsWidgetObject(VM)) continue;

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

	// Restore Expansion State
	if (PropertyTreeView.IsValid())
	{
		PropertyTreeView->RequestTreeRefresh();

		const bool bIsFiltering = !PropertyFilterString.IsEmpty();
		for (const TSharedPtr<FMVVMPropertyNode>& Node : PropertyRootNodes)
		{
			SetPropertyExpansion(Node, bIsFiltering);
		}
	}
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
	if (!IsValid(Struct) || !ValuePtr)
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
	if (!Node.IsValid() || !TargetPtr) return;

	const FStructProperty* StructProp = CastField<FStructProperty>(Node->Property.Get());

	if (!StructProp || !StructProp->Struct) return;

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

	if (!BaseAddress || !StructLayout || !IsValid(OwnerObject) || !Parent.IsValid())
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
		if (!Prop)
		{
			continue;
		}

		// Skip widget-typed properties/arrays, and also skip generic UObject* values that currently point to widgets. 
		// I'm only interested in ViewModel data, and in practice a ViewModel pointing to a widget is unusual. 
		bool bSkip = false;
		if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UWidget::StaticClass()))
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
				if (InnerObjProp->PropertyClass && InnerObjProp->PropertyClass->IsChildOf(UWidget::StaticClass()))
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
							if (InstancedStruct && InstancedStruct->IsValid())
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
				if (Helper.IsValidIndex(i) && MapProp->ValueProp)
				{
					FProperty* KeyProp = Helper.GetKeyProperty();
					if (!KeyProp)
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
								if (InstancedStruct && InstancedStruct->IsValid())
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
					if (InstancedStruct && InstancedStruct->IsValid())
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
			if (Func->HasAnyFunctionFlags(FUNC_Delegate | FUNC_MulticastDelegate)) continue;
			
			// Skip generic & noisy base class methods
			UClass* FuncOwner = Func->GetOwnerClass();
			if (FuncOwner == UObject::StaticClass() || FuncOwner == UMVVMViewModelBase::StaticClass()) continue;

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
	if (!Node.IsValid()) return;
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
	if (!ParentValueAddress)
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
			if (FInstancedStruct* InstancedStruct = reinterpret_cast<FInstancedStruct*>(ParentValueAddress))
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
		if (!ArrayDataPtr)
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
	if (!Container || !Prop)
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
	if (!Node.IsValid() || Node->ArrayIndex != INDEX_NONE || !Node->EffectiveOwner.IsValid()) return false;
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
	if (!Node.IsValid() || Node->ArrayIndex != INDEX_NONE || !Node->EffectiveOwner.IsValid()) return false;
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
		if (Walker->bIsFunction || Walker->bIsDelegate) return;
	}

	INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Node->EffectiveOwner.Get());
	if (!Notify)
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
		
		if (NotifyProp)
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

#if WITH_EDITOR
	// In preview mode, directly execute the ViewModel's bindings as a reliable
	// fallback. The FieldNotify broadcast only triggers bindings whose registered
	// FieldId name exactly matches the edited property name. Mismatches are common
	// with getter-based fields, conversion functions, and nested property paths.
	if (WeakPreviewToolkit.IsValid())
	{
		TSharedPtr<FMVVMHierarchyNode> Selection = CurrentSelection.Pin();
		if (Selection.IsValid() && Selection->MVVMView.IsValid())
		{
			UMVVMView* View = Selection->MVVMView.Get();
			if (const UMVVMViewClass* ViewClass = View->GetViewClass(); IsValid(ViewClass))
			{
				for (const FMVVMView_Source& VS : View->GetSources())
				{
					if (VS.Source == Node->EffectiveOwner.Get())
					{
						const FMVVMViewClass_Source& Source = ViewClass->GetSource(VS.ClassKey);
						View->ExecuteViewModelBindings(Source.GetName());
						break;
					}
				}
			}
		}
	}
#endif
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateValueWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	if (!Node.IsValid())
	{
		return SNullWidget::NullWidget;
	}

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
				.ColorAndOpacity(FLinearColor(0.4f, 0.8f, 1.0f))
				.Text_Lambda([Node, ReturnProp]() -> FText
				{
					if (!Node.IsValid() || !Node->EffectiveOwner.IsValid() || !Node->Function.IsValid()) return FText::GetEmpty();
					
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
	if (!Prop)
	{
		return SNullWidget::NullWidget;
	}

	// Editing is disabled for widget instances (generally unsafe to mutate at runtime here). 
	const bool bIsWidgetOwner = Node->EffectiveOwner.IsValid() && Node->EffectiveOwner->IsA(UWidget::StaticClass());
	bool bCanEdit = !bIsWidgetOwner;

	// Disallow editing Return values (Function output parameters)
	if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
	{
		bCanEdit = false;
	}

	// Walk the parent chain to see if we're inside a function parameter struct, in which case we disable editing (since it's a temporary buffer, not a live property).
	for (TSharedPtr<FMVVMPropertyNode> Walker = Node->ParentNode.Pin(); Walker.IsValid(); Walker = Walker->ParentNode.Pin())
	{
		if (Walker->bIsFunction || Walker->bIsDelegate)
		{
			// Check if it's a pure node (standard getter)
			bool bIsGetter = Walker->Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
		  
#if WITH_METADATA
			// If it isn't pure, explicitly check for metadata specifiers
			if (!bIsGetter)
			{
				bIsGetter = Walker->Function->HasMetaData(TEXT("Getter")) ||
							Walker->Function->HasMetaData(TEXT("BlueprintGetter"));
			}
#endif
		  
			// Lock the property if the owning function is a getter
			if (bIsGetter)
			{
				bCanEdit = false;
			}
			break;
		}
	}

	// Bool
	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		return CreateBoolWidget(Node, BoolProp, bCanEdit);
	}

	// Enums / Bytes
	if (Prop->IsA<FEnumProperty>() || Prop->IsA<FByteProperty>())
	{
		// Enums and Byte properties with Enums are handled together
		if (Prop->IsA<FEnumProperty>() || (CastField<FByteProperty>(Prop) && CastField<FByteProperty>(Prop)->Enum))
		{
			return CreateEnumWidget(Node, Prop, bCanEdit);
		}
	}

	// Numerics
	if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
	{
		return CreateNumericWidget(Node, NumProp, bCanEdit);
	}

	// Strings
	if (Prop->IsA<FStrProperty>() || Prop->IsA<FTextProperty>() || Prop->IsA<FNameProperty>())
	{
		return CreateStringWidget(Node, bCanEdit);
	}

	// Special Structs (GameplayTags, Vectors, Colors)
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct && IsSpecialStruct(StructProp->Struct))
		{
			return CreateSpecialStructWidget(Node, StructProp, bCanEdit);
		}
	}

	// Containers (Arrays / Sets)
	if (Prop->IsA<FArrayProperty>() || Prop->IsA<FSetProperty>())
	{
		return CreateContainerWidget(Node);
	}

	// Object-like properties (Soft, Weak, Class)
	if (Prop->IsA<FSoftObjectProperty>() || Prop->IsA<FWeakObjectProperty>() || Prop->IsA<FClassProperty>())
	{
		return CreateObjectLikeWidget(Node, Prop);
	}

	// Fallback / Standard Objects
	return CreateFallbackWidget(Node);
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateBoolWidget(TSharedPtr<FMVVMPropertyNode> Node,
														  const FBoolProperty* BoolProp, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	return SNew(SCheckBox)
		.IsEnabled(bCanEdit)
		.IsChecked_Lambda([Node]()
		{
			if (!Node.IsValid()) return ECheckBoxState::Unchecked;

			const FBoolProperty* Prop = CastField<FBoolProperty>(Node->Property.Get());
			if (!Prop) return ECheckBoxState::Unchecked;

			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (!Ptr) return ECheckBoxState::Unchecked;

			return Prop->GetPropertyValue(Ptr) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, Node](const ECheckBoxState NewState)
		{
			if (!Node.IsValid()) return;

			if (const FBoolProperty* Prop = CastField<FBoolProperty>(Node->Property.Get()))
			{
				const bool bNewValue = (NewState == ECheckBoxState::Checked);
				
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

TSharedRef<SWidget> SMVVMInspectorPanel::CreateEnumWidget(TSharedPtr<FMVVMPropertyNode> Node, const FProperty* Prop, const bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!Node.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	UEnum* EnumDef = nullptr;
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		EnumDef = EnumProp->GetEnum();
	}
	else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		EnumDef = ByteProp->Enum;
	}

	if (!EnumDef)
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
	TSharedPtr<int64> CurrentItem = nullptr;
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
			if (!NewValue.IsValid() || !Node.IsValid()) return;
			FProperty* CurrentProp = Node->Property.Get();
			if (!CurrentProp) return;

			const int64 Val = *NewValue;
			TArray<uint8> LocalBuffer;
			LocalBuffer.SetNumZeroed(CurrentProp->GetSize());

			if (const FEnumProperty* EP = CastField<FEnumProperty>(CurrentProp))
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
					if (const FEnumProperty* EP = CastField<FEnumProperty>(CurrentProp))
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
				if (!Node.IsValid()) return FText::FromString("Invalid Node");

				FProperty* CurrentProp = Node->Property.Get();
				if (!CurrentProp) return FText::FromString(TEXT("None"));

				TArray<uint8> LocalBuffer;
				LocalBuffer.SetNumZeroed(CurrentProp->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, LocalBuffer.GetData()) ? LocalBuffer.GetData() : Node->GetRawValuePtr();
				if (!Ptr) return FText::FromString(TEXT("None"));

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
															 const FNumericProperty* NumProp, const bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (NumProp->IsFloatingPoint())
	{
		return SNew(SSpinBox<double>)
			.IsEnabled(bCanEdit)
			.Value_Lambda([Node]()
			{
				if (!Node.IsValid()) return 0.0;
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (!Prop) return 0.0;

				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
				if (!Ptr) return 0.0;
				
				return static_cast<double>(Prop->GetFloatingPointPropertyValue(Ptr));
			})
			.OnValueChanged_Lambda([this, Node](const double NewVal)
			{
				if (!Node.IsValid()) return;
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (!Prop) return;

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
				if (!Node.IsValid()) return 0;
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (!Prop) return 0;

				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(Prop->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
				if (!Ptr) return 0;
				
				return Prop->GetSignedIntPropertyValue(Ptr);
			})
			.OnValueChanged_Lambda([this, Node](int64 NewVal)
			{
				if (!Node.IsValid()) return;
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				if (!Prop) return;

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
			if (!Node.IsValid() || !Node->Property.Get()) return FText::GetEmpty();
			FProperty* Prop = Node->Property.Get();

			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (!Ptr) return FText::GetEmpty();

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
			if (!Node.IsValid()) return;
			FProperty* Prop = Node->Property.Get();
			if (!Prop) return;

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

	const FName StructName = StructProp->Struct ? StructProp->Struct->GetFName() : NAME_None;
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
				if (!Node.IsValid()) return FLinearColor::Transparent;
				
				TArray<uint8> Buffer;
				Buffer.SetNumZeroed(StructProp->GetSize());
				const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
				if (!Ptr) return FLinearColor::Transparent;

				if (StructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_Color)
				{
					return FLinearColor(*reinterpret_cast<const FColor*>(Ptr));
				}
				else if (StructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_LinearColor)
				{
					return *reinterpret_cast<const FLinearColor*>(Ptr);
				}
				return FLinearColor::Transparent;
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
			if (!Node.IsValid()) return FText::GetEmpty();
			const FStructProperty* Prop = CastField<FStructProperty>(Node->Property.Get());
			if (!Prop) return FText::FromString(TEXT("Error"));

			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			const uint8* Ptr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (!Ptr) return FText::FromString(TEXT("Error"));

			return FText::FromString(GetSpecialStructValue(Prop->Struct, Ptr));
		})
		.OnTextCommitted_Lambda([this, Node](const FText& NewText, ETextCommit::Type)
		{
			if (!Node.IsValid()) return;
			const FStructProperty* Prop = CastField<FStructProperty>(Node->Property.Get());
			if (!Prop) return;

			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(Prop->GetSize());
			
			// Grab current value to only override specified string representation part
			const uint8* GetterPtr = TryCallGetterSafe(Node, Buffer.GetData()) ? Buffer.GetData() : Node->GetRawValuePtr();
			if (GetterPtr && GetterPtr != Buffer.GetData())
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
		.ForegroundColor(FLinearColor(0.4f, 0.8f, 1.0f))
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
		if (ArrayProp->Inner->IsA<FByteProperty>() || ArrayProp->Inner->IsA<FInt8Property>())
		{
			return SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("Binary Data")))
				.ColorAndOpacity(FLinearColor::Gray);
		}

		const uint8* Ptr = Node->GetRawValuePtr();
		if (!Ptr)
		{
			return SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("Array (Null)")))
				.ColorAndOpacity(FLinearColor::Gray);
		}

		const FScriptArrayHelper Helper(ArrayProp, Ptr);
		const FString ArrayText = (Helper.Num() == 0) ? TEXT("Array (Empty)") : TEXT("Array (Expand to view)");
		return SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(ArrayText))
			.ColorAndOpacity(FLinearColor::Gray);
	}

	// Sets
	if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FLinearColor::Gray)
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid()) return FText::GetEmpty();
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
			.ColorAndOpacity(FLinearColor(0.6f, 1.0f, 0.6f))
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid()) return FText::GetEmpty();
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FSoftObjectProperty* SProp = CastField<FSoftObjectProperty>(Node->Property.Get()))
					{
						const FSoftObjectPtr SoftPtr = SProp->GetPropertyValue(Ptr);
						const FString Path = SoftPtr.ToString();
						if (Path.IsEmpty()) return FText::FromString(TEXT("None"));

						FString AssetName = FPaths::GetBaseFilename(Path);
						return FText::FromString(AssetName);
					}
				}
				return FText::FromString(TEXT("None"));
			})
			.ToolTipText_Lambda([Node]() -> FText
			{
				if (!Node.IsValid()) return FText::GetEmpty();
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
			.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f))
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid()) return FText::GetEmpty();
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
			.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.8f)) // Pinkish for Classes
			.Text_Lambda([Node]() -> FText
			{
				if (!Node.IsValid()) return FText::GetEmpty();
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					if (const FClassProperty* CProp = CastField<
						FClassProperty>(Node->Property.Get()))
					{
						UObject* ClassObj = CProp->GetObjectPropertyValue(Ptr);
						if (ClassObj)
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
			if (!Node.IsValid()) return FText::GetEmpty();

			if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Node->Property.Get()))
			{
				if (const uint8* Ptr = Node->GetRawValuePtr())
				{
					UObject* Obj = ObjectProperty->GetObjectPropertyValue(Ptr);
					if (!IsValid(Obj))
					{
						return FText::FromString(TEXT("Object (None)"));
					}
					return FText::FromString(FString::Printf(TEXT("Object (%s)"), *GetNameSafe(Obj)));
				}
			}
			return FText::FromString(Node->TypeName);
		})
		.ColorAndOpacity(FLinearColor::Gray);
}

#endif // WITH_PLUGIN_MODELVIEWVIEWMODEL