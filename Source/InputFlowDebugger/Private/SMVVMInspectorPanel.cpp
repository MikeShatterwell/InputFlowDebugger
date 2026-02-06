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
	
	const FText HighlightText = InArgs._InspectorPanel ? InArgs._InspectorPanel->GetHierarchySearchText() : FText::GetEmpty();
	
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

	const FText HighlightText = InArgs._InspectorPanel ? InArgs._InspectorPanel->GetPropertySearchText() : FText::GetEmpty();

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
	// Ensure we unregister all field-notify delegates we added.
	for (const TWeakObjectPtr<UObject>& WeakObj : ListenedObjects)
	{
		if (UObject* Obj = WeakObj.Get())
		{
			if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Obj))
			{
				Notify->RemoveAllFieldValueChangedDelegates(this);
			}
		}
	}
	ListenedObjects.Empty();
}

// ----------------------------------------------------------------------------------
// Hierarchy (Left)
// ----------------------------------------------------------------------------------

void SMVVMInspectorPanel::RefreshHierarchy()
{
	HierarchyRootNodes.Reset();

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	TArray<TSharedRef<SWindow>> Windows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);

	for (const TSharedRef<SWindow>& Window : Windows)
	{
		const TSharedPtr<FMVVMHierarchyNode> WindowNode = MakeShared<FMVVMHierarchyNode>();
		WindowNode->Widget = Window;
		WindowNode->WidgetName = FString::Printf(TEXT("Window: %s"), *Window->GetTitle().ToString());

		RecursivelyBuildHierarchy(Window, WindowNode);

		if (!WindowNode->Children.IsEmpty())
		{
			HierarchyRootNodes.Add(WindowNode);
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
	TSharedPtr<FMVVMHierarchyNode> ParentNode)
{
	if (!InWidget.IsValid())
	{
		return;
	}

	TSharedPtr<FMVVMHierarchyNode> CurrentNode = nullptr;
	UMVVMView* FoundView = nullptr;
	UUserWidget* UserWidget = nullptr;

	if (UWidget* OwnerUWidget = InputFlowHelpers::GetOwnerUWidget(InWidget); IsValid(OwnerUWidget))
	{
		if (UUserWidget* FoundUserWidget = Cast<UUserWidget>(OwnerUWidget); IsValid(FoundUserWidget))
		{
			UserWidget = FoundUserWidget;
			FoundView = UserWidget->GetExtension<UMVVMView>();
		}
	}

	if (IsValid(FoundView))
	{
		CurrentNode = MakeShared<FMVVMHierarchyNode>();
		CurrentNode->Widget = InWidget;
		CurrentNode->UserWidgetOwner = UserWidget;
		CurrentNode->MVVMView = FoundView;
		CurrentNode->WidgetName = UserWidget ? UserWidget->GetName() : TEXT("Unknown Widget");

		TArray<FString> SourceNames;
		for (const FMVVMView_Source& Source : FoundView->GetSources())
		{
			if (Source.Source && !MVVMInspectorPanel::IsWidgetObject(Source.Source))
			{
				SourceNames.Add(Source.Source->GetName());
			}
		}

		CurrentNode->ViewModelSummary = !SourceNames.IsEmpty()
											? FString::Join(SourceNames, TEXT(", "))
											: TEXT("No Sources");
	}

	const TSharedPtr<FMVVMHierarchyNode> NextParent = CurrentNode.IsValid() ? CurrentNode : ParentNode;

	if (FChildren* Children = InWidget->GetChildren())
	{
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			RecursivelyBuildHierarchy(Children->GetChildAt(i), NextParent);
		}
	}

	// Filter Logic:
	// Only add this node to the parent if it matches the search OR if it has children that matched.
	if (CurrentNode.IsValid() && ParentNode.IsValid())
	{
		bool bMatches = HierarchyFilterString.IsEmpty();
		if (!bMatches)
		{
			bMatches = CurrentNode->WidgetName.Contains(HierarchyFilterString) || 
					   CurrentNode->ViewModelSummary.Contains(HierarchyFilterString);
		}

		if (bMatches || !CurrentNode->Children.IsEmpty())
		{
			ParentNode->Children.Add(CurrentNode);
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
	OutChildren = Item->Children;
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
	if (!IsValid(Object) || ListenedObjects.Contains(Object))
	{
		return;
	}

	if (INotifyFieldValueChanged* NotifyInterface = Cast<INotifyFieldValueChanged>(Object))
	{
		TWeakPtr<SMVVMInspectorPanel> WeakPanel = SharedThis(this);
		NotifyInterface->AddFieldValueChangedDelegate(
		UE::FieldNotification::FFieldId(),
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateSP(this, &SMVVMInspectorPanel::OnFieldChanged)
	);

		ListenedObjects.Add(Object);
	}
}

void SMVVMInspectorPanel::OnFieldChanged(UObject* Obj, UE::FieldNotification::FFieldId Id)
{
	if (CurrentSelection.IsValid())
	{
		RebuildPropertyTree(CurrentSelection.Pin());
	}
}

void SMVVMInspectorPanel::RebuildPropertyTree(TSharedPtr<FMVVMHierarchyNode> SelectedNode)
{
	// Clear old listeners.
	for (const TWeakObjectPtr<UObject>& WeakObj : ListenedObjects)
	{
		if (UObject* Obj = WeakObj.Get())
		{
			if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(Obj))
			{
				Notify->RemoveAllFieldValueChangedDelegates(this);
			}
		}
	}
	ListenedObjects.Empty();

	PropertyRootNodes.Reset();

	if (!SelectedNode.IsValid())
	{
		if (PropertyTreeView.IsValid())
		{
			PropertyTreeView->RequestTreeRefresh();
		}
		return;
	}

	// Add ViewModels (filtering out widget sources).
	if (SelectedNode->MVVMView.IsSet())
	{
		UMVVMView* View = SelectedNode->MVVMView.GetValue().Get();
		if (IsValid(View))
		{
			for (const FMVVMView_Source& Source : View->GetSources())
			{
				UObject* VM = Source.Source;
				if (!IsValid(VM) || MVVMInspectorPanel::IsWidgetObject(VM))
				{
					continue;
				}

				const TSharedPtr<FMVVMPropertyNode> VMRoot = MakeShared<FMVVMPropertyNode>();
				VMRoot->DisplayName = FString::Printf(TEXT("ViewModel: %s"), *VM->GetName());
				VMRoot->TypeName = VM->GetClass()->GetName();
				VMRoot->EffectiveOwner = VM;
				VMRoot->bIsCategoryRoot = true;

				VMRoot->Children = GeneratePropertyNodes(reinterpret_cast<uint8*>(VM), VM->GetClass(), VM, VMRoot);

				// Filter Logic:
				// Add the root if it matches OR if any children matched
				bool bMatches = PropertyFilterString.IsEmpty();
				if (!bMatches)
				{
					bMatches = VMRoot->DisplayName.Contains(PropertyFilterString) || 
							   VMRoot->TypeName.Contains(PropertyFilterString);
				}

				if (bMatches || !VMRoot->Children.IsEmpty())
				{
					PropertyRootNodes.Add(VMRoot);
					SetupChangeListener(VM);
				}
			}
		}
	}

	if (PropertyTreeView.IsValid())
	{
		PropertyTreeView->RequestTreeRefresh();
		// Always expand root, recursive expand if searching
		const bool bRecursiveExpand = !PropertyFilterString.IsEmpty();
		for (const TSharedPtr<FMVVMPropertyNode>& Node : PropertyRootNodes)
		{
			SetPropertyExpansion(Node, bRecursiveExpand);
		}
	}
}

// ---- Special Struct Handling ----

bool SMVVMInspectorPanel::IsSpecialStruct(const UScriptStruct* Struct) const
{
	if (!Struct)
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
	if (!Node.IsValid()) return;
	
	const FStructProperty* StructProp = CastField<FStructProperty>(Node->Property.Get());
	uint8* Ptr = Node->GetRawValuePtr();
	
	if (!StructProp || !StructProp->Struct || !Ptr) return;

	const FName StructName = StructProp->Struct->GetFName();

	// Handle FGameplayTag
	if (StructName == MVVMInspectorPanel::NAME_GameplayTag)
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
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_BlueprintVisible))
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
								SubObj->GetClass(), SubObj, ElementNode, CurrentDepth + 1);
							SetupChangeListener(SubObj);
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
		// Structs
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (!IsSpecialStruct(StructProp->Struct))
			{
				Node->Children = GeneratePropertyNodes(PropValuePtr, StructProp->Struct, OwnerObject, Node, CurrentDepth + 1);
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
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Parent->Property.Get()))
	{
		UObject* ActualObject = ObjProp->GetObjectPropertyValue(ParentValueAddress);
		return reinterpret_cast<uint8*>(ActualObject);
	}

	// Struct members use the parent's value address as the container.
	return ParentValueAddress;
}

