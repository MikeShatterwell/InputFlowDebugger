// Copyright Mike Desrosiers, All Rights Reserved. 

#pragma once

// Core 
#include <CoreMinimal.h>
#include <UObject/FieldPath.h>
#include <UObject/StructOnScope.h>

// Slate 
#include <Widgets/SCompoundWidget.h>
#include <Widgets/Views/STableRow.h>
#include <Widgets/Views/STreeView.h>

// FieldNotification 
#include <FieldNotificationId.h>

#if WITH_PLUGIN_MODELVIEWVIEWMODEL

class SMVVMInspectorPanel;
class FProperty;
class STableViewBase;
class SWidget;
class UMVVMView;
class UUserWidget;
class SSearchBox;

/** 
* Hierarchy node for the left-hand widget tree. 
* We only materialize nodes for widgets that have an MVVMView extension. 
*/
struct FMVVMHierarchyNode : public TSharedFromThis<FMVVMHierarchyNode>
{
	/** Slate widget instance for this node. */
	TWeakPtr<SWidget> Widget;

	/** Friendly name of the widget (usually UUserWidget name). */
	FString WidgetName;

	/** Comma-separated summary of MVVM sources. */
	FString ViewModelSummary;

	/** Child hierarchy nodes. */
	TArray<TSharedPtr<FMVVMHierarchyNode>> Children;

	/** Optional MVVM view extension if present. */
	TWeakObjectPtr<UMVVMView> MVVMView;

	/** Owning UUserWidget (if the Slate widget belongs to one). */
	TWeakObjectPtr<UUserWidget> UserWidgetOwner;

	bool HasViewModels() const { return MVVMView.IsValid(); }
};

/** 
* Property node for the right-hand property tree. 
* The node can represent a top-level "ViewModel: X" category root, a property, or an array element. 
*/
struct FMVVMPropertyNode : public TSharedFromThis<FMVVMPropertyNode>
{
	/** Display label in the tree. */
	FString DisplayName = "";

	/** Human-readable type string. */
	FString TypeName = "";

	/** String displayed in the property widget's tooltip. */
	FString TooltipText = "";

	/** The UObject that should receive FieldNotify broadcasts for this node. */
	TWeakObjectPtr<UObject> EffectiveOwner = nullptr;

	/** Property definition for this node (or array inner when ArrayIndex is set). */
	TFieldPath<FProperty> Property = nullptr;

	/** If this represents an array element, the index in the parent array; otherwise INDEX_NONE. */
	int32 ArrayIndex = INDEX_NONE;

	/** Parent and children for the tree view. */
	TWeakPtr<FMVVMPropertyNode> ParentNode = nullptr;
	TArray<TSharedPtr<FMVVMPropertyNode>> Children;

	/** True when this node is a category root (ViewModel header). */
	bool bIsCategoryRoot = false;

	/** Is this node representing a UFunction? */
	bool bIsFunction = false;

	/** Is this node representing a Delegate property? */
	bool bIsDelegate = false;

	/** The function or delegate signature function */
	TWeakObjectPtr<UFunction> Function = nullptr;

	/**
	 * Parameter memory buffer for the function/delegate to hold inputs/outputs locally
	 * This lets us expose function parameters as editable properties and pass them to ProcessEvent when invoking the function from the inspector.
	 */
	TSharedPtr<FStructOnScope> FunctionParams;
	
	FMVVMPropertyNode() = default;

	/** Cached enum option values for combo boxes. */
	TArray<TSharedPtr<int64>> EnumOptionValues;

	/** Returns the raw value pointer represented by this node. */
	uint8* GetRawValuePtr() const;

	/** Returns the container pointer used to resolve GetRawValuePtr (struct/object container). */
	uint8* GetContainerPtr() const;
};

/** 
* Row widget for the hierarchy tree. 
*/
class SMVVMHierarchyRow : public STableRow<TSharedPtr<FMVVMHierarchyNode>>
{
public:
	SLATE_BEGIN_ARGS(SMVVMHierarchyRow) : _InspectorPanel(nullptr)
		{
		}

		SLATE_ARGUMENT(SMVVMInspectorPanel*, InspectorPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs,
	               const TSharedRef<STableViewBase>& InOwnerTableView,
	               TSharedPtr<FMVVMHierarchyNode> InItem);
};

/** 
* Row widget for the property tree. 
*/
class SMVVMPropertyRow : public STableRow<TSharedPtr<FMVVMPropertyNode>>
{
public:
	SLATE_BEGIN_ARGS(SMVVMPropertyRow)
			: _InspectorPanel(nullptr)
		{
		}

		SLATE_ARGUMENT(class SMVVMInspectorPanel*, InspectorPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs,
	               const TSharedRef<STableViewBase>& InOwnerTableView,
	               TSharedPtr<FMVVMPropertyNode> InItem);
};

