#pragma once

#include "HTBlueprintToggleGenerator.h"

namespace HTMaterialVisibilityInput
{
	// Semicolon input cycles only the specified groups; legacy input also hides all.
	bool IsGroupCycle(const FString& RawText);
	bool Parse(const FString& RawText, TArray<FHTMaterialVisibilityGroup>& OutGroups, FString& OutError);
}
