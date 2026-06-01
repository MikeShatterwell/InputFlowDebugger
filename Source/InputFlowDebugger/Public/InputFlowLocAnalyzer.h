// Copyright Mike Desrosiers, All Rights Reserved.

#pragma once

// Core
#include <CoreMinimal.h>

// Internationalization
#include <Internationalization/Text.h>

#include "InputFlowLocAnalyzer.generated.h"

class SWidget;

/**
 * Classification of an FText's localization state.
 */
UENUM()
enum class EInputFlowLocStatus : uint8
{
	/** The text is empty. */
	Empty,

	/** The text has a namespace and key, it's properly localized. */
	Localized,

	/** The text is non-empty but has no localization key (e.g. FText::FromString). */
	Hardcoded,

	/** The text is explicitly marked as culture-invariant (e.g. numeric formatting). */
	CultureInvariant
};

/**
 * Cracked-open data for a single FText, suitable for inspection / display.
 */
struct INPUTFLOWDEBUGGER_API FLocStringData
{
	EInputFlowLocStatus Status = EInputFlowLocStatus::Empty;

	/** The localization namespace, if Status == Localized. */
	FString Namespace;

	/** The localization key, if Status == Localized. */
	FString Key;

	/** The original source string the FText was created with. */
	FString SourceString;

	/** What the FText currently resolves to in the active culture. */
	FString DisplayString;
};

/**
 * Static utility for inspecting localization state of FText and Slate text widgets.
 */
class INPUTFLOWDEBUGGER_API FInputFlowLocAnalyzer
{
public:
	/**
	 * Classify an FText and pull out its namespace, key, source string, and display string.
	 */
	static FLocStringData AnalyzeText(const FText& InText);
};