uint8* FMVVMPropertyNode::GetRawValuePtr() const
{
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

TSharedRef<SWidget> SMVVMInspectorPanel::CreateValueWidget(TSharedPtr<FMVVMPropertyNode> Node)
{
	FProperty* Prop = Node ? Node->Property.Get() : nullptr;
	if (!Prop)
	{
		return SNullWidget::NullWidget;
	}

	// Editing is disabled for widget instances (generally unsafe to mutate at runtime here).
	const bool bIsWidgetOwner = Node->EffectiveOwner.IsValid() && Node->EffectiveOwner->IsA(UWidget::StaticClass());
	const bool bCanEdit = !bIsWidgetOwner;

	auto NotifyChange = [Node]()
	{
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

		if (!OwnerClass || !NotifyProp)
		{
			return;
		}

		// If we're editing a nested struct member, find the first property in the chain that belongs to the owner class
		// so FieldNotify can broadcast the correct top-level field.
		for (TSharedPtr<FMVVMPropertyNode> Walker = Node; Walker.IsValid() && Walker->Property.Get(); Walker = Walker->
			 ParentNode.Pin())
		{
			if (OwnerClass->IsChildOf(Walker->Property->GetOwnerClass()))
			{
				NotifyProp = Walker->Property.Get();
				break;
			}
		}

		if (!NotifyProp)
		{
			return;
		}

		const UE::FieldNotification::FFieldId FieldId =
			Notify->GetFieldNotificationDescriptor().GetField(OwnerClass, NotifyProp->GetFName());

		if (FieldId.IsValid())
		{
			Notify->BroadcastFieldValueChanged(FieldId);
		}
	};

	// Bool
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		return SNew(SCheckBox)
			.IsEnabled(bCanEdit)
			.IsChecked_Lambda([Node]()
			{
				if (!Node.IsValid())
				{
					return ECheckBoxState::Unchecked;
				}
				const FBoolProperty* BoolProp = CastField<FBoolProperty>(Node->Property.Get());
				const uint8* Ptr = Node->GetRawValuePtr();
				if (!BoolProp || !Ptr)
				{
					return ECheckBoxState::Unchecked;
				}
				return (Ptr && BoolProp->GetPropertyValue(Ptr)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Node, NotifyChange](ECheckBoxState NewState)
			{
				if (!Node.IsValid())
				{
					return;
				}
				if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Node->Property.Get()))
				{
					if (uint8* Ptr = Node->GetRawValuePtr())
					{
						BoolProp->SetPropertyValue(Ptr, NewState == ECheckBoxState::Checked);
						NotifyChange();
					}
				}
			});
	}

	// Enums (EnumProperty or ByteProperty with Enum)
	UEnum* EnumDef = nullptr;
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		EnumDef = EnumProp->GetEnum();
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		EnumDef = ByteProp->Enum;
	}

	if (EnumDef)
	{
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

		TSharedPtr<int64> CurrentItem;
		if (const uint8* Ptr = Node->GetRawValuePtr())
		{
			int64 CurrentVal = 0;
			if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
			{
				CurrentVal = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
			}
			else if (FByteProperty* BP = CastField<FByteProperty>(Prop))
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
			.OnSelectionChanged_Lambda([Node, NotifyChange](TSharedPtr<int64> NewValue, ESelectInfo::Type)
			{
				if (!NewValue.IsValid() || !Node.IsValid())
				{
					return;
				}

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
					NotifyChange();
				}
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
					const uint8* Ptr = Node->GetRawValuePtr();
					if (!Ptr || !CurrentProp)
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

	// Numeric (int/float/double)
	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
	{
		if (NumProp->IsFloatingPoint())
		{
			return SNew(SSpinBox<double>)
				.IsEnabled(bCanEdit)
				.Value_Lambda([Node]()
				{
					if (!Node.IsValid())
					{
						return 0.0f;
					}
					const FNumericProperty* NumProp = CastField<FNumericProperty>(Node->Property.Get());
					const uint8* Ptr = Node->GetRawValuePtr();
					if (!NumProp || !Ptr)
					{
						return 0.0f;
					}
					return static_cast<float>(NumProp->GetFloatingPointPropertyValue(Ptr));
				})
				.OnValueChanged_Lambda([Node, NotifyChange](const double NewVal)
				{
					if (!Node.IsValid())
					{
						return;
					}
					const FNumericProperty* NumProp = CastField<FNumericProperty>(Node->Property.Get());
					uint8* Ptr = Node->GetRawValuePtr();
					if (!NumProp || !Ptr)
					{
						return;
					}

					NumProp->SetFloatingPointPropertyValue(Ptr, NewVal);
					NotifyChange();
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
					const uint8* Ptr = Node->GetRawValuePtr();
					const FNumericProperty* NumProp = CastField<FNumericProperty>(Node->Property.Get());
					if (!Ptr || !NumProp)
					{
						return 0;
					}
					return NumProp->GetSignedIntPropertyValue(Ptr);
				})
				.OnValueChanged_Lambda([Node, NotifyChange](int64 NewVal)
				{
					if (!Node.IsValid())
					{
						return;
					}
					uint8* Ptr = Node->GetRawValuePtr();
					const FNumericProperty* NumProp = CastField<FNumericProperty>(Node->Property.Get());
					if (!Ptr || !NumProp)
					{
						return;
					}
					NumProp->SetIntPropertyValue(Ptr, NewVal);
					NotifyChange();
				});
		}
	}

	// String-like (FString/FText/FName)
	if (Prop->IsA<FStrProperty>() || Prop->IsA<FTextProperty>() || Prop->IsA<FNameProperty>())
	{
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
			.OnTextCommitted_Lambda([Node, NotifyChange](const FText& NewText, ETextCommit::Type)
			{
				if (!Node.IsValid())
				{
					return;
				}
				if (uint8* Ptr = Node->GetRawValuePtr())
				{
					Node->Property->ImportText_Direct(*NewText.ToString(), Ptr, Node->EffectiveOwner.Get(), PPF_None);
					NotifyChange();
				}
			});
	}

	// Special structs (render/edit as text)
	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct && StaticCast<const SMVVMInspectorPanel*>(this)->IsSpecialStruct(StructProp->Struct))
		{
			return SNew(SEditableTextBox)
				.IsEnabled(bCanEdit)
				.Text_Lambda([this, Node]()
				{
					if (!Node.IsValid()) return FText::GetEmpty();
					const uint8* Ptr = Node->GetRawValuePtr();
					const FStructProperty* StructProp = CastField<FStructProperty>(Node->Property.Get());
					if (!StructProp || !Ptr)
					{
						return FText::FromString(TEXT("Error"));
					}
					return FText::FromString(GetSpecialStructValue(StructProp->Struct, Ptr));
				})
				.OnTextCommitted_Lambda([this, Node, NotifyChange](const FText& NewText, ETextCommit::Type)
				{
					if (!Node.IsValid()) return;

					SetSpecialStructValue(Node, NewText.ToString());
					NotifyChange();
					
				})
				.ForegroundColor(FLinearColor(0.4f, 0.8f, 1.0f));
		}
	}

	// Arrays: show summary text (expand to view elements)
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

	// Fallback: show the type name
	return SNew(STextBlock)
		.AutoWrapText(true)
		.Text(FText::FromString(Node->TypeName))
		.ColorAndOpacity(FLinearColor::Gray);
}

#endif // WITH_PLUGIN_MODELVIEWVIEWMODEL
