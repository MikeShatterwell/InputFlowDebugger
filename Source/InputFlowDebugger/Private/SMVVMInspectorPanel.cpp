// Copyright Mike Desrosiers, All Rights Reserved. 

#include "SMVVMInspectorPanel.h"


#if WITH_PLUGIN_MODELVIEWVIEWMODEL

// Internal 
#include "InputFlowHelpers.h"

// UMG 
#include <Blueprint/UserWidget.h>
#include <Components/Widget.h>

// ModelViewViewModel 
#include <MVVMViewModelBase.h>
#include <View/MVVMView.h>

// CoreUObject 
#include <UObject/EnumProperty.h>
#include <UObject/TextProperty.h>
#include <UObject/UnrealType.h>
#include <StructUtils/InstancedStruct.h>
#include <UObject/UObjectIterator.h>

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
		return Obj && Obj->IsA(UWidget::StaticClass());
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
					.ToolTipText(FText::FromString(InItem->TypeName))
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

SMVVMInspectorPanel::~SMVVMInspectorPanel()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	// Ensure we unregister all field-notify delegates we added. 
	for (auto& Elem : ListenedFields)
	{
		if (UObject* Obj = Elem.Key.Get(); IsValid(Obj))
		{
			if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Obj))
			{
				Notify->RemoveAllFieldValueChangedDelegates(this);
			}
		}
	}
	ListenedFields.Empty();
}

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

	if (!GEngine || !IsValid(GEngine->GameViewport))
	{
		return;
	}

	const TSharedPtr<SWidget> RootWidget = GEngine->GameViewport->GetGameViewportWidget();
	if (!RootWidget.IsValid())
	{
		return;
	}

	// Keep track of visited widgets to prevent cycles, though Slate tree usually prevents this naturally.
	TSet<UUserWidget*> ProcessedWidgets;

	// Start recursion from the Viewport's Slate Widget. 
	// Pass nullptr as parent so the first valid MVVM widget found becomes a RootNode.
	RecursivelyBuildHierarchy(RootWidget, nullptr, ProcessedWidgets);

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
			SourceNames.Add(GetNameSafe(Source.Source));
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

void SMVVMInspectorPanel::SetupChangeListener(UObject* Object, const FProperty* Property)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!IsValid(Object) || !Property) return;

	const bool bIsStructural =
		Property->IsA<FObjectProperty>() ||
		Property->IsA<FArrayProperty>() ||
		Property->IsA<FSetProperty>() ||
		Property->IsA<FMapProperty>() ||
		Property->IsA<FClassProperty>() ||
		Property->IsA<FSoftObjectProperty>();

	// InstancedStructs can change topology
	bool bIsInstancedStruct = false;
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct->GetFName() == MVVMInspectorPanel::NAME_InstancedStruct)
		{
			bIsInstancedStruct = true;
		}
	}

	// If it's not a property that changes the tree structure, don't listen.
	if (!bIsStructural && !bIsInstancedStruct)
	{
		return;
	}

	INotifyFieldValueChanged* NotifyInterface = Cast<INotifyFieldValueChanged>(Object);
	if (!NotifyInterface) return;

	// Use the definition class to find the FieldId (Fix from previous step)
	const UClass* DefinitionClass = Property->GetOwnerClass();
	if (!IsValid(DefinitionClass)) return;

	const UE::FieldNotification::IClassDescriptor& Descriptor = NotifyInterface->GetFieldNotificationDescriptor();
	const UE::FieldNotification::FFieldId FieldId = Descriptor.GetField(DefinitionClass, Property->GetFName());

	if (!FieldId.IsValid()) return;

	TSet<UE::FieldNotification::FFieldId>& FieldsForObject = ListenedFields.FindOrAdd(Object);
	if (FieldsForObject.Contains(FieldId)) return;

	// Bind Delegate
	auto Delegate = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateSP(
		this, &SMVVMInspectorPanel::OnFieldChanged);
	NotifyInterface->AddFieldValueChangedDelegate(FieldId, Delegate);

	FieldsForObject.Add(FieldId);
}

