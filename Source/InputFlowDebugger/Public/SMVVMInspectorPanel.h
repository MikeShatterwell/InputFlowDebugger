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
class UWidget;
class SSearchBox;
class UInputDebugSubsystem;
class FInputFlowLabelAPI;
class FInputFlowDrawAPI;
class FMVVMInspectorPickerInputProcessor;

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
namespace UE::UMGWidgetPreview { class IWidgetPreviewToolkit; }
enum class EWidgetPreviewWidgetChangeType : uint8;
#endif

/**
 * Widget picker mode
 */
enum class EMVVMPickingMode : uint8
{
	/** Not picking. */
	None,
	/** Pick widgets that participate in hit-testing (interactive widgets). */
	HitTestable,
	// TODO: Add more picking modes (e.g., painted widgets, etc).
};

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

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
		SLATE_ARGUMENT(TWeakPtr<UE::UMGWidgetPreview::IWidgetPreviewToolkit>, PreviewToolkit)
#endif
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMVVMInspectorPanel() override;

	/** Rebuilds the hierarchy view. */
	void RefreshHierarchy();

	/** Factory for property value widgets (right-hand column). */
	void NotifyPropertyValueChanged(TSharedPtr<FMVVMPropertyNode> Node);
	TSharedRef<SWidget> CreateValueWidget(TSharedPtr<FMVVMPropertyNode> Node);
	TSharedRef<SWidget> CreateBoolWidget(TSharedPtr<FMVVMPropertyNode> Node, const FBoolProperty* BoolProp, bool bCanEdit);
	TSharedRef<SWidget> CreateEnumWidget(TSharedPtr<FMVVMPropertyNode> Node, const FProperty* Prop, bool bCanEdit);
	TSharedRef<SWidget> CreateNumericWidget(TSharedPtr<FMVVMPropertyNode> Node, const FNumericProperty* IntProp, bool bCanEdit);
	TSharedRef<SWidget> CreateStringWidget(TSharedPtr<FMVVMPropertyNode> Node, bool bCanEdit);
	TSharedRef<SWidget> CreateSpecialStructWidget(TSharedPtr<FMVVMPropertyNode> Node, const FStructProperty* StructProp, bool bCanEdit);
	TSharedRef<SWidget> CreateContainerWidget(TSharedPtr<FMVVMPropertyNode> Node);
	TSharedRef<SWidget> CreateObjectLikeWidget(TSharedPtr<FMVVMPropertyNode> Node, const FProperty* Prop);
	TSharedRef<SWidget> CreateObjectPropertyWidget(TSharedPtr<FMVVMPropertyNode> Node, const FObjectProperty* ObjProp, bool bCanEdit);
	TSharedRef<SWidget> CreateFallbackWidget(TSharedPtr<FMVVMPropertyNode> Node);

	/** Wraps a value widget with an array-element "Remove" control. Used for rows that are TArray elements. */
	TSharedRef<SWidget> WrapWithArrayElementControls(TSharedRef<SWidget> InnerWidget, TSharedPtr<FMVVMPropertyNode> ElementNode);
	

	// --- Widget Picker (public so the IInputProcessor subclass can call in) ---
	/** True while the user is hovering the cursor around to pick a UMG widget. */
	bool IsPicking() const { return PickingMode != EMVVMPickingMode::None; }
	/** Called each tick by the input processor, re-evaluates the hovered widget from cursor position. */
	void UpdatePickingHover();
	/** Called by the input processor on left-click during picking. Selects the hovered UUserWidget. */
	void CommitPick();
	/** Called by the input processor on Esc / right-click. Aborts without selecting. */
	void CancelPick();

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

	// --- Structural Mutation ---
	// Append a default-initialized element to the array represented by ArrayNode.
	void AddArrayElement(TSharedPtr<FMVVMPropertyNode> ArrayNode);
	// Remove the element represented by ElementNode from its containing TArray.
	void RemoveArrayElement(TSharedPtr<FMVVMPropertyNode> ElementNode);
	// Set a strong UObject property to nullptr.
	void NullifyObjectProperty(TSharedPtr<FMVVMPropertyNode> ObjectNode);
	// Instantiate a new UObject of ClassToUse and assign it to the property represented by ObjectNode.
	void InstantiateObjectProperty(TSharedPtr<FMVVMPropertyNode> ObjectNode, const UClass* ClassToUse);

	// Shared edit-gating used by both value edits and structural mutations.
	bool CanModifyNode(TSharedPtr<FMVVMPropertyNode> Node) const;

	// True if Node represents a TArray element (ArrayIndex set, parent is an FArrayProperty node).
	// Map entries also carry ArrayIndex but are excluded here.
	static bool IsArrayElementNode(TSharedPtr<FMVVMPropertyNode> Node);

	// Collect non-abstract, non-deprecated subclasses of BaseClass (including BaseClass itself when concrete),
	// sorted by name for stable UI. Used to populate the instantiation class picker.
	static void GatherConcreteSubclasses(const UClass* BaseClass, TArray<UClass*>& OutClasses);

	// --- Tree Expansion Persistence ---
	// Produces a stable "/"-separated identifier for a node (property names + array indices, rooted at
	// the VM category). Two nodes in different rebuilds of the same tree that represent the same logical
	// property return the same path, which is what lets us carry expansion state across rebuilds.
	FString GetNodePath(TSharedPtr<FMVVMPropertyNode> Node) const;
	// Walks the current PropertyRootNodes and collects the paths of every expanded node.
	void CaptureExpandedNodePaths(TSet<FString>& OutPaths) const;
	// Re-expands any node in the rebuilt tree whose path is in Paths.
	void ApplyExpandedNodePaths(const TSet<FString>& Paths);
	// After a rebuild, finds the node matching Path, expands its ancestors so it's reachable, optionally
	// expands it, and scrolls it into view. No-ops if no match is found (e.g., node was removed).
	void FocusNodeByPath(const FString& Path, bool bExpand);
	// Common tail for structural mutations: force a tree rebuild (bypassing the FieldNotify requirement)
	// then focus the mutation site so the user doesn't lose their place.
	void RebuildAfterStructuralMutation(const FString& FocusPath, bool bExpand);

	static bool TryCallGetterSafe(TSharedPtr<FMVVMPropertyNode> Node, uint8* OutRawValue);
	static bool TryCallSetterSafe(TSharedPtr<FMVVMPropertyNode> Node, const uint8* InRawValue);

	// "Special Struct" refers to data types we can't easily display via checkbox/spinner/combo box, 
	// so we just show their text representation. Custom property display could be added later. 
	bool IsSpecialStruct(const UScriptStruct* Struct) const;
	FString GetSpecialStructValue(const UScriptStruct* Struct, const void* ValuePtr) const;
	void SetSpecialStructValue(TSharedPtr<FMVVMPropertyNode> Node, uint8* TargetPtr, const FString& NewStringValue);

	// --- Widget Picker (private implementation) ---
	/** Transitions to a picking mode - registers the input processor, binds overlay draw/label delegates. */
	void BeginPicking(EMVVMPickingMode NewMode);
	/** Leaves picking mode. Unregisters the processor and clears hover state. */
	void EndPicking();
	/** Returns true iff the given WidgetPath's tail passes through the game viewport (SGameLayerManager / SViewport). */
	static bool IsPathInGameViewport(const FWidgetPath& Path);
	/** Walks the widget path leaf-to-root, returning the owning UUserWidget of the deepest UMG widget hit. */
	static UUserWidget* ResolvePickedUserWidget(const FWidgetPath& WidgetPath);
	/** Selects the hierarchy tree node whose UserWidgetOwner matches Target; expands all its ancestors. */
	void SelectUserWidgetInHierarchy(const UUserWidget* Target);
	/** Called via UInputDebugSubsystem::OnDrawOverlay - draws the hover highlight while picking. */
	void OnPickerDrawOverlay(UInputDebugSubsystem* Subsystem, FInputFlowDrawAPI& DrawAPI);
	/** Called via UInputDebugSubsystem::OnGatherLabels - queues the floating "UMG → UserWidget" label. */
	void OnPickerGatherLabels(UInputDebugSubsystem* Subsystem, FInputFlowLabelAPI& LabelAPI);
	/** Toggles picking mode from the toolbar button. */
	FReply OnPickerButtonClicked();