/** 
* Inspector panel displaying MVVM sources (ViewModels) and their reflected properties. 
* 
* Left: widget hierarchy nodes that have an MVVMView extension. 
* Right: properties for the selected node's MVVM sources, including nested objects and arrays. 
*/
class SMVVMInspectorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMInspectorPanel)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMVVMInspectorPanel() override;

	/** Rebuilds the hierarchy view. */
	void RefreshHierarchy();

	/** Factory for property value widgets (right-hand column). */
	static void NotifyPropertyValueChanged(TSharedPtr<FMVVMPropertyNode> Node);
	TSharedRef<SWidget> CreateValueWidget(TSharedPtr<FMVVMPropertyNode> Node);
	TSharedRef<SWidget> CreateBoolWidget(TSharedPtr<FMVVMPropertyNode> Node, const FBoolProperty* BoolProp, bool bCanEdit);
	TSharedRef<SWidget> CreateEnumWidget(TSharedPtr<FMVVMPropertyNode> Node, const FProperty* Prop, bool bCanEdit);
	TSharedRef<SWidget> CreateNumericWidget(TSharedPtr<FMVVMPropertyNode> Node, const FNumericProperty* IntProp, bool bCanEdit);
	TSharedRef<SWidget> CreateStringWidget(TSharedPtr<FMVVMPropertyNode> Node, bool bCanEdit);
	TSharedRef<SWidget> CreateSpecialStructWidget(TSharedPtr<FMVVMPropertyNode> Node, const FStructProperty* StructProp, bool bCanEdit);
	TSharedRef<SWidget> CreateContainerWidget(TSharedPtr<FMVVMPropertyNode> Node);
	TSharedRef<SWidget> CreateObjectLikeWidget(TSharedPtr<FMVVMPropertyNode> Node, const FProperty* Prop);
	TSharedRef<SWidget> CreateFallbackWidget(TSharedPtr<FMVVMPropertyNode> Node);
	

	// Public getters for the rows to access search text for highlighting. 
	FText GetHierarchySearchText() const { return FText::FromString(HierarchyFilterString); }
	FText GetPropertySearchText() const { return FText::FromString(PropertyFilterString); }

private:
	// Removes FieldNotify listeners for all currently listened to objects.
	void ResetListenedObjects();
	
	// --- Hierarchy --- 
	void RecursivelyBuildHierarchy(TSharedPtr<SWidget> RootWidget, TSharedPtr<FMVVMHierarchyNode> ParentNode, TSet<UUserWidget*>& VisitedWidgets);
	TSharedRef<ITableRow> GenerateHierarchyRow(TSharedPtr<FMVVMHierarchyNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetHierarchyChildren(TSharedPtr<FMVVMHierarchyNode> Item, TArray<TSharedPtr<FMVVMHierarchyNode>>& OutChildren);
	void OnHierarchySelectionChanged(TSharedPtr<FMVVMHierarchyNode> Item, ESelectInfo::Type SelectInfo);
	void OnHierarchySearchChanged(const FText& InFilterText);
	void SetHierarchyExpansion(TSharedPtr<FMVVMHierarchyNode> Node, bool bExpand);

	// --- Properties --- 
	void RebuildPropertyTree(TSharedPtr<FMVVMHierarchyNode> SelectedNode);
	TArray<TSharedPtr<FMVVMPropertyNode>> GeneratePropertyNodes(uint8* BaseAddress, const UStruct* StructLayout, UObject* OwnerObject, TSharedPtr<FMVVMPropertyNode> Parent, int32 CurrentDepth = 0, int32 MaxDepth = 16);
	TSharedRef<ITableRow> GeneratePropertyRow(TSharedPtr<FMVVMPropertyNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetPropertyChildren(TSharedPtr<FMVVMPropertyNode> Item, TArray<TSharedPtr<FMVVMPropertyNode>>& OutChildren);
	void OnPropertySearchChanged(const FText& InFilterText);
	void SetPropertyExpansion(TSharedPtr<FMVVMPropertyNode> Node, bool bExpand);

	// --- Helpers --- 
	void SetupChangeListener(UObject* Object);
	void OnFieldChanged(UObject* Obj, UE::FieldNotification::FFieldId Id);
	FReply OnInvokeClicked(TSharedPtr<FMVVMPropertyNode> Node);

	static bool TryCallGetterSafe(TSharedPtr<FMVVMPropertyNode> Node, uint8* OutRawValue);
	static bool TryCallSetterSafe(TSharedPtr<FMVVMPropertyNode> Node, const uint8* InRawValue);

	// "Special Struct" refers to data types we can't easily display via checkbox/spinner/combo box, 
	// so we just show their text representation. Custom property display could be added later. 
	bool IsSpecialStruct(const UScriptStruct* Struct) const;
	FString GetSpecialStructValue(const UScriptStruct* Struct, const void* ValuePtr) const;
	void SetSpecialStructValue(TSharedPtr<FMVVMPropertyNode> Node, uint8* TargetPtr, const FString& NewStringValue);

private:
	// Left-hand hierarchy tree 
	TSharedPtr<STreeView<TSharedPtr<FMVVMHierarchyNode>>> HierarchyTreeView;
	TArray<TSharedPtr<FMVVMHierarchyNode>> HierarchyRootNodes;
	TSharedPtr<SSearchBox> HierarchySearchBox;
	FString HierarchyFilterString;

	// Right-hand property tree 
	TSharedPtr<STreeView<TSharedPtr<FMVVMPropertyNode>>> PropertyTreeView;
	TArray<TSharedPtr<FMVVMPropertyNode>> PropertyRootNodes;
	TSharedPtr<SSearchBox> PropertySearchBox;
	FString PropertyFilterString;

	// Objects we are currently listening to for FieldNotify changes.
	TSet<TWeakObjectPtr<UObject>> ListenedObjects;

	// Current hierarchy selection. 
	TWeakPtr<FMVVMHierarchyNode> CurrentSelection;
};

#endif // WITH_PLUGIN_MODELVIEWVIEWMODEL
