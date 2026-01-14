// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Slate
#include <Widgets/SCompoundWidget.h>

class UInputDebugSubsystem;
class SInputFlowLogView;
class SCommonUIHierarchyView;
class SEnhancedInputInspector;
class STextBlock;

/**
 * The main Editor Dock Tab widget.
 * Composes the Dashboard, Settings, Log, Hierarchy, and Inspector into a single layout.
 */
class SInputFlowAnalyzer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInputFlowAnalyzer) {}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

private:
	UInputDebugSubsystem* GetActiveSubsystem() const;
	bool IsSessionRunning() const;
	EVisibility GetOverlayVisibility() const;
	EVisibility GetContentVisibility() const;

	TSharedPtr<SInputFlowLogView> LogView;
	TSharedPtr<SCommonUIHierarchyView> HierarchyView;
	TSharedPtr<SEnhancedInputInspector> InspectorView;
};