#include "HTMaterialVisibilityInput.h"

namespace HTMaterialVisibilityInput
{
	bool IsGroupCycle(const FString& RawText)
	{
		return RawText.Contains(TEXT(";")) || RawText.Contains(TEXT("\uFF1B"));
	}

	bool Parse(const FString& RawText, TArray<FHTMaterialVisibilityGroup>& OutGroups, FString& OutError)
	{
		OutGroups.Reset();
		OutError.Reset();
		FString Input = RawText.TrimStartAndEnd();
		Input.ReplaceInline(TEXT("\uFF0B"), TEXT("+"));
		Input.ReplaceInline(TEXT("\uFF0C"), TEXT(","));
		Input.ReplaceInline(TEXT("\u3001"), TEXT(","));
		Input.ReplaceInline(TEXT("\uFF1B"), TEXT(";"));
		if (Input.Contains(TEXT(";")) && Input.Contains(TEXT(",")))
		{
			OutError = TEXT("Use semicolons for a group-only cycle, or commas to include hide-all. Do not mix these separators.");
			return false;
		}
		Input.ReplaceInline(TEXT(";"), TEXT(","));
		if (Input.IsEmpty())
		{
			OutError = TEXT("Enter Material IDs. Group cycle: 5+6;8+10+12. Legacy cycle with hide-all: 13,20.");
			return false;
		}

		// Spaces around separators are formatting. Bare spaces between IDs retain
		// the legacy meaning of separate states; explicit empty groups stay invalid.
		FString Normalized;
		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			if (!FChar::IsWhitespace(Input[Index]))
			{
				Normalized.AppendChar(Input[Index]);
				continue;
			}
			while (Index + 1 < Input.Len() && FChar::IsWhitespace(Input[Index + 1]))
			{
				++Index;
			}
			if (!Normalized.IsEmpty() && Index + 1 < Input.Len())
			{
				const TCHAR Previous = Normalized[Normalized.Len() - 1];
				const TCHAR Next = Input[Index + 1];
				if (Previous != TEXT('+') && Previous != TEXT(',') && Next != TEXT('+') && Next != TEXT(','))
				{
					Normalized.AppendChar(TEXT(','));
				}
			}
		}

		TArray<FHTMaterialVisibilityGroup> ParsedGroups;
		TSet<int32> UsedMaterialIDs;
		TArray<FString> GroupParts;
		Normalized.ParseIntoArray(GroupParts, TEXT(","), false);
		for (int32 GroupIndex = 0; GroupIndex < GroupParts.Num(); ++GroupIndex)
		{
			if (GroupParts[GroupIndex].IsEmpty())
			{
				OutError = FString::Printf(TEXT("Material group %d is empty."), GroupIndex + 1);
				return false;
			}
			FHTMaterialVisibilityGroup& Group = ParsedGroups.AddDefaulted_GetRef();
			TArray<FString> MaterialParts;
			GroupParts[GroupIndex].ParseIntoArray(MaterialParts, TEXT("+"), false);
			for (const FString& MaterialPart : MaterialParts)
			{
				int32 MaterialID = INDEX_NONE;
				bool bOnlyDigits = !MaterialPart.IsEmpty();
				for (const TCHAR Character : MaterialPart)
				{
					bOnlyDigits &= Character >= TEXT('0') && Character <= TEXT('9');
				}
				if (!bOnlyDigits || !LexTryParseString(MaterialID, *MaterialPart) || MaterialID < 0 ||
					FCString::Strtoui64(*MaterialPart, nullptr, 10) > static_cast<uint64>(MAX_int32))
				{
					OutError = FString::Printf(TEXT("Invalid Material ID in group %d: %s"), GroupIndex + 1, *MaterialPart);
					return false;
				}
				if (UsedMaterialIDs.Contains(MaterialID))
				{
					OutError = FString::Printf(TEXT("Material ID %d appears more than once."), MaterialID);
					return false;
				}
				UsedMaterialIDs.Add(MaterialID);
				Group.MaterialIDs.Add(MaterialID);
			}
		}

		if (ParsedGroups.IsEmpty())
		{
			OutError = TEXT("Enter at least one valid Material ID.");
			return false;
		}
		OutGroups = MoveTemp(ParsedGroups);
		return true;
	}
}
