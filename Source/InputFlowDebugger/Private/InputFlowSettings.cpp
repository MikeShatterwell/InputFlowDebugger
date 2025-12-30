// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowSettings.h"

UInputFlowSettings::UInputFlowSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("InputFlowDebugger");
}

const UInputFlowSettings* UInputFlowSettings::Get()
{
	return GetDefault<UInputFlowSettings>();
}

#if WITH_EDITOR
void UInputFlowSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnSettingsChanged.Broadcast();
}
#endif