private:
	// Left-hand hierarchy tree 
	TSharedPtr<STreeView<TSharedPtr<FMVVMHierarchyNode>>> HierarchyTreeView;
	TArray<TSharedPtr<FMVVMHierarchyNode>> HierarchyRootNodes;
	TSharedPtr<SSearchBox> HierarchySearchBox = nullptr;
	FString HierarchyFilterString;

	// Right-hand property tree 
	TSharedPtr<STreeView<TSharedPtr<FMVVMPropertyNode>>> PropertyTreeView;
	TArray<TSharedPtr<FMVVMPropertyNode>> PropertyRootNodes;
	TSharedPtr<SSearchBox> PropertySearchBox = nullptr;
	FString PropertyFilterString;

	// Objects we are currently listening to for FieldNotify changes.
	TSet<TWeakObjectPtr<UObject>> ListenedObjects;

	// Current hierarchy selection. 
	TWeakPtr<FMVVMHierarchyNode> CurrentSelection;

	// Selection + filter the property tree was last rebuilt for. Used by RebuildPropertyTree to decide
	// whether to preserve expansion state (same selection, same filter) or reset to filter-based defaults
	// (selection change, filter edit, initial build).
	TWeakPtr<FMVVMHierarchyNode> LastRebuiltSelection = nullptr;
	FString LastRebuiltFilterString;

	// --- Widget Picker state ---
	/** Current picking mode. None when inactive. */
	EMVVMPickingMode PickingMode = EMVVMPickingMode::None;
	/** The slate widget currently under the cursor while picking, if any. */
	TWeakPtr<SWidget> HoveredSlateWidget = nullptr;
	/** The UUserWidget resolved from the hovered slate widget, if any. */
	TWeakObjectPtr<UUserWidget> HoveredUserWidget = nullptr;
	/** The specific UMG widget that was directly clicked on (for the hover label display). */
	TWeakObjectPtr<UWidget> HoveredUMGWidget = nullptr;
	/** Registered IInputProcessor for the duration of picking. Shared so its lifetime is explicit. */
	TSharedPtr<FMVVMInspectorPickerInputProcessor> PickerProcessor = nullptr;
	/** Handles for the subsystem delegate bindings so we can cleanly unbind on EndPicking. */
	FDelegateHandle PickerDrawOverlayHandle;
	FDelegateHandle PickerGatherLabelsHandle;
	/** The subsystem we bound delegates to, cached so EndPicking can unbind even if resolution later fails. */
	TWeakObjectPtr<UInputDebugSubsystem> BoundPickerSubsystem = nullptr;

	/**
	 * Views into which we've injected mock ViewModels. Lets us skip redundant uninit/reinit cycles
	 * on refresh, and lets NotifyPropertyValueChanged re-run bindings only for mocked views (where
	 * FieldNotify-name alone isn't always enough to fire all bindings).
	 *
	 * Always compiled so the mocking path can be used in non-editor builds. Population is currently
	 * only driven by the editor-only preview toolkit hook below, but the plumbing is build-safe.
	 */
	TSet<TWeakObjectPtr<UMVVMView>> MockedViews;

#if WITH_EDITOR && WITH_PLUGIN_UMGWIDGETPREVIEW
	void OnPreviewWidgetChanged(EWidgetPreviewWidgetChangeType ChangeType);
	TWeakPtr<UE::UMGWidgetPreview::IWidgetPreviewToolkit> WeakPreviewToolkit;
	FDelegateHandle PreviewWidgetChangedHandle;
#endif
};

#endif // WITH_PLUGIN_MODELVIEWVIEWMODEL