void SMVVMInspectorPanel::OnFieldChanged(UObject* Obj, UE::FieldNotification::FFieldId Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (CurrentSelection.IsValid())
	{
		RebuildPropertyTree(CurrentSelection.Pin());
	}
}

void SMVVMInspectorPanel::RebuildPropertyTree(TSharedPtr<FMVVMHierarchyNode> SelectedNode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	// Capture Expansion State
	TSet<FString> ExpandedPaths;

	// Helper lambda to construct a unique path string for a node
	// Example: "ViewModel.DynamicData.X"
	auto GetNodePath = [](TSharedPtr<FMVVMPropertyNode> Node) -> FString
	{
		if (!Node.IsValid()) return FString();

		FString Path = Node->DisplayName;
		TSharedPtr<FMVVMPropertyNode> CurrentParent = Node->ParentNode.Pin();
		while (CurrentParent.IsValid())
		{
			Path = CurrentParent->DisplayName + TEXT(".") + Path;
			CurrentParent = CurrentParent->ParentNode.Pin();
		}
		return Path;
	};

	if (PropertyTreeView.IsValid())
	{
		// Recursively save paths of currently expanded nodes
		TFunction<void(TSharedPtr<FMVVMPropertyNode>)> SaveExpansion;
		SaveExpansion = [&](TSharedPtr<FMVVMPropertyNode> Node)
		{
			if (Node.IsValid() && PropertyTreeView->IsItemExpanded(Node))
			{
				ExpandedPaths.Add(GetNodePath(Node));
				for (const auto& Child : Node->Children)
				{
					SaveExpansion(Child);
				}
			}
		};

		for (const auto& Root : PropertyRootNodes)
		{
			SaveExpansion(Root);
		}
	}

	// Clear old listeners
	for (auto& Elem : ListenedFields)
	{
		if (UObject* Obj = Elem.Key.Get(); IsValid(Obj))
		{
			if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Obj))
			{
				Notify->RemoveAllFieldValueChangedDelegates(this);
			}
		}
	}
	ListenedFields.Empty();

	PropertyRootNodes.Reset();

	if (!SelectedNode.IsValid())
	{
		if (PropertyTreeView.IsValid()) PropertyTreeView->RequestTreeRefresh();
		return;
	}

	// Regenerate Tree
	if (SelectedNode->MVVMView.IsSet())
	{
		const UMVVMView* View = SelectedNode->MVVMView.GetValue().Get();
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
				}
			}
		}
	}

	// Restore Expansion State
	if (PropertyTreeView.IsValid())
	{
		PropertyTreeView->RequestTreeRefresh();

		const bool bIsFiltering = !PropertyFilterString.IsEmpty();

		// Recursive lambda to restore state
		TFunction<void(TSharedPtr<FMVVMPropertyNode>)> RestoreExpansion;
		RestoreExpansion = [&](TSharedPtr<FMVVMPropertyNode> Node)
		{
			if (!Node.IsValid()) return;

			// Expand if user is searching (always expand matches) OR if it was previously expanded
			bool bShouldExpand = bIsFiltering;
			if (!bShouldExpand)
			{
				bShouldExpand = ExpandedPaths.Contains(GetNodePath(Node));
			}

			if (bShouldExpand)
			{
				PropertyTreeView->SetItemExpansion(Node, true);
				for (const auto& Child : Node->Children)
				{
					RestoreExpansion(Child);
				}
			}
		};

		for (const TSharedPtr<FMVVMPropertyNode>& Node : PropertyRootNodes)
		{
			RestoreExpansion(Node);
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

void SMVVMInspectorPanel::SetSpecialStructValue(TSharedPtr<FMVVMPropertyNode> Node, const FString& NewStringValue)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!Node.IsValid()) return;

	const FStructProperty* StructProp = CastField<FStructProperty>(Node->Property.Get());
	uint8* Ptr = Node->GetRawValuePtr();

	if (!StructProp || !StructProp->Struct || !Ptr) return;

	const FName StructName = StructProp->Struct->GetFName();

	// Handle FGameplayTag 
	if (StructName == MVVMInspectorPanel::NAME_GameplayTag && NewStringValue != TEXT("None"))
	{
		FString CleanValue = NewStringValue;
		CleanValue.TrimStartAndEndInline();

		const FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(FName(*CleanValue));
		StructProp->Struct->CopyScriptStruct(Ptr, &NewTag);
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

		StructProp->Struct->CopyScriptStruct(Ptr, &NewContainer);
	}
	// Fallback for others (Vector, etc) 
	else
	{
		Node->Property->ImportText_Direct(*NewStringValue, Ptr, Node->EffectiveOwner.Get(), PPF_None);
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
	auto IsObjectInAncestry = [&](UObject* Obj) -> bool
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
		Node->ParentNode = Parent;

		uint8* PropValuePtr = Prop->ContainerPtrToValuePtr<uint8>(BaseAddress);

		// Arrays 
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
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
				ElementNode->ParentNode = Node;
				ElementNode->ArrayIndex = i;

				uint8* ElementAddr = Helper.GetRawPtr(i);

				if (FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner))
				{
					if (!IsSpecialStruct(InnerStructProp->Struct))
					{
						ElementNode->Children = GeneratePropertyNodes(ElementAddr, InnerStructProp->Struct, OwnerObject,
						                                              ElementNode, CurrentDepth + 1);
					}
				}
				else if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
				{
					UObject* SubObj = InnerObjProp->GetObjectPropertyValue(ElementAddr);
					if (SubObj && !MVVMInspectorPanel::IsWidgetObject(SubObj))
					{
						if (!IsObjectInAncestry(SubObj))
						{
							ElementNode->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(SubObj),
							                                              SubObj->GetClass(), SubObj, ElementNode,
							                                              CurrentDepth + 1);
							SetupChangeListener(SubObj, InnerObjProp);
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
					const TSharedPtr<FMVVMPropertyNode> EntryNode = MakeShared<FMVVMPropertyNode>();
					FString KeyStr;
					Helper.GetKeyProperty()->ExportText_Direct(KeyStr, Helper.GetKeyPtr(i), nullptr, nullptr, PPF_None);

					EntryNode->DisplayName = FString::Printf(TEXT("[%s]"), *KeyStr);
					EntryNode->TypeName = MapProp->ValueProp->GetCPPType();
					EntryNode->EffectiveOwner = OwnerObject;
					EntryNode->Property = MapProp->ValueProp;
					EntryNode->ParentNode = Node;
					EntryNode->ArrayIndex = i;

					uint8* ValueAddr = Helper.GetValuePtr(i);
					if (FStructProperty* ValueStructProp = CastField<FStructProperty>(MapProp->ValueProp))
					{
						if (!IsSpecialStruct(ValueStructProp->Struct))
						{
							EntryNode->Children = GeneratePropertyNodes(ValueAddr, ValueStructProp->Struct, OwnerObject,
							                                            EntryNode, CurrentDepth + 1);
						}
					}
					else if (FObjectProperty* ValueObjProp = CastField<FObjectProperty>(MapProp->ValueProp))
					{
						UObject* SubObj = ValueObjProp->GetObjectPropertyValue(ValueAddr);
						if (IsValid(SubObj) && !MVVMInspectorPanel::IsWidgetObject(SubObj) && !IsObjectInAncestry(
							SubObj))
						{
							EntryNode->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(SubObj),
							                                            SubObj->GetClass(), SubObj, EntryNode,
							                                            CurrentDepth + 1);
							SetupChangeListener(SubObj, ValueObjProp);
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
			if (!IsSpecialStruct(StructProp->Struct))
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
				if (!MVVMInspectorPanel::IsWidgetObject(SubObj))
				{
					Node->TypeName = FString::Printf(TEXT("Object (%s)"), *SubObj->GetName());

					if (!IsObjectInAncestry(SubObj))
					{
						Node->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(SubObj), SubObj->GetClass(),
						                                       SubObj, Node, CurrentDepth + 1);
						SetupChangeListener(SubObj, ObjProp);
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
// Value Widgets 
// ----------------------------------------------------------------------------------

void SMVVMInspectorPanel::NotifyPropertyValueChanged(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!Node.IsValid() || !Node->EffectiveOwner.IsValid())
	{
		return;
	}

	INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Node->EffectiveOwner.Get());
	if (!Notify)
	{
		return;
	}

	const UClass* OwnerClass = Node->EffectiveOwner->GetClass();
	const FProperty* NotifyProp = Node->Property.Get();

	if (!NotifyProp)
	{
		return;
	}

	// If we're editing a nested struct member (e.g. Vector.X), we need to find the 
	// top-level UObject property (e.g. the Vector itself) to broadcast the change.
	// We walk up the tree until we find a property owned by a UClass (not a UStruct).
	for (TSharedPtr<FMVVMPropertyNode> Walker = Node; Walker.IsValid() && Walker->Property.Get(); Walker = Walker->
	     ParentNode.Pin())
	{
		// FProperty::GetOwnerClass() returns the UClass owner, or nullptr if owned by a UStruct.
		if (const UClass* PropOwner = Walker->Property->GetOwnerClass())
		{
			// We found the top-level property on the object.
			NotifyProp = Walker->Property.Get();
			break;
		}
	}
	const UClass* DefinitionClass = NotifyProp->GetOwnerClass();

	if (DefinitionClass)
	{
		const UE::FieldNotification::FFieldId FieldId =
			Notify->GetFieldNotificationDescriptor().GetField(DefinitionClass, NotifyProp->GetFName());

		if (FieldId.IsValid())
		{
			Notify->BroadcastFieldValueChanged(FieldId);
		}
	}
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateValueWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	FProperty* Prop = Node ? Node->Property.Get() : nullptr;
	if (!Prop)
	{
		return SNullWidget::NullWidget;
	}

	// Editing is disabled for widget instances (generally unsafe to mutate at runtime here). 
	const bool bIsWidgetOwner = Node->EffectiveOwner.IsValid() && Node->EffectiveOwner->IsA(UWidget::StaticClass());
	const bool bCanEdit = !bIsWidgetOwner;

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
			const uint8* Ptr = Node->GetRawValuePtr();

			if (!Prop || !Ptr) return ECheckBoxState::Unchecked;
			return (Ptr && Prop->GetPropertyValue(Ptr)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, Node](ECheckBoxState NewState)
		{
			if (!Node.IsValid()) return;

			if (const FBoolProperty* Prop = CastField<FBoolProperty>(Node->Property.Get()))
			{
				if (uint8* Ptr = Node->GetRawValuePtr())
				{
					Prop->SetPropertyValue(Ptr, NewState == ECheckBoxState::Checked);
					NotifyPropertyValueChanged(Node);
				}
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
			if (EnumDef->HasMetaData(TEXT("Hidden"), i))
			{
				continue;
			}
			Node->EnumOptionValues.Add(MakeShared<int64>(EnumDef->GetValueByIndex(i)));
		}
	}

	// Determine current selection
	TSharedPtr<int64> CurrentItem;
	if (const uint8* Ptr = Node->GetRawValuePtr())
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
			if (uint8* Ptr = Node->GetRawValuePtr())
			{
				const int64 Val = *NewValue;
				if (FEnumProperty* EP = CastField<FEnumProperty>(CurrentProp))
				{
					EP->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Val);
				}
				else if (FByteProperty* BP = CastField<FByteProperty>(CurrentProp))
				{
					*Ptr = static_cast<uint8>(Val);
				}
				NotifyPropertyValueChanged(Node);
			}
		})
		.Content()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([Node, EnumDef]()
			{
				if (!Node.IsValid()) return FText::FromString("Invalid Node");

				FProperty* CurrentProp = Node->Property.Get();
				const uint8* Ptr = Node->GetRawValuePtr();
				if (!Ptr || !CurrentProp) return FText::FromString(TEXT("None"));

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
				if (!Node.IsValid()) return 0.0f;

				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				const uint8* Ptr = Node->GetRawValuePtr();

				if (!Prop || !Ptr) return 0.0f;
				return static_cast<float>(Prop->GetFloatingPointPropertyValue(Ptr));
			})
			.OnValueChanged_Lambda([this, Node](const double NewVal)
			{
				if (!Node.IsValid()) return;

				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());
				uint8* Ptr = Node->GetRawValuePtr();

				if (!Prop || !Ptr) return;

				Prop->SetFloatingPointPropertyValue(Ptr, NewVal);
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

				const uint8* Ptr = Node->GetRawValuePtr();
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());

				if (!Ptr || !Prop) return 0;
				return Prop->GetSignedIntPropertyValue(Ptr);
			})
			.OnValueChanged_Lambda([this, Node](int64 NewVal)
			{
				if (!Node.IsValid()) return;

				uint8* Ptr = Node->GetRawValuePtr();
				const FNumericProperty* Prop = CastField<FNumericProperty>(Node->Property.Get());

				if (!Ptr || !Prop) return;

				Prop->SetIntPropertyValue(Ptr, NewVal);
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
			const uint8* Ptr = Node->GetRawValuePtr();
			if (!Ptr || !Node.IsValid() || !Node->Property.Get())
			{
				return FText::GetEmpty();
			}

			if (const FTextProperty* TextProp = CastField<FTextProperty>(Node->Property.Get()))
			{
				return TextProp->GetPropertyValue(Ptr);
			}

			FString ValStr;
			Node->Property->ExportText_Direct(ValStr, Ptr, nullptr, nullptr, PPF_None);
			return FText::FromString(ValStr);
		})
		.OnTextCommitted_Lambda([this, Node](const FText& NewText, ETextCommit::Type)
		{
			if (!Node.IsValid()) return;

			if (uint8* Ptr = Node->GetRawValuePtr())
			{
				Node->Property->ImportText_Direct(*NewText.ToString(), Ptr, Node->EffectiveOwner.Get(), PPF_None);
				NotifyPropertyValueChanged(Node);
			}
		});
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateSpecialStructWidget(TSharedPtr<FMVVMPropertyNode> Node,
                                                                   const FStructProperty* StructProp, bool bCanEdit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	return SNew(SEditableTextBox)
		.IsEnabled(bCanEdit)
		.Text_Lambda([this, Node]()
		{
			if (!Node.IsValid()) return FText::GetEmpty();

			const uint8* Ptr = Node->GetRawValuePtr();
			const FStructProperty* Prop = CastField<FStructProperty>(Node->Property.Get());

			if (!Prop || !Ptr) return FText::FromString(TEXT("Error"));
			return FText::FromString(GetSpecialStructValue(Prop->Struct, Ptr));
		})
		.OnTextCommitted_Lambda([this, Node](const FText& NewText, ETextCommit::Type)
		{
			if (!Node.IsValid()) return;

			SetSpecialStructValue(Node, NewText.ToString());
			NotifyPropertyValueChanged(Node);
		})
		.ForegroundColor(FLinearColor(0.4f, 0.8f, 1.0f));
}

TSharedRef<SWidget> SMVVMInspectorPanel::CreateContainerWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	const FProperty* Prop = Node->Property.Get();

	// Arrays
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
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
