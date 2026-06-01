// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowLocAnalyzer.h"

// Internationalization
#include <Internationalization/TextKey.h>

// Slate
#include <Widgets/SWidget.h>

FLocStringData FInputFlowLocAnalyzer::AnalyzeText(const FText& InText)
{
	FLocStringData Out;
	Out.DisplayString = InText.ToString();

	if (InText.IsEmpty())
	{
		Out.Status = EInputFlowLocStatus::Empty;
		return Out;
	}

	// Pull the namespace + key. If both are present, the FText was created with a
	// localization key (NSLOCTEXT / LOCTEXT / from a string table). If neither, it
	// was created via FText::FromString or similar - we treat that as "hardcoded".
	const TOptional<FString> Namespace = FTextInspector::GetNamespace(InText);
	const TOptional<FString> Key = FTextInspector::GetKey(InText);

	if (const FString* SourceStr = FTextInspector::GetSourceString(InText))
	{
		Out.SourceString = *SourceStr;
	}
	else
	{
		Out.SourceString = Out.DisplayString;
	}

	if (InText.IsCultureInvariant())
	{
		Out.Status = EInputFlowLocStatus::CultureInvariant;
		return Out;
	}

	const bool bHasKey = Key.IsSet() && !Key.GetValue().IsEmpty();
	if (bHasKey)
	{
		Out.Status = EInputFlowLocStatus::Localized;
		Out.Namespace = Namespace.Get(FString());
		Out.Key = Key.GetValue();
	}
	else
	{
		Out.Status = EInputFlowLocStatus::Hardcoded;
	}

	return Out;
}