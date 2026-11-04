// Blueprint generation for every toggle mode (visibility, texture, material, RGB tuner) and the toggle tree.

#include "HTBlueprintToggleGenerator.h"

#include "Algo/Unique.h"
#include "Animation/AnimInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Canvas.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MaterialTypes.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "HTBlueprintToggleGenerator"

namespace HTToggleGenerator
{
	static FString Trimmed(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT("\""), TEXT(""));
		return Value;
	}

	static FString NormalizeAssetPath(FString Path)
	{
		Path = Trimmed(Path);
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (Path.EndsWith(TEXT(".uasset")))
		{
			const int32 ContentIndex = Path.Find(TEXT("/Content/"), ESearchCase::IgnoreCase);
			if (ContentIndex != INDEX_NONE)
			{
				FString RelativePath = Path.Mid(ContentIndex + 9);
				RelativePath.LeftChopInline(7);
				return TEXT("/Game/") + RelativePath;
			}
		}

		if (Path.StartsWith(TEXT("Game/")))
		{
			Path = TEXT("/") + Path;
		}

		return Path;
	}

	static UBlueprint* LoadBlueprint(const FString& RawPath, FHTBlueprintToggleGeneratorResult& Result, const FString& Label)
	{
		const FString Path = NormalizeAssetPath(RawPath);
		UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);

		if (!Object && Path.StartsWith(TEXT("/Game/")) && !Path.Contains(TEXT(".")))
		{
			const FString ObjectPath = Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
			Object = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(Object);
		if (!Blueprint)
		{
			Result.Errors.Add(FString::Printf(TEXT("%s could not be loaded: %s"), *Label, *Path));
		}
		return Blueprint;
	}

	static UTexture* LoadTexture(const FString& RawPath, FHTBlueprintToggleGeneratorResult& Result, const FString& Label)
	{
		const FString Path = NormalizeAssetPath(RawPath);
		UObject* Object = StaticLoadObject(UTexture::StaticClass(), nullptr, *Path);

		if (!Object && Path.StartsWith(TEXT("/Game/")) && !Path.Contains(TEXT(".")))
		{
			const FString ObjectPath = Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
			Object = StaticLoadObject(UTexture::StaticClass(), nullptr, *ObjectPath);
		}

		UTexture* Texture = Cast<UTexture>(Object);
		if (!Texture)
		{
			Result.Errors.Add(FString::Printf(TEXT("%s could not be loaded: %s"), *Label, *Path));
		}
		return Texture;
	}

	static UMaterialInterface* LoadMaterial(const FString& RawPath, FHTBlueprintToggleGeneratorResult& Result, const FString Label = TEXT("Source Material"))
	{
		const FString Path = NormalizeAssetPath(RawPath);
		UObject* Object = StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *Path);

		if (!Object && Path.StartsWith(TEXT("/Game/")) && !Path.Contains(TEXT(".")))
		{
			const FString ObjectPath = Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
			Object = StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ObjectPath);
		}

		UMaterialInterface* Material = Cast<UMaterialInterface>(Object);
		if (!Material)
		{
			Result.Errors.Add(FString::Printf(TEXT("%s could not be loaded: %s"), *Label, *Path));
		}
		return Material;
	}

	static FName ToName(const FString& RawValue, const TCHAR* Fallback)
	{
		FString Value = Trimmed(RawValue);
		if (Value.IsEmpty())
		{
			Value = Fallback;
		}

		FString Sanitized;
		bool bUpperNext = false;
		for (const TCHAR Character : Value)
		{
			if (FChar::IsWhitespace(Character) || Character == TCHAR('-'))
			{
				bUpperNext = Sanitized.Len() > 0;
				continue;
			}

			if (Sanitized.IsEmpty() && FChar::IsDigit(Character))
			{
				Sanitized.AppendChar(TEXT('_'));
			}

			Sanitized.AppendChar(bUpperNext ? FChar::ToUpper(Character) : Character);
			bUpperNext = false;
		}

		if (Sanitized.IsEmpty())
		{
			Sanitized = Fallback;
		}

		return FName(*Sanitized);
	}

	enum class EInputKeyModifier
	{
		None,
		Control,
		Shift,
		Alt
	};

	struct FInputKeySpec
	{
		FKey MainKey;
		EInputKeyModifier Modifier = EInputKeyModifier::None;
		FString MainKeyName;

		bool HasModifier() const
		{
			return Modifier != EInputKeyModifier::None;
		}
	};

	static FKey ResolveKey(FString KeyName)
	{
		KeyName = Trimmed(KeyName);

		if (KeyName.Len() == 1)
		{
			const TCHAR Character = KeyName[0];
			if ((Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('A') && Character <= TEXT('Z')))
			{
				KeyName.ToUpperInline();
				return FKey(FName(*KeyName));
			}
		}

		if (KeyName == TEXT("0")) return EKeys::Zero;
		if (KeyName == TEXT("1")) return EKeys::One;
		if (KeyName == TEXT("2")) return EKeys::Two;
		if (KeyName == TEXT("3")) return EKeys::Three;
		if (KeyName == TEXT("4")) return EKeys::Four;
		if (KeyName == TEXT("5")) return EKeys::Five;
		if (KeyName == TEXT("6")) return EKeys::Six;
		if (KeyName == TEXT("7")) return EKeys::Seven;
		if (KeyName == TEXT("8")) return EKeys::Eight;
		if (KeyName == TEXT("9")) return EKeys::Nine;

		struct FKeyAlias
		{
			const TCHAR* Alias;
			FKey Key;
		};

		// Besides the English names, full-width punctuation and Chinese key names are accepted, so
		// keys typed with a Chinese input method still resolve.
		const FKeyAlias Aliases[] = {
			{ TEXT("="), EKeys::Equals },
			{ TEXT("\uFF1D"), EKeys::Equals },
			{ TEXT("+"), EKeys::Equals },
			{ TEXT("\uFF0B"), EKeys::Equals },
			{ TEXT("Equals"), EKeys::Equals },
			{ TEXT("Equal"), EKeys::Equals },
			{ TEXT("\u7B49\u53F7"), EKeys::Equals },
			{ TEXT(";"), EKeys::Semicolon },
			{ TEXT("\uFF1B"), EKeys::Semicolon },
			{ TEXT("Semicolon"), EKeys::Semicolon },
			{ TEXT("SemiColon"), EKeys::Semicolon },
			{ TEXT("\u5206\u53F7"), EKeys::Semicolon },
			{ TEXT(","), EKeys::Comma },
			{ TEXT("\uFF0C"), EKeys::Comma },
			{ TEXT("Comma"), EKeys::Comma },
			{ TEXT("\u9017\u53F7"), EKeys::Comma },
			{ TEXT("-"), EKeys::Hyphen },
			{ TEXT("\uFF0D"), EKeys::Hyphen },
			{ TEXT("Hyphen"), EKeys::Hyphen },
			{ TEXT("Minus"), EKeys::Hyphen },
			{ TEXT("\u51CF\u53F7"), EKeys::Hyphen },
			{ TEXT("_"), EKeys::Underscore },
			{ TEXT("\uFF3F"), EKeys::Underscore },
			{ TEXT("Underscore"), EKeys::Underscore },
			{ TEXT("\u4E0B\u5212\u7EBF"), EKeys::Underscore },
			{ TEXT("."), EKeys::Period },
			{ TEXT("\u3002"), EKeys::Period },
			{ TEXT("Period"), EKeys::Period },
			{ TEXT("Dot"), EKeys::Period },
			{ TEXT("\u53E5\u53F7"), EKeys::Period },
			{ TEXT("\u70B9"), EKeys::Period },
			{ TEXT("/"), EKeys::Slash },
			{ TEXT("\uFF0F"), EKeys::Slash },
			{ TEXT("?"), EKeys::Slash },
			{ TEXT("\uFF1F"), EKeys::Slash },
			{ TEXT("Slash"), EKeys::Slash },
			{ TEXT("\u659C\u6760"), EKeys::Slash },
			{ TEXT("\\"), EKeys::Backslash },
			{ TEXT("\uFF3C"), EKeys::Backslash },
			{ TEXT("|"), EKeys::Backslash },
			{ TEXT("\uFF5C"), EKeys::Backslash },
			{ TEXT("Backslash"), EKeys::Backslash },
			{ TEXT("\u53CD\u659C\u6760"), EKeys::Backslash },
			{ TEXT("["), EKeys::LeftBracket },
			{ TEXT("\uFF3B"), EKeys::LeftBracket },
			{ TEXT("LeftBracket"), EKeys::LeftBracket },
			{ TEXT("Left Bracket"), EKeys::LeftBracket },
			{ TEXT("\u5DE6\u4E2D\u62EC\u53F7"), EKeys::LeftBracket },
			{ TEXT("\u5DE6\u65B9\u62EC\u53F7"), EKeys::LeftBracket },
			{ TEXT("]"), EKeys::RightBracket },
			{ TEXT("\uFF3D"), EKeys::RightBracket },
			{ TEXT("RightBracket"), EKeys::RightBracket },
			{ TEXT("Right Bracket"), EKeys::RightBracket },
			{ TEXT("\u53F3\u4E2D\u62EC\u53F7"), EKeys::RightBracket },
			{ TEXT("\u53F3\u65B9\u62EC\u53F7"), EKeys::RightBracket },
			{ TEXT("'"), EKeys::Apostrophe },
			{ TEXT("\u2018"), EKeys::Apostrophe },
			{ TEXT("\u2019"), EKeys::Apostrophe },
			{ TEXT("Apostrophe"), EKeys::Apostrophe },
			{ TEXT("\u5355\u5F15\u53F7"), EKeys::Apostrophe },
			{ TEXT("\""), EKeys::Quote },
			{ TEXT("\u201C"), EKeys::Quote },
			{ TEXT("\u201D"), EKeys::Quote },
			{ TEXT("Quote"), EKeys::Quote },
			{ TEXT("DoubleQuote"), EKeys::Quote },
			{ TEXT("\u53CC\u5F15\u53F7"), EKeys::Quote },
			{ TEXT("`"), EKeys::Tilde },
			{ TEXT("\uFF40"), EKeys::Tilde },
			{ TEXT("~"), EKeys::Tilde },
			{ TEXT("\uFF5E"), EKeys::Tilde },
			{ TEXT("Tilde"), EKeys::Tilde },
			{ TEXT("BackQuote"), EKeys::Tilde },
			{ TEXT("\u53CD\u5F15\u53F7"), EKeys::Tilde },
			{ TEXT("\u6CE2\u6D6A\u53F7"), EKeys::Tilde },
			{ TEXT("("), EKeys::LeftParantheses },
			{ TEXT("\uFF08"), EKeys::LeftParantheses },
			{ TEXT("LeftParantheses"), EKeys::LeftParantheses },
			{ TEXT("LeftParenthesis"), EKeys::LeftParantheses },
			{ TEXT("\u5DE6\u62EC\u53F7"), EKeys::LeftParantheses },
			{ TEXT(")"), EKeys::RightParantheses },
			{ TEXT("\uFF09"), EKeys::RightParantheses },
			{ TEXT("RightParantheses"), EKeys::RightParantheses },
			{ TEXT("RightParenthesis"), EKeys::RightParantheses },
			{ TEXT("\u53F3\u62EC\u53F7"), EKeys::RightParantheses },
			{ TEXT("&"), EKeys::Ampersand },
			{ TEXT("Ampersand"), EKeys::Ampersand },
			{ TEXT("*"), EKeys::Asterix },
			{ TEXT("\uFF0A"), EKeys::Asterix },
			{ TEXT("Asterix"), EKeys::Asterix },
			{ TEXT("Asterisk"), EKeys::Asterix },
			{ TEXT("^"), EKeys::Caret },
			{ TEXT("\uFF3E"), EKeys::Caret },
			{ TEXT("Caret"), EKeys::Caret },
			{ TEXT("$"), EKeys::Dollar },
			{ TEXT("\uFF04"), EKeys::Dollar },
			{ TEXT("Dollar"), EKeys::Dollar },
			{ TEXT("!"), EKeys::Exclamation },
			{ TEXT("\uFF01"), EKeys::Exclamation },
			{ TEXT("Exclamation"), EKeys::Exclamation },
			{ TEXT(":"), EKeys::Colon },
			{ TEXT("\uFF1A"), EKeys::Colon },
			{ TEXT("Colon"), EKeys::Colon },
			{ TEXT("Space"), EKeys::SpaceBar },
			{ TEXT("SpaceBar"), EKeys::SpaceBar },
			{ TEXT("\u7A7A\u683C"), EKeys::SpaceBar },
			{ TEXT("Esc"), EKeys::Escape },
			{ TEXT("Escape"), EKeys::Escape },
			{ TEXT("Enter"), EKeys::Enter },
			{ TEXT("Return"), EKeys::Enter },
			{ TEXT("Tab"), EKeys::Tab },
			{ TEXT("Backspace"), EKeys::BackSpace },
			{ TEXT("BackSpace"), EKeys::BackSpace },
			{ TEXT("Delete"), EKeys::Delete },
			{ TEXT("Del"), EKeys::Delete },
			{ TEXT("Insert"), EKeys::Insert },
			{ TEXT("Ins"), EKeys::Insert },
			{ TEXT("Left"), EKeys::Left },
			{ TEXT("Right"), EKeys::Right },
			{ TEXT("Up"), EKeys::Up },
			{ TEXT("Down"), EKeys::Down },
			{ TEXT("Num+"), EKeys::Add },
			{ TEXT("NumAdd"), EKeys::Add },
			{ TEXT("Numpad+"), EKeys::Add },
			{ TEXT("Add"), EKeys::Add },
			{ TEXT("Num-"), EKeys::Subtract },
			{ TEXT("NumSubtract"), EKeys::Subtract },
			{ TEXT("Numpad-"), EKeys::Subtract },
			{ TEXT("Subtract"), EKeys::Subtract }
		};

		for (const FKeyAlias& Alias : Aliases)
		{
			if (KeyName.Equals(Alias.Alias, ESearchCase::IgnoreCase))
			{
				return Alias.Key;
			}
		}

		const FKey NamedKey = FKey(FName(*KeyName));
		if (NamedKey.IsValid())
		{
			return NamedKey;
		}

		for (const FKeyAlias& Alias : Aliases)
		{
			FString NormalizedInput = KeyName;
			FString NormalizedAlias = Alias.Alias;
			NormalizedInput.ReplaceInline(TEXT(" "), TEXT(""));
			NormalizedInput.ReplaceInline(TEXT("_"), TEXT(""));
			NormalizedAlias.ReplaceInline(TEXT(" "), TEXT(""));
			NormalizedAlias.ReplaceInline(TEXT("_"), TEXT(""));
			if (NormalizedInput.Equals(NormalizedAlias, ESearchCase::IgnoreCase))
			{
				return Alias.Key;
			}
		}

		return NamedKey;
	}

	static FString NormalizedModifierToken(FString Token)
	{
		Token = Trimmed(Token);
		Token.ToLowerInline();
		Token.ReplaceInline(TEXT("_"), TEXT(""));
		Token.ReplaceInline(TEXT("-"), TEXT(""));
		Token.ReplaceInline(TEXT("+"), TEXT(""));
		return Token;
	}

	static bool TryParseModifierToken(const FString& Token, EInputKeyModifier& OutModifier)
	{
		const FString Normalized = NormalizedModifierToken(Token);
		if (Normalized == TEXT("ctrl") || Normalized == TEXT("control") || Normalized == TEXT("lctrl") || Normalized == TEXT("rctrl") || Normalized == TEXT("leftctrl") || Normalized == TEXT("rightctrl"))
		{
			OutModifier = EInputKeyModifier::Control;
			return true;
		}

		if (Normalized == TEXT("shift") || Normalized == TEXT("shit") || Normalized == TEXT("lshift") || Normalized == TEXT("rshift") || Normalized == TEXT("leftshift") || Normalized == TEXT("rightshift"))
		{
			OutModifier = EInputKeyModifier::Shift;
			return true;
		}

		if (Normalized == TEXT("alt") || Normalized == TEXT("lalt") || Normalized == TEXT("ralt") || Normalized == TEXT("leftalt") || Normalized == TEXT("rightalt"))
		{
			OutModifier = EInputKeyModifier::Alt;
			return true;
		}

		return false;
	}

	static bool ResolveInputKeySpec(const FString& RawKeyName, FInputKeySpec& OutSpec, FString& OutError)
	{
		FString KeyName = Trimmed(RawKeyName);
		KeyName.ReplaceInline(TEXT("\t"), TEXT(" "));
		KeyName.ReplaceInline(TEXT("\r"), TEXT(" "));
		KeyName.ReplaceInline(TEXT("\n"), TEXT(" "));
		// Ideographic space and full-width plus from a Chinese input method.
		KeyName.ReplaceInline(TEXT("\u3000"), TEXT(" "));
		KeyName.ReplaceInline(TEXT("\uFF0B"), TEXT("+"));

		FString MainKeyName = KeyName;
		EInputKeyModifier Modifier = EInputKeyModifier::None;

		TArray<FString> Tokens;
		KeyName.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() >= 2 && TryParseModifierToken(Tokens[0], Modifier))
		{
			TArray<FString> MainKeyTokens;
			for (int32 Index = 1; Index < Tokens.Num(); ++Index)
			{
				MainKeyTokens.Add(Tokens[Index]);
			}
			MainKeyName = FString::Join(MainKeyTokens, TEXT(" "));
		}
		else
		{
			FString LeftPart;
			FString RightPart;
			if (KeyName.Split(TEXT("+"), &LeftPart, &RightPart) && TryParseModifierToken(LeftPart, Modifier))
			{
				MainKeyName = RightPart;
			}
		}

		MainKeyName = Trimmed(MainKeyName);
		if (MainKeyName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Key not recognized: %s. Combo format examples: ctrl 6, shift 6, alt 6."), *RawKeyName);
			return false;
		}

		const FKey MainKey = ResolveKey(MainKeyName);
		if (!MainKey.IsValid())
		{
			OutError = FString::Printf(TEXT("Key not recognized: %s. Try keys like 9, =, [, ], ;, ctrl 6, shift 6, alt 6."), *RawKeyName);
			return false;
		}

		OutSpec.MainKey = MainKey;
		OutSpec.Modifier = Modifier;
		OutSpec.MainKeyName = MainKeyName;
		return true;
	}

	static void GetModifierKeys(const EInputKeyModifier Modifier, FKey& OutLeftKey, FKey& OutRightKey)
	{
		if (Modifier == EInputKeyModifier::Control)
		{
			OutLeftKey = EKeys::LeftControl;
			OutRightKey = EKeys::RightControl;
			return;
		}

		if (Modifier == EInputKeyModifier::Shift)
		{
			OutLeftKey = EKeys::LeftShift;
			OutRightKey = EKeys::RightShift;
			return;
		}

		if (Modifier == EInputKeyModifier::Alt)
		{
			OutLeftKey = EKeys::LeftAlt;
			OutRightKey = EKeys::RightAlt;
			return;
		}

		OutLeftKey = FKey();
		OutRightKey = FKey();
	}

	static bool HasVariable(UBlueprint* Blueprint, const FName VariableName)
	{
		if (!Blueprint)
		{
			return false;
		}

		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE)
		{
			return true;
		}

		return (Blueprint->SkeletonGeneratedClass && FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VariableName)) ||
			(Blueprint->GeneratedClass && FindFProperty<FProperty>(Blueprint->GeneratedClass, VariableName));
	}

	static bool EnsureIntVariable(UBlueprint* Blueprint, const FName VariableName, const TOptional<int32> InitialValue, FHTBlueprintToggleGeneratorResult& Result, const FString& Label)
	{
		const FString DefaultValue = FString::FromInt(InitialValue.Get(0));
		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
		if (VariableIndex != INDEX_NONE)
		{
			FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
			if (Variable.VarType.PinCategory != UEdGraphSchema_K2::PC_Int)
			{
				Result.Errors.Add(FString::Printf(TEXT("%s variable is not an int: %s"), *Label, *VariableName.ToString()));
				return false;
			}

			if (InitialValue.IsSet() && Variable.DefaultValue != DefaultValue)
			{
				Blueprint->Modify();
				Variable.DefaultValue = DefaultValue;
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				Result.Messages.Add(FString::Printf(TEXT("%s variable default updated: %s = %d"), *Label, *VariableName.ToString(), InitialValue.GetValue()));
			}
			else
			{
				Result.Messages.Add(FString::Printf(TEXT("%s variable already exists: %s"), *Label, *VariableName.ToString()));
			}
			return true;
		}

		if (HasVariable(Blueprint, VariableName))
		{
			Result.Messages.Add(FString::Printf(TEXT("%s variable already exists: %s"), *Label, *VariableName.ToString()));
			return true;
		}

		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;

		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType, DefaultValue))
		{
			Result.Errors.Add(FString::Printf(TEXT("%s could not add variable: %s"), *Label, *VariableName.ToString()));
			return false;
		}

		Result.Messages.Add(FString::Printf(TEXT("%s added int variable: %s"), *Label, *VariableName.ToString()));
		return true;
	}

	static bool EnsureObjectVariable(UBlueprint* Blueprint, const FName VariableName, UClass* ObjectClass, FHTBlueprintToggleGeneratorResult& Result, const FString& Label)
	{
		if (!Blueprint || !ObjectClass)
		{
			return false;
		}

		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
		if (VariableIndex != INDEX_NONE)
		{
			const FEdGraphPinType& ExistingType = Blueprint->NewVariables[VariableIndex].VarType;
			if (ExistingType.PinCategory != UEdGraphSchema_K2::PC_Object || ExistingType.PinSubCategoryObject.Get() != ObjectClass)
			{
				Result.Errors.Add(FString::Printf(TEXT("%s variable has the wrong type: %s"), *Label, *VariableName.ToString()));
				return false;
			}
			Result.Messages.Add(FString::Printf(TEXT("%s variable already exists: %s"), *Label, *VariableName.ToString()));
			return true;
		}

		if (FObjectPropertyBase* ExistingProperty = Blueprint->SkeletonGeneratedClass ? FindFProperty<FObjectPropertyBase>(Blueprint->SkeletonGeneratedClass, VariableName) : nullptr)
		{
			if (!ExistingProperty->PropertyClass || !ExistingProperty->PropertyClass->IsChildOf(ObjectClass))
			{
				Result.Errors.Add(FString::Printf(TEXT("%s variable has the wrong type: %s"), *Label, *VariableName.ToString()));
				return false;
			}
			return true;
		}

		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = ObjectClass;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to add %s variable: %s"), *Label, *VariableName.ToString()));
			return false;
		}

		Result.Messages.Add(FString::Printf(TEXT("Added %s variable: %s"), *Label, *VariableName.ToString()));
		return true;
	}

	static bool EnsureStructVariable(
		UBlueprint* Blueprint,
		const FName VariableName,
		UScriptStruct* StructType,
		const FString& DefaultValue,
		FHTBlueprintToggleGeneratorResult& Result,
		const FString& Label)
	{
		if (!Blueprint || !StructType)
		{
			return false;
		}

		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
		if (VariableIndex != INDEX_NONE)
		{
			FBPVariableDescription& Existing = Blueprint->NewVariables[VariableIndex];
			if (Existing.VarType.PinCategory != UEdGraphSchema_K2::PC_Struct || Existing.VarType.PinSubCategoryObject.Get() != StructType)
			{
				Result.Errors.Add(FString::Printf(TEXT("%s variable has the wrong type: %s"), *Label, *VariableName.ToString()));
				return false;
			}
			if (!DefaultValue.IsEmpty() && Existing.DefaultValue.IsEmpty())
			{
				Existing.DefaultValue = DefaultValue;
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
			return true;
		}

		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = StructType;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType, DefaultValue))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to add %s variable: %s"), *Label, *VariableName.ToString()));
			return false;
		}
		Result.Messages.Add(FString::Printf(TEXT("Added %s variable: %s"), *Label, *VariableName.ToString()));
		return true;
	}

	static const UEdGraphSchema_K2* GetSchema()
	{
		return GetDefault<UEdGraphSchema_K2>();
	}

	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FName PinName, const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName == PinName && Pin->Direction == Direction)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	static UEdGraphPin* FindAnyPin(UEdGraphNode* Node, const FName PinName)
	{
		return Node ? Node->FindPin(PinName) : nullptr;
	}

	static UEdGraphPin* FindSelfPin(UEdGraphNode* Node)
	{
		if (UEdGraphPin* Pin = FindAnyPin(Node, UEdGraphSchema_K2::PN_Self))
		{
			return Pin;
		}
		return FindAnyPin(Node, TEXT("Target"));
	}

	static UEdGraphPin* FindSetOutputPin(UK2Node_VariableSet* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		if (UEdGraphPin* Pin = FindAnyPin(Node, TEXT("Output_Get")))
		{
			return Pin;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	static bool Connect(UEdGraphPin* A, UEdGraphPin* B, FHTBlueprintToggleGeneratorResult& Result, const FString& Label)
	{
		if (!A || !B)
		{
			Result.Errors.Add(FString::Printf(TEXT("Connection failed, missing pin: %s"), *Label));
			return false;
		}

		if (A->LinkedTo.Contains(B))
		{
			return true;
		}

		if (!GetSchema()->TryCreateConnection(A, B))
		{
			Result.Errors.Add(FString::Printf(TEXT("Connection failed: %s (%s -> %s)"), *Label, *A->PinName.ToString(), *B->PinName.ToString()));
			return false;
		}
		return true;
	}

	static int32 SnapY(const int32 Value)
	{
		constexpr int32 Grid = 400;
		return ((Value + Grid - 1) / Grid) * Grid;
	}

	static int32 ApproxNodeHeight(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return 0;
		}

		if (Node->NodeHeight > 0)
		{
			return Node->NodeHeight;
		}

		return Node->IsA<UEdGraphNode_Comment>() ? 1200 : 240;
	}

	static int32 FindNextLayoutBaseY(UEdGraph* Graph)
	{
		constexpr int32 DefaultBaseY = 300;
		if (!Graph)
		{
			return DefaultBaseY;
		}

		int32 MaxBottom = MIN_int32;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			MaxBottom = FMath::Max(MaxBottom, Node->NodePosY + ApproxNodeHeight(Node));
		}

		if (MaxBottom == MIN_int32 || MaxBottom < DefaultBaseY + 900)
		{
			return DefaultBaseY;
		}

		return FMath::Max(DefaultBaseY, SnapY(MaxBottom + 700));
	}

	static UEdGraphNode_Comment* SpawnCommentBox(
		UEdGraph* Graph,
		const FString& Label,
		const FVector2D Position,
		const FVector2D Size,
		const FLinearColor Color)
	{
		if (!Graph)
		{
			return nullptr;
		}

		UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();
		UEdGraphNode_Comment* Comment = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(Graph, CommentTemplate, Position, false);
		if (!Comment)
		{
			return nullptr;
		}

		Comment->NodeComment = Label;
		Comment->NodeWidth = FMath::RoundToInt(Size.X);
		Comment->NodeHeight = FMath::RoundToInt(Size.Y);
		Comment->CommentColor = Color;
		Comment->FontSize = 18;
		Comment->MoveMode = ECommentBoxMode::GroupMovement;
		Comment->CommentDepth = -1;
		Comment->bCommentBubbleVisible = true;
		Comment->bCommentBubbleVisible_InDetailsPanel = true;
		return Comment;
	}

	static void AddNodesToComment(UEdGraphNode_Comment* Comment, const TArray<UEdGraphNode*>& Nodes)
	{
		if (!Comment)
		{
			return;
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Comment->AddNodeUnderComment(Node);
			}
		}
	}

	static TArray<UEdGraphNode*> GetNodesUnderComment(UEdGraphNode_Comment* Comment)
	{
		TArray<UEdGraphNode*> Nodes;
		if (!Comment)
		{
			return Nodes;
		}
		for (UObject* Object : Comment->GetNodesUnderComment())
		{
			if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
			{
				Nodes.AddUnique(Node);
			}
		}
		return Nodes;
	}

	static void RemoveTextureTunerGroup(UEdGraph* Graph, const FName TunerName)
	{
		if (!Graph)
		{
			return;
		}
		const FString Prefix = FString::Printf(TEXT("HT Texture Tuner - %s;"), *TunerName.ToString());
		TArray<UEdGraphNode_Comment*> Comments;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node); Comment && Comment->NodeComment.StartsWith(Prefix))
			{
				Comments.Add(Comment);
			}
		}
		for (UEdGraphNode_Comment* Comment : Comments)
		{
			TArray<UEdGraphNode*> Members = GetNodesUnderComment(Comment);
			// Comment membership is editor-only and is not serialized. Recover the
			// generated group from its saved bounds when reopening an AnimBP.
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Node->IsA<UEdGraphNode_Comment>()
					|| (Node->IsA<UK2Node_Event>() && !Node->IsA<UK2Node_CustomEvent>())
					|| Node->IsA<UK2Node_ExecutionSequence>())
				{
					continue; // Shared animation entry/dispatch nodes are not tuner-owned.
				}
				if (Node->NodePosX >= Comment->NodePosX && Node->NodePosX <= Comment->NodePosX + Comment->NodeWidth
					&& Node->NodePosY >= Comment->NodePosY && Node->NodePosY <= Comment->NodePosY + Comment->NodeHeight)
				{
					Members.AddUnique(Node);
				}
			}
			for (UEdGraphNode* Node : Members)
			{
				Node->BreakAllNodeLinks();
				Graph->RemoveNode(Node);
			}
			Graph->RemoveNode(Comment);
		}
	}

	static void SetDefaultValue(UEdGraphPin* Pin, const FString& Value)
	{
		if (Pin)
		{
			GetSchema()->TrySetDefaultValue(*Pin, Value);
		}
	}

	static void SetDefaultObject(UEdGraphPin* Pin, UObject* Value)
	{
		if (Pin)
		{
			GetSchema()->TrySetDefaultObject(*Pin, Value);
		}
	}

	static UK2Node_CallFunction* SpawnCall(UEdGraph* Graph, UClass* OwnerClass, const FName FunctionName, const FVector2D Position, FHTBlueprintToggleGeneratorResult& Result)
	{
		UFunction* Function = OwnerClass ? OwnerClass->FindFunctionByName(FunctionName) : nullptr;
		if (!Function)
		{
			Result.Errors.Add(FString::Printf(TEXT("Function not found: %s.%s"), OwnerClass ? *OwnerClass->GetName() : TEXT("None"), *FunctionName.ToString()));
			return nullptr;
		}

		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[Function](UK2Node_CallFunction* NewNode)
			{
				NewNode->SetFromFunction(Function);
			});
	}

	static UK2Node_CustomEvent* SpawnCustomEvent(UEdGraph* Graph, const FName EventName, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[EventName](UK2Node_CustomEvent* Node)
			{
				Node->CustomFunctionName = EventName;
			});
	}

	static UK2Node_CommutativeAssociativeBinaryOperator* SpawnBooleanOperator(UEdGraph* Graph, const FName FunctionName, const FVector2D Position, const int32 InputCount, FHTBlueprintToggleGeneratorResult& Result)
	{
		UFunction* Function = UKismetMathLibrary::StaticClass()->FindFunctionByName(FunctionName);
		if (!Function)
		{
			Result.Errors.Add(FString::Printf(TEXT("Function not found: UKismetMathLibrary.%s"), *FunctionName.ToString()));
			return nullptr;
		}

		const int32 AdditionalInputs = FMath::Max(0, InputCount - 2);
		UK2Node_CommutativeAssociativeBinaryOperator* Node = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CommutativeAssociativeBinaryOperator>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[Function, AdditionalInputs](UK2Node_CommutativeAssociativeBinaryOperator* NewNode)
			{
				NewNode->SetFromFunction(Function);
				NewNode->NumAdditionalInputs = AdditionalInputs;
			});

		while (Node && Node->GetNumberOfAdditionalInputs() < AdditionalInputs)
		{
			Node->AddInputPin();
		}

		return Node;
	}

	static UK2Node_VariableGet* SpawnVariableGet(UEdGraph* Graph, const FName VariableName, UClass* SourceClass, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[VariableName, SourceClass](UK2Node_VariableGet* NewNode)
			{
				if (SourceClass)
				{
					NewNode->VariableReference.SetExternalMember(VariableName, SourceClass);
				}
				else
				{
					NewNode->VariableReference.SetSelfMember(VariableName);
				}
			});
	}

	static UK2Node_VariableSet* SpawnVariableSet(UEdGraph* Graph, const FName VariableName, UClass* SourceClass, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[VariableName, SourceClass](UK2Node_VariableSet* NewNode)
			{
				if (SourceClass)
				{
					NewNode->VariableReference.SetExternalMember(VariableName, SourceClass);
				}
				else
				{
					NewNode->VariableReference.SetSelfMember(VariableName);
				}
			});
	}

	static UK2Node_IfThenElse* SpawnBranch(UEdGraph* Graph, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_IfThenElse>(Graph, Position, EK2NewNodeFlags::None);
	}

	static UK2Node_SwitchInteger* SpawnSwitchOnInt(UEdGraph* Graph, const FVector2D Position, const int32 CaseCount = 2)
	{
		UK2Node_SwitchInteger* SwitchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SwitchInteger>(Graph, Position, EK2NewNodeFlags::None);
		if (SwitchNode)
		{
			SwitchNode->StartIndex = 0;
			const int32 SafeCaseCount = FMath::Max(2, CaseCount);
			for (int32 CaseIndex = 0; CaseIndex < SafeCaseCount; ++CaseIndex)
			{
				if (!FindPin(SwitchNode, FName(*FString::FromInt(CaseIndex)), EGPD_Output))
				{
					SwitchNode->AddPinToSwitchNode();
				}
			}
		}
		return SwitchNode;
	}

	static UK2Node_DynamicCast* SpawnCast(UEdGraph* Graph, UClass* TargetClass, const FVector2D Position)
	{
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_DynamicCast>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[TargetClass](UK2Node_DynamicCast* NewNode)
			{
				NewNode->TargetType = TargetClass;
			});
	}

	static UK2Node_Event* FindEventNode(UEdGraph* Graph, const FName EventName)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (EventNode && EventNode->GetFunctionName() == EventName)
			{
				return EventNode;
			}
		}

		return nullptr;
	}

	static UK2Node_Event* FindOrCreateAnimEvent(UEdGraph* Graph, const FName EventName, const FVector2D Position, FHTBlueprintToggleGeneratorResult& Result)
	{
		if (UK2Node_Event* ExistingEvent = FindEventNode(Graph, EventName))
		{
			return ExistingEvent;
		}

		UK2Node_Event* EventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Event>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[EventName](UK2Node_Event* NewNode)
			{
				NewNode->EventReference.SetExternalMember(EventName, UAnimInstance::StaticClass());
				NewNode->bOverrideFunction = true;
			});

		if (EventNode)
		{
			Result.Messages.Add(FString::Printf(TEXT("Created event node: %s"), *EventName.ToString()));
		}
		return EventNode;
	}

	static int32 CountSequenceOutputs(UK2Node_ExecutionSequence* Sequence)
	{
		int32 Count = 0;
		if (!Sequence)
		{
			return Count;
		}

		for (UEdGraphPin* Pin : Sequence->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && UEdGraphSchema_K2::IsExecPin(*Pin))
			{
				++Count;
			}
		}
		return Count;
	}

	static UEdGraphPin* FindAvailableThenPin(UK2Node_ExecutionSequence* Sequence)
	{
		if (!Sequence)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Sequence->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && UEdGraphSchema_K2::IsExecPin(*Pin) && Pin->LinkedTo.Num() == 0)
			{
				return Pin;
			}
		}

		const int32 OldCount = CountSequenceOutputs(Sequence);
		Sequence->AddInputPin();
		return Sequence->GetThenPinGivenIndex(OldCount);
	}

	static UEdGraphPin* GetEventExecutionEntry(UEdGraph* Graph, const FName EventName, const FVector2D EventPosition, FHTBlueprintToggleGeneratorResult& Result)
	{
		UK2Node_Event* EventNode = FindOrCreateAnimEvent(Graph, EventName, EventPosition, Result);
		if (!EventNode)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not create or find event node: %s"), *EventName.ToString()));
			return nullptr;
		}

		UEdGraphPin* EventOutput = GetSchema()->FindExecutionPin(*EventNode, EGPD_Output);
		if (!EventOutput)
		{
			Result.Errors.Add(FString::Printf(TEXT("Event has no execution output: %s"), *EventName.ToString()));
			return nullptr;
		}

		UK2Node_ExecutionSequence* Sequence = nullptr;
		for (UEdGraphPin* LinkedPin : EventOutput->LinkedTo)
		{
			if (LinkedPin && LinkedPin->Direction == EGPD_Input)
			{
				Sequence = Cast<UK2Node_ExecutionSequence>(LinkedPin->GetOwningNode());
				if (Sequence)
				{
					break;
				}
			}
		}

		if (!Sequence)
		{
			TArray<UEdGraphPin*> OldLinks = EventOutput->LinkedTo;
			EventOutput->BreakAllPinLinks();

			Sequence = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
				Graph,
				FVector2D(EventNode->NodePosX + 260, EventNode->NodePosY),
				EK2NewNodeFlags::None);

			Connect(EventOutput, FindPin(Sequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input), Result, TEXT("Event -> Sequence"));

			UEdGraphPin* PreservePin = Sequence->GetThenPinGivenIndex(0);
			for (UEdGraphPin* OldLink : OldLinks)
			{
				if (OldLink)
				{
					Connect(PreservePin, OldLink, Result, TEXT("Sequence preserve old event link"));
				}
			}
		}

		return FindAvailableThenPin(Sequence);
	}

	static void ConfigureSaveSlotPins(UK2Node_CallFunction* Node, const FString& SlotName)
	{
		SetDefaultValue(FindAnyPin(Node, TEXT("SlotName")), SlotName);
		SetDefaultValue(FindAnyPin(Node, TEXT("UserIndex")), TEXT("0"));
	}

	static TArray<FHTMaterialVisibilityGroup> GetMaterialVisibilityGroups(const FHTBlueprintToggleGeneratorParams& Params)
	{
		TArray<FHTMaterialVisibilityGroup> Groups = Params.MaterialVisibilityGroups;
		if (Groups.Num() == 0)
		{
			TArray<int32> LegacyMaterialIDs = Params.MaterialIDs;
			if (LegacyMaterialIDs.Num() == 0)
			{
				LegacyMaterialIDs.Add(Params.MaterialID);
			}

			if (Params.bToggleMaterialIDsTogether)
			{
				Groups.AddDefaulted_GetRef().MaterialIDs = MoveTemp(LegacyMaterialIDs);
			}
			else
			{
				for (const int32 MaterialID : LegacyMaterialIDs)
				{
					Groups.AddDefaulted_GetRef().MaterialIDs.Add(MaterialID);
				}
			}
		}

		TSet<int32> UsedMaterialIDs;
		for (FHTMaterialVisibilityGroup& Group : Groups)
		{
			TArray<int32> UniqueMaterialIDs;
			for (const int32 MaterialID : Group.MaterialIDs)
			{
				if (!UsedMaterialIDs.Contains(MaterialID))
				{
					UsedMaterialIDs.Add(MaterialID);
					UniqueMaterialIDs.Add(MaterialID);
				}
			}
			Group.MaterialIDs = MoveTemp(UniqueMaterialIDs);
		}
		Groups.RemoveAll([](const FHTMaterialVisibilityGroup& Group)
		{
			return Group.MaterialIDs.Num() == 0;
		});
		return Groups;
	}

	static TArray<int32> GetMaterialIDs(const FHTBlueprintToggleGeneratorParams& Params)
	{
		TArray<int32> MaterialIDs;
		for (const FHTMaterialVisibilityGroup& Group : GetMaterialVisibilityGroups(Params))
		{
			MaterialIDs.Append(Group.MaterialIDs);
		}
		return MaterialIDs;
	}

	static TArray<FHTTextureMaterialSlotGroup> GetTextureMaterialSlotGroups(const FHTBlueprintToggleGeneratorParams& Params)
	{
		TArray<FHTTextureMaterialSlotGroup> Groups = Params.TextureMaterialSlotGroups;
		if (Groups.Num() == 0)
		{
			FHTTextureMaterialSlotGroup& LegacyGroup = Groups.AddDefaulted_GetRef();
			LegacyGroup.SourceMaterialPath = Params.SourceMaterialPath;
			LegacyGroup.MaterialElementIndices = Params.MaterialElementIndices;
			LegacyGroup.TexturePaths = Params.TexturePaths;
			if (LegacyGroup.MaterialElementIndices.Num() == 0)
			{
				LegacyGroup.MaterialElementIndices.Add(Params.MaterialElementIndex);
			}
		}

		for (FHTTextureMaterialSlotGroup& Group : Groups)
		{
			Group.SourceMaterialPath = Trimmed(Group.SourceMaterialPath);
			TArray<int32> UniqueIndices;
			for (const int32 MaterialElementIndex : Group.MaterialElementIndices)
			{
				UniqueIndices.AddUnique(MaterialElementIndex);
			}
			Group.MaterialElementIndices = MoveTemp(UniqueIndices);
			if (Group.TexturePaths.Num() == 0)
			{
				Group.TexturePaths = Params.TexturePaths;
			}
			for (FString& TexturePath : Group.TexturePaths)
			{
				TexturePath = Trimmed(TexturePath);
			}
		}

		Groups.RemoveAll([](const FHTTextureMaterialSlotGroup& Group)
		{
			return Group.SourceMaterialPath.IsEmpty() && Group.MaterialElementIndices.IsEmpty();
		});

		return Groups;
	}

	static TArray<int32> GetMaterialElementIndices(const FHTBlueprintToggleGeneratorParams& Params)
	{
		if (Params.Mode == EHTBlueprintToggleMode::Texture && Params.TextureMaterialSlotGroups.Num() > 0)
		{
			TArray<int32> MaterialElementIndices;
			for (const FHTTextureMaterialSlotGroup& Group : GetTextureMaterialSlotGroups(Params))
			{
				for (const int32 MaterialElementIndex : Group.MaterialElementIndices)
				{
					MaterialElementIndices.AddUnique(MaterialElementIndex);
				}
			}
			return MaterialElementIndices;
		}

		TArray<int32> MaterialElementIndices = Params.MaterialElementIndices;
		if (MaterialElementIndices.Num() == 0)
		{
			MaterialElementIndices.Add(Params.MaterialElementIndex);
		}
		return MaterialElementIndices;
	}

	static TArray<FName> BuildMIDVariableNames(const FName ToggleVariable, const TArray<int32>& MaterialElementIndices)
	{
		TArray<FName> MIDVariables;
		MIDVariables.Reserve(MaterialElementIndices.Num());
		for (const int32 MaterialElementIndex : MaterialElementIndices)
		{
			const FString Suffix = MaterialElementIndices.Num() > 1
				? FString::Printf(TEXT("MID_%d"), MaterialElementIndex)
				: TEXT("MID");
			MIDVariables.Add(FName(*(ToggleVariable.ToString() + Suffix)));
		}
		return MIDVariables;
	}

	static int32 GetCycleStateCount(const FHTBlueprintToggleGeneratorParams& Params)
	{
		if (Params.Mode == EHTBlueprintToggleMode::Texture)
		{
			const TArray<FHTTextureMaterialSlotGroup> Groups = GetTextureMaterialSlotGroups(Params);
			return Groups.Num() > 0 ? FMath::Max(2, Groups[0].TexturePaths.Num()) : FMath::Max(2, Params.TexturePaths.Num());
		}
		if (Params.Mode == EHTBlueprintToggleMode::MaterialInterface)
		{
			return FMath::Max(2, Params.MaterialInterfacePaths.Num());
		}
		return FMath::Max(2, GetMaterialVisibilityGroups(Params).Num() + (Params.bIncludeHiddenMaterialState ? 1 : 0));
	}

	static UK2Node_ExecutionSequence* SpawnSequence(UEdGraph* Graph, const FVector2D Position, const int32 ThenCount)
	{
		UK2Node_ExecutionSequence* Sequence = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
			Graph,
			Position,
			EK2NewNodeFlags::None);

		while (Sequence && CountSequenceOutputs(Sequence) < ThenCount)
		{
			Sequence->AddInputPin();
		}

		return Sequence;
	}

	static void AddNodeToList(TArray<UEdGraphNode*>* OutNodes, UEdGraphNode* Node)
	{
		if (OutNodes && Node)
		{
			OutNodes->Add(Node);
		}
	}

	static UK2Node_CallFunction* SpawnShowMaterialSectionNode(
		UEdGraph* Graph,
		const FVector2D Position,
		const int32 MaterialID,
		const bool bShow,
		const FHTBlueprintToggleGeneratorParams& Params,
		UEdGraphPin* TargetPin,
		FHTBlueprintToggleGeneratorResult& Result,
		TArray<UEdGraphNode*>* OutNodes)
	{
		UK2Node_CallFunction* ShowNode = SpawnCall(Graph, USkinnedMeshComponent::StaticClass(), TEXT("ShowMaterialSection"), Position, Result);
		if (!ShowNode)
		{
			return nullptr;
		}

		AddNodeToList(OutNodes, ShowNode);
		SetDefaultValue(FindAnyPin(ShowNode, TEXT("MaterialID")), FString::FromInt(MaterialID));
		SetDefaultValue(FindAnyPin(ShowNode, TEXT("SectionIndex")), FString::FromInt(Params.SectionIndex));
		SetDefaultValue(FindAnyPin(ShowNode, TEXT("LODIndex")), FString::FromInt(Params.LODIndex));
		SetDefaultValue(FindAnyPin(ShowNode, TEXT("bShow")), bShow ? TEXT("true") : TEXT("false"));
		Connect(TargetPin, FindSelfPin(ShowNode), Result, TEXT("OwningComponent -> Show Target"));
		return ShowNode;
	}

	static UEdGraphPin* AddApplyMaterialNodes(
		UEdGraph* Graph,
		const FHTBlueprintToggleGeneratorParams& Params,
		UEdGraphPin* ExecIn,
		UEdGraphPin* ValuePin,
		const FVector2D Base,
		FHTBlueprintToggleGeneratorResult& Result,
		TArray<UEdGraphNode*>* OutNodes = nullptr)
	{
		const TArray<FHTMaterialVisibilityGroup> Groups = GetMaterialVisibilityGroups(Params);
		const TArray<int32> MaterialIDs = GetMaterialIDs(Params);
		const int32 StateCount = GetCycleStateCount(Params);
		UK2Node_SwitchInteger* SwitchNode = SpawnSwitchOnInt(Graph, Base, StateCount);
		UK2Node_CallFunction* OwningNode = SpawnCall(
			Graph,
			UAnimInstance::StaticClass(),
			TEXT("GetOwningComponent"),
			Base + FVector2D(-120, StateCount * 360 + 120),
			Result);
		if (!SwitchNode || !OwningNode || Groups.Num() == 0 || MaterialIDs.Num() == 0)
		{
			return nullptr;
		}

		AddNodeToList(OutNodes, SwitchNode);
		AddNodeToList(OutNodes, OwningNode);
		Connect(ExecIn, GetSchema()->FindExecutionPin(*SwitchNode, EGPD_Input), Result, TEXT("Exec -> Material Group Switch on Int"));
		Connect(ValuePin, FindAnyPin(SwitchNode, TEXT("Selection")), Result, TEXT("Material group value -> Switch Selection"));

		UEdGraphPin* LastExecOut = nullptr;
		for (int32 StateIndex = 0; StateIndex < StateCount; ++StateIndex)
		{
			const float StateYOffset = static_cast<float>(StateIndex * 360);
			UK2Node_ExecutionSequence* Sequence = SpawnSequence(
				Graph,
				Base + FVector2D(520, -80 + StateYOffset),
				MaterialIDs.Num());
			if (!Sequence)
			{
				continue;
			}

			AddNodeToList(OutNodes, Sequence);
			Connect(
				FindPin(SwitchNode, FName(*FString::FromInt(StateIndex)), EGPD_Output),
				FindPin(Sequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input),
				Result,
				FString::Printf(TEXT("Switch %d -> material group sequence"), StateIndex));

			for (int32 MaterialIndex = 0; MaterialIndex < MaterialIDs.Num(); ++MaterialIndex)
			{
				const bool bShow = StateIndex < Groups.Num() && Groups[StateIndex].MaterialIDs.Contains(MaterialIDs[MaterialIndex]);
				UK2Node_CallFunction* ShowNode = SpawnShowMaterialSectionNode(
					Graph,
					Base + FVector2D(1050 + MaterialIndex * 520, -80 + StateYOffset),
					MaterialIDs[MaterialIndex],
					bShow,
					Params,
					FindAnyPin(OwningNode, UEdGraphSchema_K2::PN_ReturnValue),
					Result,
					OutNodes);
				if (!ShowNode)
				{
					continue;
				}

				Connect(
					Sequence->GetThenPinGivenIndex(MaterialIndex),
					GetSchema()->FindExecutionPin(*ShowNode, EGPD_Input),
					Result,
					FString::Printf(TEXT("Material group state %d -> Material %d"), StateIndex, MaterialIDs[MaterialIndex]));
				LastExecOut = GetSchema()->FindExecutionPin(*ShowNode, EGPD_Output);
			}
		}

		return LastExecOut;
	}

	static UEdGraphPin* AddApplyTextureNodes(
		UEdGraph* Graph,
		const FHTBlueprintToggleGeneratorParams& Params,
		const TArray<FName>& MIDVariables,
		const TArray<TArray<UTexture*>>& TextureStatesByMaterial,
		UEdGraphPin* ExecIn,
		UEdGraphPin* ValuePin,
		const FVector2D Base,
		FHTBlueprintToggleGeneratorResult& Result,
		TArray<UEdGraphNode*>* OutNodes)
	{
		const TArray<int32> MaterialElementIndices = GetMaterialElementIndices(Params);
		const int32 TextureStateCount = TextureStatesByMaterial.Num() > 0 ? TextureStatesByMaterial[0].Num() : 0;
		UK2Node_SwitchInteger* SwitchNode = SpawnSwitchOnInt(Graph, Base, TextureStateCount);
		if (!SwitchNode ||
			MIDVariables.Num() == 0 ||
			MIDVariables.Num() != MaterialElementIndices.Num() ||
			TextureStatesByMaterial.Num() != MIDVariables.Num() ||
			TextureStateCount < 2)
		{
			return nullptr;
		}
		for (const TArray<UTexture*>& TextureStates : TextureStatesByMaterial)
		{
			if (TextureStates.Num() != TextureStateCount)
			{
				return nullptr;
			}
		}

		AddNodeToList(OutNodes, SwitchNode);

		Connect(ExecIn, GetSchema()->FindExecutionPin(*SwitchNode, EGPD_Input), Result, TEXT("Exec -> Texture Switch on Int"));
		Connect(ValuePin, FindAnyPin(SwitchNode, TEXT("Selection")), Result, TEXT("Texture state -> Switch Selection"));

		TArray<UK2Node_VariableGet*> GetMIDNodes;
		for (int32 MaterialIndex = 0; MaterialIndex < MIDVariables.Num(); ++MaterialIndex)
		{
			UK2Node_VariableGet* GetMID = SpawnVariableGet(
				Graph,
				MIDVariables[MaterialIndex],
				nullptr,
				Base + FVector2D(1000 + MaterialIndex * 520, TextureStateCount * 360 + 120));
			if (!GetMID)
			{
				return nullptr;
			}
			GetMIDNodes.Add(GetMID);
			AddNodeToList(OutNodes, GetMID);
		}

		UEdGraphPin* LastExecOut = nullptr;
		for (int32 TextureIndex = 0; TextureIndex < TextureStateCount; ++TextureIndex)
		{
			UEdGraphPin* StateExec = FindPin(SwitchNode, FName(*FString::FromInt(TextureIndex)), EGPD_Output);
			UK2Node_ExecutionSequence* Sequence = nullptr;
			if (MIDVariables.Num() > 1)
			{
				Sequence = SpawnSequence(Graph, Base + FVector2D(520, -80 + TextureIndex * 360), MIDVariables.Num());
				if (!Sequence)
				{
					continue;
				}
				AddNodeToList(OutNodes, Sequence);
				Connect(StateExec, FindPin(Sequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input), Result, FString::Printf(TEXT("Texture state %d -> material sequence"), TextureIndex));
			}

			for (int32 MaterialIndex = 0; MaterialIndex < MIDVariables.Num(); ++MaterialIndex)
			{
				UK2Node_CallFunction* SetTexture = SpawnCall(
					Graph,
					UMaterialInstanceDynamic::StaticClass(),
					TEXT("SetTextureParameterValue"),
					Base + FVector2D(1050 + MaterialIndex * 520, -80 + TextureIndex * 360),
					Result);
				if (!SetTexture)
				{
					continue;
				}

				AddNodeToList(OutNodes, SetTexture);
				SetDefaultValue(FindAnyPin(SetTexture, TEXT("ParameterName")), Params.TextureParameterName);
				SetDefaultObject(FindAnyPin(SetTexture, TEXT("Value")), TextureStatesByMaterial[MaterialIndex][TextureIndex]);
				UEdGraphPin* SetTextureExec = GetSchema()->FindExecutionPin(*SetTexture, EGPD_Input);
				if (Sequence)
				{
					Connect(Sequence->GetThenPinGivenIndex(MaterialIndex), SetTextureExec, Result, FString::Printf(TEXT("State %d -> Material Slot %d"), TextureIndex, MaterialElementIndices[MaterialIndex]));
				}
				else
				{
					Connect(StateExec, SetTextureExec, Result, FString::Printf(TEXT("Switch %d -> Texture %d"), TextureIndex, TextureIndex));
				}
				Connect(
					FindAnyPin(GetMIDNodes[MaterialIndex], MIDVariables[MaterialIndex]),
					FindSelfPin(SetTexture),
					Result,
					FString::Printf(TEXT("MID %d -> Texture %d Target"), MaterialIndex, TextureIndex));
				LastExecOut = GetSchema()->FindExecutionPin(*SetTexture, EGPD_Output);
			}
		}

		return LastExecOut;
	}

	static UEdGraphPin* AddApplyMaterialInterfaceNodes(
		UEdGraph* Graph,
		const FHTBlueprintToggleGeneratorParams& Params,
		const TArray<UMaterialInterface*>& Materials,
		UEdGraphPin* ExecIn,
		UEdGraphPin* ValuePin,
		const FVector2D Base,
		FHTBlueprintToggleGeneratorResult& Result,
		TArray<UEdGraphNode*>* OutNodes)
	{
		const TArray<int32> MaterialElementIndices = GetMaterialElementIndices(Params);
		UK2Node_SwitchInteger* SwitchNode = SpawnSwitchOnInt(Graph, Base, Materials.Num());
		UK2Node_CallFunction* OwningNode = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), Base + FVector2D(-120, Materials.Num() * 360 + 120), Result);
		if (!SwitchNode || !OwningNode || MaterialElementIndices.Num() == 0)
		{
			return nullptr;
		}

		AddNodeToList(OutNodes, SwitchNode);
		AddNodeToList(OutNodes, OwningNode);

		Connect(ExecIn, GetSchema()->FindExecutionPin(*SwitchNode, EGPD_Input), Result, TEXT("Exec -> Material Switch on Int"));
		Connect(ValuePin, FindAnyPin(SwitchNode, TEXT("Selection")), Result, TEXT("Material state -> Switch Selection"));

		UEdGraphPin* LastExecOut = nullptr;
		for (int32 MaterialStateIndex = 0; MaterialStateIndex < Materials.Num(); ++MaterialStateIndex)
		{
			UEdGraphPin* StateExec = FindPin(SwitchNode, FName(*FString::FromInt(MaterialStateIndex)), EGPD_Output);
			UK2Node_ExecutionSequence* Sequence = nullptr;
			if (MaterialElementIndices.Num() > 1)
			{
				Sequence = SpawnSequence(Graph, Base + FVector2D(520, -80 + MaterialStateIndex * 360), MaterialElementIndices.Num());
				if (!Sequence)
				{
					continue;
				}
				AddNodeToList(OutNodes, Sequence);
				Connect(StateExec, FindPin(Sequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input), Result, FString::Printf(TEXT("Material state %d -> slot sequence"), MaterialStateIndex));
			}

			for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialElementIndices.Num(); ++MaterialSlotIndex)
			{
				UK2Node_CallFunction* SetMaterial = SpawnCall(
					Graph,
					UPrimitiveComponent::StaticClass(),
					TEXT("SetMaterial"),
					Base + FVector2D(1050 + MaterialSlotIndex * 520, -80 + MaterialStateIndex * 360),
					Result);
				if (!SetMaterial)
				{
					continue;
				}

				AddNodeToList(OutNodes, SetMaterial);
				SetDefaultValue(FindAnyPin(SetMaterial, TEXT("ElementIndex")), FString::FromInt(MaterialElementIndices[MaterialSlotIndex]));
				SetDefaultObject(FindAnyPin(SetMaterial, TEXT("Material")), Materials[MaterialStateIndex]);
				UEdGraphPin* SetMaterialExec = GetSchema()->FindExecutionPin(*SetMaterial, EGPD_Input);
				if (Sequence)
				{
					Connect(Sequence->GetThenPinGivenIndex(MaterialSlotIndex), SetMaterialExec, Result, FString::Printf(TEXT("State %d -> Material Slot %d"), MaterialStateIndex, MaterialElementIndices[MaterialSlotIndex]));
				}
				else
				{
					Connect(StateExec, SetMaterialExec, Result, FString::Printf(TEXT("Switch %d -> Material %d"), MaterialStateIndex, MaterialStateIndex));
				}
				Connect(
					FindAnyPin(OwningNode, UEdGraphSchema_K2::PN_ReturnValue),
					FindSelfPin(SetMaterial),
					Result,
					FString::Printf(TEXT("Owning Component -> Set Material Slot %d"), MaterialElementIndices[MaterialSlotIndex]));
				LastExecOut = GetSchema()->FindExecutionPin(*SetMaterial, EGPD_Output);
			}
		}

		return LastExecOut;
	}

	static UEdGraphPin* AddApplyToggleNodes(
		UEdGraph* Graph,
		const FHTBlueprintToggleGeneratorParams& Params,
		const TArray<FName>& MIDVariables,
		const TArray<TArray<UTexture*>>& TextureStatesByMaterial,
		const TArray<UMaterialInterface*>& Materials,
		UEdGraphPin* ExecIn,
		UEdGraphPin* ValuePin,
		const FVector2D Base,
		FHTBlueprintToggleGeneratorResult& Result,
		TArray<UEdGraphNode*>* OutNodes = nullptr)
	{
		if (Params.Mode == EHTBlueprintToggleMode::Texture)
		{
			return AddApplyTextureNodes(Graph, Params, MIDVariables, TextureStatesByMaterial, ExecIn, ValuePin, Base, Result, OutNodes);
		}
		if (Params.Mode == EHTBlueprintToggleMode::MaterialInterface)
		{
			return AddApplyMaterialInterfaceNodes(Graph, Params, Materials, ExecIn, ValuePin, Base, Result, OutNodes);
		}

		return AddApplyMaterialNodes(Graph, Params, ExecIn, ValuePin, Base, Result, OutNodes);
	}

	static void GenerateInitializeGraph(
		UBlueprint* AnimBlueprint,
		UClass* SaveClass,
		const FName ToggleVariable,
		const FName SaveVariable,
		const TArray<FName>& MIDVariables,
		const FString& SlotName,
		const FHTBlueprintToggleGeneratorParams& Params,
		const TArray<UMaterialInterface*>& TextureSourceMaterials,
		const TArray<TArray<UTexture*>>& TextureStatesByMaterial,
		const TArray<UMaterialInterface*>& Materials,
		const int32 BaseY,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
		if (!Graph)
		{
			Result.Errors.Add(TEXT("The Animation Blueprint has no Event Graph."));
			return;
		}

		UEdGraphPin* EntryExec = GetEventExecutionEntry(Graph, TEXT("BlueprintInitializeAnimation"), FVector2D(-1500, BaseY), Result);
		if (!EntryExec)
		{
			return;
		}

		const int32 TrueY = BaseY - 280;
		const int32 FalseY = BaseY + 470;
		const TArray<int32> MaterialElementIndices = GetMaterialElementIndices(Params);
		const int32 FlowOffsetX = Params.Mode == EHTBlueprintToggleMode::Texture
			? 900 + FMath::Max(0, MaterialElementIndices.Num() - 1) * 700
			: 0;

		UK2Node_CallFunction* DoesSaveExist = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("DoesSaveGameExist"), FVector2D(-960 + FlowOffsetX, BaseY), Result);
		UK2Node_IfThenElse* Branch = SpawnBranch(Graph, FVector2D(-620 + FlowOffsetX, BaseY));
		UK2Node_CallFunction* LoadGame = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("LoadGameFromSlot"), FVector2D(-260 + FlowOffsetX, TrueY), Result);
		UK2Node_DynamicCast* CastSave = SpawnCast(Graph, SaveClass, FVector2D(100 + FlowOffsetX, TrueY));
		UK2Node_VariableGet* GetSaveValue = SpawnVariableGet(Graph, SaveVariable, SaveClass, FVector2D(420 + FlowOffsetX, TrueY + 130));
		UK2Node_VariableSet* SetAnimValue = SpawnVariableSet(Graph, ToggleVariable, nullptr, FVector2D(700 + FlowOffsetX, TrueY));

		UK2Node_CallFunction* CreateSave = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("CreateSaveGameObject"), FVector2D(-260 + FlowOffsetX, FalseY), Result);
		UK2Node_VariableGet* GetAnimDefault = SpawnVariableGet(Graph, ToggleVariable, nullptr, FVector2D(100 + FlowOffsetX, FalseY + 170));
		UK2Node_VariableSet* SetSaveDefault = SpawnVariableSet(Graph, SaveVariable, SaveClass, FVector2D(420 + FlowOffsetX, FalseY));
		UK2Node_CallFunction* SaveDefaultSlot = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("SaveGameToSlot"), FVector2D(760 + FlowOffsetX, FalseY), Result);

		if (!DoesSaveExist || !Branch || !LoadGame || !CastSave || !GetSaveValue || !SetAnimValue || !CreateSave || !GetAnimDefault || !SetSaveDefault || !SaveDefaultSlot)
		{
			return;
		}

		ConfigureSaveSlotPins(DoesSaveExist, SlotName);
		ConfigureSaveSlotPins(LoadGame, SlotName);
		ConfigureSaveSlotPins(SaveDefaultSlot, SlotName);
		SetDefaultObject(FindAnyPin(CreateSave, TEXT("SaveGameClass")), SaveClass);

		UEdGraphPin* SaveFlowEntry = EntryExec;
		TArray<UEdGraphNode*> SetupNodes;
		if (Params.Mode == EHTBlueprintToggleMode::Texture)
		{
			UK2Node_CallFunction* GetOwningForMID = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), FVector2D(-1460, BaseY + 380), Result);
			if (!GetOwningForMID || MIDVariables.Num() != MaterialElementIndices.Num() || TextureSourceMaterials.Num() != MaterialElementIndices.Num())
			{
				return;
			}

			SetupNodes.Add(GetOwningForMID);
			UEdGraphPin* SetupExec = EntryExec;
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialElementIndices.Num(); ++MaterialIndex)
			{
				const float SetupX = -1200.0f + MaterialIndex * 700.0f;
				UK2Node_CallFunction* CreateMID = SpawnCall(Graph, UPrimitiveComponent::StaticClass(), TEXT("CreateDynamicMaterialInstance"), FVector2D(SetupX, BaseY), Result);
				UK2Node_VariableSet* SetMID = SpawnVariableSet(Graph, MIDVariables[MaterialIndex], nullptr, FVector2D(SetupX + 380.0f, BaseY));
				if (!CreateMID || !SetMID)
				{
					return;
				}

				SetDefaultValue(FindAnyPin(CreateMID, TEXT("ElementIndex")), FString::FromInt(MaterialElementIndices[MaterialIndex]));
				SetDefaultObject(FindAnyPin(CreateMID, TEXT("SourceMaterial")), TextureSourceMaterials[MaterialIndex]);
				Connect(SetupExec, GetSchema()->FindExecutionPin(*CreateMID, EGPD_Input), Result, FString::Printf(TEXT("Create MID for Material Slot %d"), MaterialElementIndices[MaterialIndex]));
				Connect(FindAnyPin(GetOwningForMID, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(CreateMID), Result, TEXT("Owning Component -> Create MID Target"));
				Connect(FindAnyPin(CreateMID, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetMID, MIDVariables[MaterialIndex]), Result, TEXT("Create MID -> MID variable"));
				Connect(GetSchema()->FindExecutionPin(*CreateMID, EGPD_Output), GetSchema()->FindExecutionPin(*SetMID, EGPD_Input), Result, TEXT("Create MID -> Set MID"));
				SetupExec = GetSchema()->FindExecutionPin(*SetMID, EGPD_Output);
				SetupNodes.Add(CreateMID);
				SetupNodes.Add(SetMID);
			}
			SaveFlowEntry = SetupExec;
		}

		Connect(SaveFlowEntry, GetSchema()->FindExecutionPin(*DoesSaveExist, EGPD_Input), Result, TEXT("Init entry -> DoesSaveGameExist"));
		Connect(GetSchema()->FindExecutionPin(*DoesSaveExist, EGPD_Output), GetSchema()->FindExecutionPin(*Branch, EGPD_Input), Result, TEXT("DoesSaveGameExist -> Branch"));
		Connect(FindAnyPin(DoesSaveExist, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Branch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("DoesSaveGameExist Return -> Branch Condition"));

		Connect(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Then), GetSchema()->FindExecutionPin(*LoadGame, EGPD_Input), Result, TEXT("Init true -> LoadGame"));
		Connect(GetSchema()->FindExecutionPin(*LoadGame, EGPD_Output), GetSchema()->FindExecutionPin(*CastSave, EGPD_Input), Result, TEXT("LoadGame -> Cast"));
		Connect(FindAnyPin(LoadGame, UEdGraphSchema_K2::PN_ReturnValue), CastSave->GetCastSourcePin(), Result, TEXT("Load Return -> Cast Object"));
		Connect(CastSave->GetCastResultPin(), FindSelfPin(GetSaveValue), Result, TEXT("Cast Result -> Save value target"));
		Connect(CastSave->GetValidCastPin(), GetSchema()->FindExecutionPin(*SetAnimValue, EGPD_Input), Result, TEXT("Cast valid -> Set Anim"));
		UEdGraphPin* RestoredStatePin = FindAnyPin(GetSaveValue, SaveVariable);
		if (Params.Mode == EHTBlueprintToggleMode::MaterialSection && !Params.bIncludeHiddenMaterialState)
		{
			// An older save may still contain the removed hide-all state or a group index that no longer exists.
			UK2Node_CallFunction* ClampSavedState = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Clamp"), FVector2D(700 + FlowOffsetX, TrueY + 180), Result);
			if (!ClampSavedState)
			{
				return;
			}
			SetDefaultValue(FindAnyPin(ClampSavedState, TEXT("Min")), TEXT("0"));
			SetDefaultValue(FindAnyPin(ClampSavedState, TEXT("Max")), FString::FromInt(GetCycleStateCount(Params) - 1));
			Connect(RestoredStatePin, FindAnyPin(ClampSavedState, TEXT("Value")), Result, TEXT("Saved material group -> Clamp"));
			RestoredStatePin = FindAnyPin(ClampSavedState, UEdGraphSchema_K2::PN_ReturnValue);
			SetupNodes.Add(ClampSavedState);
		}
		Connect(RestoredStatePin, FindAnyPin(SetAnimValue, ToggleVariable), Result, TEXT("Save value -> Anim value"));

		TArray<UEdGraphNode*> MaterialNodes;
		UEdGraphPin* SetAnimThen = GetSchema()->FindExecutionPin(*SetAnimValue, EGPD_Output);
		AddApplyToggleNodes(Graph, Params, MIDVariables, TextureStatesByMaterial, Materials, SetAnimThen, FindSetOutputPin(SetAnimValue), FVector2D(1040 + FlowOffsetX, TrueY), Result, &MaterialNodes);

		Connect(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Else), GetSchema()->FindExecutionPin(*CreateSave, EGPD_Input), Result, TEXT("Init false -> CreateSaveGame"));
		Connect(GetSchema()->FindExecutionPin(*CreateSave, EGPD_Output), GetSchema()->FindExecutionPin(*SetSaveDefault, EGPD_Input), Result, TEXT("CreateSave -> Set Save default"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(SetSaveDefault), Result, TEXT("CreateSave Return -> Set Save target"));
		Connect(FindAnyPin(GetAnimDefault, ToggleVariable), FindAnyPin(SetSaveDefault, SaveVariable), Result, TEXT("Anim default -> Save default"));
		Connect(GetSchema()->FindExecutionPin(*SetSaveDefault, EGPD_Output), GetSchema()->FindExecutionPin(*SaveDefaultSlot, EGPD_Input), Result, TEXT("Set Save default -> SaveGameToSlot"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SaveDefaultSlot, TEXT("SaveGameObject")), Result, TEXT("CreateSave Return -> SaveGameToSlot"));
		const int32 AssetStateCount = GetCycleStateCount(Params);
		const float CommentHeight = FMath::Max(1800.0f, 1000.0f + AssetStateCount * 360.0f);
		const float VisibilityGroupWidth = Params.Mode == EHTBlueprintToggleMode::MaterialSection
			? FMath::Max(0, GetMaterialIDs(Params).Num() - 1) * 520.0f
			: 0.0f;
		UEdGraphNode_Comment* Comment = SpawnCommentBox(
			Graph,
			FString::Printf(TEXT("HT Init - %s"), *ToggleVariable.ToString()),
			FVector2D(-1500, BaseY - 560),
			FVector2D(Params.Mode == EHTBlueprintToggleMode::Texture || Params.Mode == EHTBlueprintToggleMode::MaterialInterface ? 7000 + FMath::Max(0, MaterialElementIndices.Num() - 1) * 520 : 6100 + VisibilityGroupWidth, CommentHeight),
			FLinearColor(0.08f, 0.22f, 0.34f, 1.0f));
		TArray<UEdGraphNode*> CommentNodes = { DoesSaveExist, Branch, LoadGame, CastSave, GetSaveValue, SetAnimValue, CreateSave, GetAnimDefault, SetSaveDefault, SaveDefaultSlot };
		CommentNodes.Append(SetupNodes);
		CommentNodes.Append(MaterialNodes);
		AddNodesToComment(Comment, CommentNodes);
	}

	static void GenerateUpdateGraph(
		UBlueprint* AnimBlueprint,
		UClass* SaveClass,
		const FName ToggleVariable,
		const FName SaveVariable,
		const TArray<FName>& MIDVariables,
		const FString& SlotName,
		const FHTBlueprintToggleGeneratorParams& Params,
		const TArray<TArray<UTexture*>>& TextureStatesByMaterial,
		const TArray<UMaterialInterface*>& Materials,
		const int32 BaseY,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
		if (!Graph)
		{
			Result.Errors.Add(TEXT("The Animation Blueprint has no Event Graph."));
			return;
		}

		UEdGraphPin* EntryExec = GetEventExecutionEntry(Graph, TEXT("BlueprintUpdateAnimation"), FVector2D(-1500, BaseY), Result);
		if (!EntryExec)
		{
			return;
		}

		FInputKeySpec KeySpec;
		FString KeyError;
		if (!ResolveInputKeySpec(Params.KeyName, KeySpec, KeyError))
		{
			Result.Errors.Add(KeyError);
			return;
		}
		const TArray<int32> MaterialElementIndices = GetMaterialElementIndices(Params);

		UK2Node_CallFunction* GetController = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("GetPlayerController"), FVector2D(-960, BaseY + 120), Result);
		UK2Node_CallFunction* WasPressed = SpawnCall(Graph, APlayerController::StaticClass(), TEXT("WasInputKeyJustPressed"), FVector2D(-620, BaseY + 60), Result);
		UK2Node_CallFunction* ModifierRightDown = nullptr;
		UK2Node_CallFunction* ModifierLeftDown = nullptr;
		UK2Node_CommutativeAssociativeBinaryOperator* ModifierOrNode = nullptr;

		if (KeySpec.HasModifier())
		{
			ModifierRightDown = SpawnCall(Graph, APlayerController::StaticClass(), TEXT("IsInputKeyDown"), FVector2D(-620, BaseY + 240), Result);
			ModifierLeftDown = SpawnCall(Graph, APlayerController::StaticClass(), TEXT("IsInputKeyDown"), FVector2D(-620, BaseY + 420), Result);
			ModifierOrNode = SpawnBooleanOperator(Graph, TEXT("BooleanOR"), FVector2D(-260, BaseY + 330), 2, Result);
		}

		UK2Node_CallFunction* GetOwningForRendered = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), FVector2D(-620, KeySpec.HasModifier() ? BaseY + 640 : BaseY + 340), Result);
		UK2Node_CallFunction* WasRendered = SpawnCall(Graph, UPrimitiveComponent::StaticClass(), TEXT("WasRecentlyRendered"), FVector2D(-260, KeySpec.HasModifier() ? BaseY + 640 : BaseY + 340), Result);
		UK2Node_CommutativeAssociativeBinaryOperator* AndNode = SpawnBooleanOperator(Graph, TEXT("BooleanAND"), FVector2D(80, KeySpec.HasModifier() ? BaseY + 330 : BaseY + 190), KeySpec.HasModifier() ? 3 : 2, Result);
		UK2Node_IfThenElse* Branch = SpawnBranch(Graph, FVector2D(420, BaseY));

		UK2Node_VariableGet* GetAnimValue = SpawnVariableGet(Graph, ToggleVariable, nullptr, FVector2D(760, BaseY + 190));
		UK2Node_CallFunction* AddNode = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Add_IntInt"), FVector2D(1000, BaseY + 190), Result);
		UK2Node_CallFunction* ModNode = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Percent_IntInt"), FVector2D(1240, BaseY + 190), Result);
		UK2Node_VariableSet* SetAnimValue = SpawnVariableSet(Graph, ToggleVariable, nullptr, FVector2D(1500, BaseY));

		UK2Node_CallFunction* CreateSave = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("CreateSaveGameObject"), FVector2D(1840, BaseY), Result);
		UK2Node_VariableSet* SetSaveValue = SpawnVariableSet(Graph, SaveVariable, SaveClass, FVector2D(2180, BaseY));
		UK2Node_CallFunction* SaveSlot = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("SaveGameToSlot"), FVector2D(2520, BaseY), Result);

		if (!GetController || !WasPressed || !GetOwningForRendered || !WasRendered || !AndNode || !Branch || !GetAnimValue || !AddNode || !ModNode || !SetAnimValue || !CreateSave || !SetSaveValue || !SaveSlot)
		{
			return;
		}
		if (KeySpec.HasModifier() && (!ModifierRightDown || !ModifierLeftDown || !ModifierOrNode))
		{
			return;
		}

		SetDefaultValue(FindAnyPin(GetController, TEXT("PlayerIndex")), TEXT("0"));
		SetDefaultValue(FindAnyPin(WasPressed, TEXT("Key")), KeySpec.MainKey.GetFName().ToString());
		if (KeySpec.HasModifier())
		{
			FKey LeftModifierKey;
			FKey RightModifierKey;
			GetModifierKeys(KeySpec.Modifier, LeftModifierKey, RightModifierKey);
			SetDefaultValue(FindAnyPin(ModifierRightDown, TEXT("Key")), RightModifierKey.GetFName().ToString());
			SetDefaultValue(FindAnyPin(ModifierLeftDown, TEXT("Key")), LeftModifierKey.GetFName().ToString());
		}
		SetDefaultValue(FindAnyPin(WasRendered, TEXT("Tolerance")), TEXT("0.2"));
		SetDefaultValue(FindAnyPin(AddNode, TEXT("B")), TEXT("1"));
		SetDefaultValue(FindAnyPin(ModNode, TEXT("B")), FString::FromInt(GetCycleStateCount(Params)));
		SetDefaultObject(FindAnyPin(CreateSave, TEXT("SaveGameClass")), SaveClass);
		ConfigureSaveSlotPins(SaveSlot, SlotName);

		Connect(FindAnyPin(GetController, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(WasPressed), Result, TEXT("GetPlayerController -> WasInputKeyJustPressed"));
		if (KeySpec.HasModifier())
		{
			Connect(FindAnyPin(GetController, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(ModifierRightDown), Result, TEXT("GetPlayerController -> Right modifier"));
			Connect(FindAnyPin(GetController, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(ModifierLeftDown), Result, TEXT("GetPlayerController -> Left modifier"));
			Connect(FindAnyPin(ModifierRightDown, UEdGraphSchema_K2::PN_ReturnValue), ModifierOrNode->GetInputPin(0), Result, TEXT("Right modifier -> OR A"));
			Connect(FindAnyPin(ModifierLeftDown, UEdGraphSchema_K2::PN_ReturnValue), ModifierOrNode->GetInputPin(1), Result, TEXT("Left modifier -> OR B"));
		}
		Connect(FindAnyPin(GetOwningForRendered, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(WasRendered), Result, TEXT("GetOwningComponent -> WasRecentlyRendered"));
		Connect(FindAnyPin(WasPressed, UEdGraphSchema_K2::PN_ReturnValue), AndNode->GetInputPin(0), Result, TEXT("WasPressed -> AND A"));
		if (KeySpec.HasModifier())
		{
			Connect(FindAnyPin(ModifierOrNode, UEdGraphSchema_K2::PN_ReturnValue), AndNode->GetInputPin(1), Result, TEXT("Modifier OR -> AND B"));
			Connect(FindAnyPin(WasRendered, UEdGraphSchema_K2::PN_ReturnValue), AndNode->GetInputPin(2), Result, TEXT("WasRendered -> AND C"));
		}
		else
		{
			Connect(FindAnyPin(WasRendered, UEdGraphSchema_K2::PN_ReturnValue), AndNode->GetInputPin(1), Result, TEXT("WasRendered -> AND B"));
		}
		Connect(EntryExec, GetSchema()->FindExecutionPin(*Branch, EGPD_Input), Result, TEXT("Update entry -> Branch"));
		Connect(FindAnyPin(AndNode, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Branch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("AND -> Branch condition"));

		Connect(FindAnyPin(GetAnimValue, ToggleVariable), FindAnyPin(AddNode, TEXT("A")), Result, TEXT("Anim value -> Add A"));
		Connect(FindAnyPin(AddNode, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(ModNode, TEXT("A")), Result, TEXT("Add -> Percent"));
		Connect(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Then), GetSchema()->FindExecutionPin(*SetAnimValue, EGPD_Input), Result, TEXT("Branch true -> Set Anim"));
		Connect(FindAnyPin(ModNode, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetAnimValue, ToggleVariable), Result, TEXT("Percent -> Set Anim"));

		Connect(GetSchema()->FindExecutionPin(*SetAnimValue, EGPD_Output), GetSchema()->FindExecutionPin(*CreateSave, EGPD_Input), Result, TEXT("Set Anim -> CreateSave"));
		Connect(GetSchema()->FindExecutionPin(*CreateSave, EGPD_Output), GetSchema()->FindExecutionPin(*SetSaveValue, EGPD_Input), Result, TEXT("CreateSave -> Set Save"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(SetSaveValue), Result, TEXT("CreateSave Return -> Set Save target"));
		Connect(FindSetOutputPin(SetAnimValue), FindAnyPin(SetSaveValue, SaveVariable), Result, TEXT("Set Anim output -> Set Save"));
		Connect(GetSchema()->FindExecutionPin(*SetSaveValue, EGPD_Output), GetSchema()->FindExecutionPin(*SaveSlot, EGPD_Input), Result, TEXT("Set Save -> SaveGameToSlot"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SaveSlot, TEXT("SaveGameObject")), Result, TEXT("CreateSave Return -> SaveGameToSlot"));

		TArray<UEdGraphNode*> MaterialNodes;
		UEdGraphPin* SaveThen = GetSchema()->FindExecutionPin(*SaveSlot, EGPD_Output);
		AddApplyToggleNodes(Graph, Params, MIDVariables, TextureStatesByMaterial, Materials, SaveThen, FindSetOutputPin(SetAnimValue), FVector2D(2900, BaseY), Result, &MaterialNodes);

		const int32 AssetStateCount = GetCycleStateCount(Params);
		const float CommentHeight = FMath::Max(1800.0f, 900.0f + AssetStateCount * 360.0f);
		const float VisibilityGroupWidth = Params.Mode == EHTBlueprintToggleMode::MaterialSection
			? FMath::Max(0, GetMaterialIDs(Params).Num() - 1) * 520.0f
			: 0.0f;
		UEdGraphNode_Comment* Comment = SpawnCommentBox(
			Graph,
			FString::Printf(TEXT("HT Update - %s"), *ToggleVariable.ToString()),
			FVector2D(-1030, BaseY - 220),
			FVector2D(5600 + FMath::Max(0, MaterialElementIndices.Num() - 1) * 520 + VisibilityGroupWidth, CommentHeight),
			FLinearColor(0.10f, 0.24f, 0.18f, 1.0f));
		TArray<UEdGraphNode*> CommentNodes = { GetController, WasPressed };
		if (KeySpec.HasModifier())
		{
			CommentNodes.Add(ModifierRightDown);
			CommentNodes.Add(ModifierLeftDown);
			CommentNodes.Add(ModifierOrNode);
		}
		CommentNodes.Add(GetOwningForRendered);
		CommentNodes.Add(WasRendered);
		CommentNodes.Add(AndNode);
		CommentNodes.Add(Branch);
		CommentNodes.Add(GetAnimValue);
		CommentNodes.Add(AddNode);
		CommentNodes.Add(ModNode);
		CommentNodes.Add(SetAnimValue);
		CommentNodes.Add(CreateSave);
		CommentNodes.Add(SetSaveValue);
		CommentNodes.Add(SaveSlot);
		CommentNodes.Append(MaterialNodes);
		AddNodesToComment(Comment, CommentNodes);
	}

	struct FTextureTunerMapSpec
	{
		FName ParameterName;
		UTexture2D* PreviewTexture = nullptr;
		FName SourceVariable;
		FName RenderTargetVariable;
		FName GainVariable;
		FName ApplyEvent;
		FName ResetEvent;
		FName ExportEvent;
		FString ExportFileName;
	};

	static void GenerateTextureTunerApplyGraph(
		UEdGraph* Graph,
		const FTextureTunerMapSpec& Map,
		const TArray<FName>& MIDVariables,
		const FVector2D Base,
		TArray<UEdGraphNode*>& OutNodes,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UK2Node_CustomEvent* Event = SpawnCustomEvent(Graph, Map.ApplyEvent, Base);
		UK2Node_VariableGet* GetRT = SpawnVariableGet(Graph, Map.RenderTargetVariable, nullptr, Base + FVector2D(260, 220));
		UK2Node_VariableGet* GetSource = SpawnVariableGet(Graph, Map.SourceVariable, nullptr, Base + FVector2D(520, 360));
		UK2Node_VariableGet* GetGain = SpawnVariableGet(Graph, Map.GainVariable, nullptr, Base + FVector2D(520, 500));
		UK2Node_CallFunction* BeginDraw = SpawnCall(Graph, UKismetRenderingLibrary::StaticClass(), TEXT("BeginDrawCanvasToRenderTarget"), Base + FVector2D(260, 0), Result);
		UK2Node_CallFunction* DrawTexture = SpawnCall(Graph, UCanvas::StaticClass(), TEXT("K2_DrawTexture"), Base + FVector2D(760, 0), Result);
		UK2Node_CallFunction* EndDraw = SpawnCall(Graph, UKismetRenderingLibrary::StaticClass(), TEXT("EndDrawCanvasToRenderTarget"), Base + FVector2D(1120, 0), Result);
		if (!Event || !GetRT || !GetSource || !GetGain || !BeginDraw || !DrawTexture || !EndDraw)
		{
			return;
		}

		const FVector2D TextureSize(
			Map.PreviewTexture ? Map.PreviewTexture->GetSizeX() : 1,
			Map.PreviewTexture ? Map.PreviewTexture->GetSizeY() : 1);
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("ScreenPosition")), TEXT("(X=0.000000,Y=0.000000)"));
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("ScreenSize")), FString::Printf(TEXT("(X=%.6f,Y=%.6f)"), TextureSize.X, TextureSize.Y));
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("CoordinatePosition")), TEXT("(X=0.000000,Y=0.000000)"));
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("CoordinateSize")), TEXT("(X=1.000000,Y=1.000000)"));
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("BlendMode")), TEXT("BLEND_Opaque"));
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("Rotation")), TEXT("0.0"));

		Connect(GetSchema()->FindExecutionPin(*Event, EGPD_Output), GetSchema()->FindExecutionPin(*BeginDraw, EGPD_Input), Result, TEXT("Texture tuner Apply -> Begin Draw"));
		Connect(FindAnyPin(GetRT, Map.RenderTargetVariable), FindAnyPin(BeginDraw, TEXT("TextureRenderTarget")), Result, TEXT("Texture tuner RT -> Begin Draw"));
		Connect(GetSchema()->FindExecutionPin(*BeginDraw, EGPD_Output), GetSchema()->FindExecutionPin(*DrawTexture, EGPD_Input), Result, TEXT("Begin Draw -> Draw Texture"));
		Connect(FindAnyPin(BeginDraw, TEXT("Canvas")), FindSelfPin(DrawTexture), Result, TEXT("Canvas -> Draw Texture"));
		Connect(FindAnyPin(GetSource, Map.SourceVariable), FindAnyPin(DrawTexture, TEXT("RenderTexture")), Result, TEXT("Original texture -> Draw Texture"));
		Connect(FindAnyPin(GetGain, Map.GainVariable), FindAnyPin(DrawTexture, TEXT("RenderColor")), Result, TEXT("RGB gain -> Draw Texture tint"));
		Connect(GetSchema()->FindExecutionPin(*DrawTexture, EGPD_Output), GetSchema()->FindExecutionPin(*EndDraw, EGPD_Input), Result, TEXT("Draw Texture -> End Draw"));
		Connect(FindAnyPin(BeginDraw, TEXT("Context")), FindAnyPin(EndDraw, TEXT("Context")), Result, TEXT("Draw context -> End Draw"));

		UEdGraphPin* Exec = GetSchema()->FindExecutionPin(*EndDraw, EGPD_Output);
		for (int32 Index = 0; Index < MIDVariables.Num(); ++Index)
		{
			const FVector2D NodePosition = Base + FVector2D(1460 + Index * 360, 0);
			UK2Node_VariableGet* GetMID = SpawnVariableGet(Graph, MIDVariables[Index], nullptr, NodePosition + FVector2D(0, 220));
			UK2Node_CallFunction* SetTexture = SpawnCall(Graph, UMaterialInstanceDynamic::StaticClass(), TEXT("SetTextureParameterValue"), NodePosition, Result);
			if (!GetMID || !SetTexture)
			{
				return;
			}
			SetDefaultValue(FindAnyPin(SetTexture, TEXT("ParameterName")), Map.ParameterName.ToString());
			Connect(Exec, GetSchema()->FindExecutionPin(*SetTexture, EGPD_Input), Result, TEXT("End Draw -> Set tuned texture"));
			Connect(FindAnyPin(GetMID, MIDVariables[Index]), FindSelfPin(SetTexture), Result, TEXT("MID -> Set tuned texture"));
			Connect(FindAnyPin(GetRT, Map.RenderTargetVariable), FindAnyPin(SetTexture, TEXT("Value")), Result, TEXT("RT -> texture parameter"));
			Exec = GetSchema()->FindExecutionPin(*SetTexture, EGPD_Output);
			OutNodes.Add(GetMID);
			OutNodes.Add(SetTexture);
		}

		OutNodes.Append({ Event, GetRT, GetSource, GetGain, BeginDraw, DrawTexture, EndDraw });
	}

	static void GenerateTextureTunerResetGraph(
		UEdGraph* Graph,
		const FTextureTunerMapSpec& Map,
		const TArray<FName>& MIDVariables,
		const FVector2D Base,
		TArray<UEdGraphNode*>& OutNodes,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UK2Node_CustomEvent* Event = SpawnCustomEvent(Graph, Map.ResetEvent, Base);
		UK2Node_VariableSet* SetGain = SpawnVariableSet(Graph, Map.GainVariable, nullptr, Base + FVector2D(280, 0));
		UK2Node_VariableGet* GetSource = SpawnVariableGet(Graph, Map.SourceVariable, nullptr, Base + FVector2D(560, 260));
		if (!Event || !SetGain || !GetSource)
		{
			return;
		}
		SetDefaultValue(FindAnyPin(SetGain, Map.GainVariable), TEXT("(R=1.000000,G=1.000000,B=1.000000,A=1.000000)"));
		Connect(GetSchema()->FindExecutionPin(*Event, EGPD_Output), GetSchema()->FindExecutionPin(*SetGain, EGPD_Input), Result, TEXT("Texture tuner Reset -> white gain"));

		UEdGraphPin* Exec = GetSchema()->FindExecutionPin(*SetGain, EGPD_Output);
		for (int32 Index = 0; Index < MIDVariables.Num(); ++Index)
		{
			const FVector2D NodePosition = Base + FVector2D(560 + Index * 360, 0);
			UK2Node_VariableGet* GetMID = SpawnVariableGet(Graph, MIDVariables[Index], nullptr, NodePosition + FVector2D(0, 400));
			UK2Node_CallFunction* SetTexture = SpawnCall(Graph, UMaterialInstanceDynamic::StaticClass(), TEXT("SetTextureParameterValue"), NodePosition, Result);
			if (!GetMID || !SetTexture)
			{
				return;
			}
			SetDefaultValue(FindAnyPin(SetTexture, TEXT("ParameterName")), Map.ParameterName.ToString());
			Connect(Exec, GetSchema()->FindExecutionPin(*SetTexture, EGPD_Input), Result, TEXT("Reset gain -> restore texture"));
			Connect(FindAnyPin(GetMID, MIDVariables[Index]), FindSelfPin(SetTexture), Result, TEXT("MID -> restore texture"));
			Connect(FindAnyPin(GetSource, Map.SourceVariable), FindAnyPin(SetTexture, TEXT("Value")), Result, TEXT("Original texture -> parameter"));
			Exec = GetSchema()->FindExecutionPin(*SetTexture, EGPD_Output);
			OutNodes.Add(GetMID);
			OutNodes.Add(SetTexture);
		}
		OutNodes.Append({ Event, SetGain, GetSource });
	}

	static void GenerateTextureTunerExportGraph(
		UEdGraph* Graph,
		const FTextureTunerMapSpec& Map,
		const FVector2D Base,
		TArray<UEdGraphNode*>& OutNodes,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UK2Node_CustomEvent* Event = SpawnCustomEvent(Graph, Map.ExportEvent, Base);
		UK2Node_VariableGet* GetRT = SpawnVariableGet(Graph, Map.RenderTargetVariable, nullptr, Base + FVector2D(260, 220));
		UK2Node_CallFunction* GetSavedDirectory = SpawnCall(
			Graph,
			UKismetSystemLibrary::StaticClass(),
			TEXT("GetProjectSavedDirectory"),
			Base + FVector2D(260, 380),
			Result);
		UK2Node_CallFunction* Export = SpawnCall(
			Graph,
			UKismetRenderingLibrary::StaticClass(),
			TEXT("ExportRenderTarget"),
			Base + FVector2D(560, 0),
			Result);
		if (!Event || !GetRT || !GetSavedDirectory || !Export)
		{
			return;
		}

		SetDefaultValue(FindAnyPin(Export, TEXT("FileName")), Map.ExportFileName);
		Connect(GetSchema()->FindExecutionPin(*Event, EGPD_Output), GetSchema()->FindExecutionPin(*Export, EGPD_Input), Result, TEXT("Texture tuner Export -> Export Render Target"));
		Connect(FindAnyPin(GetRT, Map.RenderTargetVariable), FindAnyPin(Export, TEXT("TextureRenderTarget")), Result, TEXT("Texture tuner RT -> PNG export"));
		Connect(FindAnyPin(GetSavedDirectory, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Export, TEXT("FilePath")), Result, TEXT("Project Saved directory -> PNG export"));
		OutNodes.Append({ Event, GetRT, GetSavedDirectory, Export });
	}

	static FHTBlueprintToggleGeneratorResult GenerateTextureChannelTuner(const FHTBlueprintToggleGeneratorParams& Params)
	{
		FHTBlueprintToggleGeneratorResult Result;
		UBlueprint* AnimBlueprint = LoadBlueprint(Params.AnimBlueprintPath, Result, TEXT("Animation Blueprint"));
		if (!AnimBlueprint)
		{
			return Result;
		}

		const FName TunerName = ToName(Params.ToggleVariableName, TEXT("TextureTuner"));
		TArray<int32> MaterialSlots = GetMaterialElementIndices(Params);
		MaterialSlots.Sort();
		MaterialSlots.SetNum(Algo::Unique(MaterialSlots));
		if (MaterialSlots.IsEmpty())
		{
			Result.Errors.Add(TEXT("Texture Channel Tuner requires at least one Material Slot."));
			return Result;
		}
		for (const int32 Slot : MaterialSlots)
		{
			if (Slot < 0)
			{
				Result.Errors.Add(TEXT("Texture Channel Tuner Material Slots must be zero or greater."));
				return Result;
			}
		}
		if (!Params.bTuneLightMap && !Params.bTuneIDTexture)
		{
			Result.Errors.Add(TEXT("Enable LightMap, ID_Tex, or both for the Texture Channel Tuner."));
			return Result;
		}

		UMaterialInterface* SourceMaterial = LoadMaterial(Params.SourceMaterialPath, Result, TEXT("Texture Tuner Source Material"));
		if (!SourceMaterial)
		{
			return Result;
		}

		TArray<FTextureTunerMapSpec> Maps;
		auto AddMap = [&](const FName ParameterName, const FString& TexturePath)
		{
			UTexture2D* PreviewTexture = Cast<UTexture2D>(LoadTexture(TexturePath, Result, ParameterName.ToString()));
			if (!PreviewTexture)
			{
				if (Result.Errors.IsEmpty())
				{
					Result.Errors.Add(FString::Printf(TEXT("%s must be a Texture2D."), *ParameterName.ToString()));
				}
				return;
			}
			UTexture* EffectiveTexture = nullptr;
			if (!SourceMaterial->GetTextureParameterValue(FMaterialParameterInfo(ParameterName), EffectiveTexture) || !EffectiveTexture)
			{
				Result.Errors.Add(FString::Printf(TEXT("The selected material does not expose an effective %s texture parameter."), *ParameterName.ToString()));
				return;
			}

			const FString Prefix = TunerName.ToString() + TEXT("_") + ParameterName.ToString();
			FTextureTunerMapSpec& Map = Maps.AddDefaulted_GetRef();
			Map.ParameterName = ParameterName;
			Map.PreviewTexture = PreviewTexture;
			Map.SourceVariable = FName(*(Prefix + TEXT("_Source")));
			Map.RenderTargetVariable = FName(*(Prefix + TEXT("_RT")));
			Map.GainVariable = FName(*(Prefix + TEXT("_Gain")));
			Map.ApplyEvent = FName(*(TEXT("HTTC_Apply_") + Prefix));
			Map.ResetEvent = FName(*(TEXT("HTTC_Reset_") + Prefix));
			Map.ExportEvent = FName(*(TEXT("HTTC_Export_") + Prefix));
			Map.ExportFileName = PreviewTexture->GetName() + TEXT("_RGB.png");
		};

		if (Params.bTuneLightMap)
		{
			AddMap(TEXT("LightMap"), Params.LightMapTexturePath);
		}
		if (Params.bTuneIDTexture)
		{
			AddMap(TEXT("ID_Tex"), Params.IDTexturePath);
		}
		if (!Result.Errors.IsEmpty() || Maps.IsEmpty())
		{
			return Result;
		}

		AnimBlueprint->Modify();
		const TArray<FName> MIDVariables = BuildMIDVariableNames(TunerName, MaterialSlots);
		for (int32 Index = 0; Index < MIDVariables.Num(); ++Index)
		{
			if (!EnsureObjectVariable(AnimBlueprint, MIDVariables[Index], UMaterialInstanceDynamic::StaticClass(), Result, FString::Printf(TEXT("Texture tuner MID for slot %d"), MaterialSlots[Index])))
			{
				return Result;
			}
		}
		for (const FTextureTunerMapSpec& Map : Maps)
		{
			if (!EnsureObjectVariable(AnimBlueprint, Map.SourceVariable, UTexture::StaticClass(), Result, Map.ParameterName.ToString() + TEXT(" original texture"))
				|| !EnsureObjectVariable(AnimBlueprint, Map.RenderTargetVariable, UTextureRenderTarget2D::StaticClass(), Result, Map.ParameterName.ToString() + TEXT(" render target"))
				|| !EnsureStructVariable(AnimBlueprint, Map.GainVariable, TBaseStructure<FLinearColor>::Get(), TEXT("(R=1.000000,G=1.000000,B=1.000000,A=1.000000)"), Result, Map.ParameterName.ToString() + TEXT(" RGB gain")))
			{
				return Result;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
		if (!Graph)
		{
			Result.Errors.Add(TEXT("The Animation Blueprint has no Event Graph."));
			return Result;
		}
		RemoveTextureTunerGroup(Graph, TunerName);

		const int32 BaseY = FindNextLayoutBaseY(Graph);
		UEdGraphPin* EntryExec = GetEventExecutionEntry(Graph, TEXT("BlueprintInitializeAnimation"), FVector2D(-1500, BaseY), Result);
		if (!EntryExec)
		{
			return Result;
		}
		TArray<UEdGraphNode*> Nodes;
		UK2Node_CallFunction* GetOwning = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), FVector2D(-1450, BaseY + 280), Result);
		if (!GetOwning)
		{
			return Result;
		}
		Nodes.Add(GetOwning);
		UEdGraphPin* InitExec = EntryExec;
		for (int32 Index = 0; Index < MaterialSlots.Num(); ++Index)
		{
			const FVector2D Position(-1180 + Index * 620, BaseY);
			UK2Node_CallFunction* CreateMID = SpawnCall(Graph, UPrimitiveComponent::StaticClass(), TEXT("CreateDynamicMaterialInstance"), Position, Result);
			UK2Node_VariableSet* SetMID = SpawnVariableSet(Graph, MIDVariables[Index], nullptr, Position + FVector2D(340, 0));
			if (!CreateMID || !SetMID)
			{
				return Result;
			}
			SetDefaultValue(FindAnyPin(CreateMID, TEXT("ElementIndex")), FString::FromInt(MaterialSlots[Index]));
			Connect(InitExec, GetSchema()->FindExecutionPin(*CreateMID, EGPD_Input), Result, TEXT("Texture tuner init -> Create MID"));
			Connect(FindAnyPin(GetOwning, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(CreateMID), Result, TEXT("Owning Component -> tuner MID"));
			Connect(FindAnyPin(CreateMID, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetMID, MIDVariables[Index]), Result, TEXT("Created tuner MID -> variable"));
			Connect(GetSchema()->FindExecutionPin(*CreateMID, EGPD_Output), GetSchema()->FindExecutionPin(*SetMID, EGPD_Input), Result, TEXT("Create tuner MID -> store MID"));
			InitExec = GetSchema()->FindExecutionPin(*SetMID, EGPD_Output);
			Nodes.Append({ CreateMID, SetMID });
		}

		for (int32 MapIndex = 0; MapIndex < Maps.Num(); ++MapIndex)
		{
			const FTextureTunerMapSpec& Map = Maps[MapIndex];
			const FVector2D Position(-1180 + MaterialSlots.Num() * 620 + MapIndex * 920, BaseY);
			UK2Node_VariableGet* GetMID = SpawnVariableGet(Graph, MIDVariables[0], nullptr, Position + FVector2D(0, 300));
			UK2Node_CallFunction* GetTexture = SpawnCall(Graph, UMaterialInstanceDynamic::StaticClass(), TEXT("K2_GetTextureParameterValue"), Position, Result);
			UK2Node_VariableSet* SetSource = SpawnVariableSet(Graph, Map.SourceVariable, nullptr, Position + FVector2D(300, 0));
			UK2Node_CallFunction* CreateRT = SpawnCall(Graph, UKismetRenderingLibrary::StaticClass(), TEXT("CreateRenderTarget2D"), Position + FVector2D(560, 0), Result);
			UK2Node_VariableSet* SetRT = SpawnVariableSet(Graph, Map.RenderTargetVariable, nullptr, Position + FVector2D(900, 0));
			if (!GetMID || !GetTexture || !SetSource || !CreateRT || !SetRT)
			{
				return Result;
			}
			SetDefaultValue(FindAnyPin(GetTexture, TEXT("ParameterName")), Map.ParameterName.ToString());
			SetDefaultValue(FindAnyPin(CreateRT, TEXT("Width")), FString::FromInt(Map.PreviewTexture->GetSizeX()));
			SetDefaultValue(FindAnyPin(CreateRT, TEXT("Height")), FString::FromInt(Map.PreviewTexture->GetSizeY()));
			SetDefaultValue(FindAnyPin(CreateRT, TEXT("Format")), TEXT("RTF_RGBA8"));
			SetDefaultValue(FindAnyPin(CreateRT, TEXT("ClearColor")), TEXT("(R=0.000000,G=0.000000,B=0.000000,A=0.000000)"));
			// EndDrawCanvasToRenderTarget resolves mip 0 but does not regenerate lower mips.
			// Keep calibration previews single-mip so view-dependent LOD cannot sample stale data.
			SetDefaultValue(FindAnyPin(CreateRT, TEXT("bAutoGenerateMipMaps")), TEXT("false"));
			Connect(InitExec, GetSchema()->FindExecutionPin(*GetTexture, EGPD_Input), Result, TEXT("Tuner MID init -> read original texture"));
			Connect(FindAnyPin(GetMID, MIDVariables[0]), FindSelfPin(GetTexture), Result, TEXT("Tuner MID -> Get Texture Parameter"));
			Connect(FindAnyPin(GetTexture, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetSource, Map.SourceVariable), Result, TEXT("Original texture -> source variable"));
			Connect(GetSchema()->FindExecutionPin(*GetTexture, EGPD_Output), GetSchema()->FindExecutionPin(*SetSource, EGPD_Input), Result, TEXT("Get original texture -> store source"));
			Connect(GetSchema()->FindExecutionPin(*SetSource, EGPD_Output), GetSchema()->FindExecutionPin(*CreateRT, EGPD_Input), Result, TEXT("Store source -> Create RT"));
			Connect(FindAnyPin(CreateRT, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetRT, Map.RenderTargetVariable), Result, TEXT("Created RT -> variable"));
			Connect(GetSchema()->FindExecutionPin(*CreateRT, EGPD_Output), GetSchema()->FindExecutionPin(*SetRT, EGPD_Input), Result, TEXT("Create RT -> store RT"));
			InitExec = GetSchema()->FindExecutionPin(*SetRT, EGPD_Output);
			Nodes.Append({ GetMID, GetTexture, SetSource, CreateRT, SetRT });
		}

		for (int32 MapIndex = 0; MapIndex < Maps.Num(); ++MapIndex)
		{
			const float MapY = BaseY + 900.0f + MapIndex * 1500.0f;
			GenerateTextureTunerApplyGraph(Graph, Maps[MapIndex], MIDVariables, FVector2D(-1450, MapY), Nodes, Result);
			GenerateTextureTunerResetGraph(Graph, Maps[MapIndex], MIDVariables, FVector2D(-1450, MapY + 600), Nodes, Result);
			GenerateTextureTunerExportGraph(Graph, Maps[MapIndex], FVector2D(-1450, MapY + 1000), Nodes, Result);
		}

		FString Metadata = FString::Printf(TEXT("HT Texture Tuner - %s;Display=%s"), *TunerName.ToString(), *SourceMaterial->GetName());
		for (const FTextureTunerMapSpec& Map : Maps)
		{
			Metadata += FString::Printf(
				TEXT(";%s=%s,%s,%s,%s,%s,%s"),
				*Map.ParameterName.ToString(),
				*Map.GainVariable.ToString(),
				*Map.ApplyEvent.ToString(),
				*Map.ResetEvent.ToString(),
				Map.PreviewTexture ? *Map.PreviewTexture->GetName() : TEXT("Unknown"),
				*Map.ExportEvent.ToString(),
				*Map.ExportFileName);
		}
		const float CommentHeight = 1100.0f + Maps.Num() * 1500.0f;
		const float CommentWidth = 2600.0f + MaterialSlots.Num() * 360.0f;
		UEdGraphNode_Comment* Comment = SpawnCommentBox(
			Graph,
			Metadata,
			FVector2D(-1550, BaseY - 300),
			FVector2D(CommentWidth, CommentHeight),
			FLinearColor(0.20f, 0.08f, 0.32f, 1.0f));
		AddNodesToComment(Comment, Nodes);
		// Large slot groups extend farther than the legacy fixed-size box.
		// Include every generated node so saved-graph regeneration is complete.
		for (const UEdGraphNode* Node : Nodes)
		{
			Comment->NodeWidth = FMath::Max(Comment->NodeWidth, Node->NodePosX - Comment->NodePosX + 600);
			Comment->NodeHeight = FMath::Max(Comment->NodeHeight, Node->NodePosY - Comment->NodePosY + 300);
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		if (AnimBlueprint->Status == BS_Error)
		{
			Result.Errors.Add(TEXT("The Animation Blueprint has compile errors after Texture Channel Tuner generation."));
		}
		AnimBlueprint->MarkPackageDirty();
		if (Params.bSaveAssets && Result.Errors.IsEmpty())
		{
			TArray<UPackage*> Packages = { AnimBlueprint->GetOutermost() };
			TArray<UPackage*> FailedPackages;
			FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false, &FailedPackages, true, false);
			if (!FailedPackages.IsEmpty())
			{
				Result.Errors.Add(TEXT("Texture Channel Tuner nodes were generated, but the Animation Blueprint could not be saved."));
			}
			else
			{
				Result.Messages.Add(TEXT("Animation Blueprint saved."));
			}
		}

		if (Result.Errors.IsEmpty())
		{
			Result.Messages.Add(FString::Printf(TEXT("Generated Texture Channel Tuner %s for slots: %s"), *TunerName.ToString(), *FString::JoinBy(MaterialSlots, TEXT(","), [](const int32 Slot) { return FString::FromInt(Slot); })));
			Result.Messages.Add(TEXT("RGB gains start at 1.0. Generate the NTE panel to create the runtime sliders."));
		}
		Result.bSuccess = Result.Errors.IsEmpty();
		return Result;
	}

	// ------------------------------------------------------------------
	// Nested visibility (toggle tree). Independent from the modes above:
	// it only shares the static helpers and never changes their behavior.
	// ------------------------------------------------------------------

	static const TCHAR* const NestedToggleCommentPrefix = TEXT("HT Toggle Tree - ");
	static const TCHAR* const NestedToggleApplyPrefix = TEXT("HTN_Apply_");

	static bool IsAsciiIdentifierChar(const TCHAR Character)
	{
		return (Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'))
			|| Character == TEXT('_');
	}

	// Blueprint member and event names stay ASCII so the Kismet compiler, SaveGame
	// slots and panel metadata never depend on the editor locale.
	static FString SanitizeNestedIdentifier(const FString& Raw)
	{
		FString Sanitized;
		for (const TCHAR Character : Trimmed(Raw))
		{
			if (IsAsciiIdentifierChar(Character))
			{
				Sanitized.AppendChar(Character);
			}
			else if (FChar::IsWhitespace(Character) || Character == TEXT('-') || Character == TEXT('.'))
			{
				if (!Sanitized.IsEmpty() && !Sanitized.EndsWith(TEXT("_")))
				{
					Sanitized.AppendChar(TEXT('_'));
				}
			}
		}
		while (Sanitized.EndsWith(TEXT("_")))
		{
			Sanitized.LeftChopInline(1);
		}
		if (!Sanitized.IsEmpty() && FChar::IsDigit(Sanitized[0]))
		{
			Sanitized = TEXT("_") + Sanitized;
		}
		return Sanitized;
	}

	// Metadata fields are separated by ';' and ','; option labels additionally use '|'.
	static FString SanitizeNestedMetadataField(FString Value)
	{
		Value.TrimStartAndEndInline();
		for (const TCHAR* Forbidden : { TEXT(";"), TEXT(","), TEXT("="), TEXT("|"), TEXT("\r"), TEXT("\n"), TEXT("\t") })
		{
			Value.ReplaceInline(Forbidden, TEXT(" "));
		}
		return Value;
	}

	static FString MakeNestedCanonicalKey(const FInputKeySpec& Spec)
	{
		FString Canonical;
		switch (Spec.Modifier)
		{
		case EInputKeyModifier::Control: Canonical = TEXT("Ctrl "); break;
		case EInputKeyModifier::Shift: Canonical = TEXT("Shift "); break;
		case EInputKeyModifier::Alt: Canonical = TEXT("Alt "); break;
		default: break;
		}
		return Canonical + Spec.MainKey.GetFName().ToString();
	}

	static FString MakeNestedSlotName(const FName VariableName, const FString& CharacterName)
	{
		FString SlotName = VariableName.ToString();
		const FString Character = Trimmed(CharacterName);
		if (!Character.IsEmpty())
		{
			SlotName += TEXT(" ") + Character;
		}
		return SlotName;
	}

	static FString JoinNodeIds(const TArray<FString>& NodeIds)
	{
		return FString::Join(NodeIds, TEXT(", "));
	}

	struct FNestedUnionFind
	{
		TArray<int32> Parent;

		explicit FNestedUnionFind(const int32 Count)
		{
			Parent.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		}

		void Union(const int32 A, const int32 B)
		{
			const int32 RootA = Find(A);
			const int32 RootB = Find(B);
			if (RootA != RootB)
			{
				// Keep the smallest index as the representative so group order is stable.
				Parent[FMath::Max(RootA, RootB)] = FMath::Min(RootA, RootB);
			}
		}
	};

	static const TCHAR* NestedSwitchKindName(const EHTNestedToggleSwitchKind Kind)
	{
		switch (Kind)
		{
		case EHTNestedToggleSwitchKind::Texture: return TEXT("Texture switch");
		case EHTNestedToggleSwitchKind::Material: return TEXT("Material switch");
		default: return TEXT("Switch");
		}
	}

	// Two effects can never act at the same time when their parent paths require different
	// options of one state variable.
	static bool AreNestedConditionsExclusive(const TArray<FHTNestedToggleCondition>& A, const TArray<FHTNestedToggleCondition>& B)
	{
		for (const FHTNestedToggleCondition& First : A)
		{
			for (const FHTNestedToggleCondition& Second : B)
			{
				if (First.VariableIndex == Second.VariableIndex && First.Option != Second.Option)
				{
					return true;
				}
			}
		}
		return false;
	}

	static bool CompileNestedToggleGraphImpl(
		const FHTNestedToggleGraph& Graph,
		FHTNestedToggleCompiledGraph& OutCompiled,
		TArray<FHTNestedToggleIssue>& OutErrors)
	{
		OutCompiled = FHTNestedToggleCompiledGraph();
		OutCompiled.Name = FHTBlueprintToggleGenerator::SanitizeNestedToggleGraphName(Graph.Name);
		auto AddIssue = [&OutErrors](TArray<FString> NodeIds, const FString& Message)
		{
			FHTNestedToggleIssue& Issue = OutErrors.AddDefaulted_GetRef();
			Issue.NodeIds = MoveTemp(NodeIds);
			Issue.Message = Message;
		};

		const int32 SwitchCount = Graph.Switches.Num();
		TMap<FString, int32> SwitchIndexById;
		TArray<FInputKeySpec> KeySpecs;
		KeySpecs.SetNum(SwitchCount);
		TArray<FString> ExplicitNames;
		ExplicitNames.SetNum(SwitchCount);
		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			const FHTNestedToggleSwitch& Switch = Graph.Switches[Index];
			const FString NodeId = Trimmed(Switch.NodeId);
			if (NodeId.IsEmpty())
			{
				AddIssue({}, FString::Printf(TEXT("Switch %d has no node id."), Index + 1));
				continue;
			}
			if (SwitchIndexById.Contains(NodeId))
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Switch node id %s is used more than once."), *NodeId));
				continue;
			}
			SwitchIndexById.Add(NodeId, Index);
		}
		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			const FHTNestedToggleSwitch& Switch = Graph.Switches[Index];
			const FString NodeId = Trimmed(Switch.NodeId);
			if (Switch.OptionCount < 2)
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s needs at least two options."), *NodeId));
			}
			if (Switch.InitialOption < 0 || Switch.InitialOption >= Switch.OptionCount)
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s initial option %d is outside 0..%d."), *NodeId, Switch.InitialOption, Switch.OptionCount - 1));
			}
			FString KeyError;
			if (Trimmed(Switch.KeyName).IsEmpty())
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s has no key."), *NodeId));
			}
			else if (!ResolveInputKeySpec(Switch.KeyName, KeySpecs[Index], KeyError))
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s: %s"), *NodeId, *KeyError));
			}
			const FString RawVariable = Trimmed(Switch.VariableName);
			if (!RawVariable.IsEmpty())
			{
				ExplicitNames[Index] = SanitizeNestedIdentifier(RawVariable);
				if (ExplicitNames[Index].IsEmpty())
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s variable name %s must contain letters, digits or underscores."), *NodeId, *RawVariable));
				}
			}
			const FString ParentId = Trimmed(Switch.ParentNodeId);
			if (!ParentId.IsEmpty())
			{
				const int32* ParentIndex = SwitchIndexById.Find(ParentId);
				if (!ParentIndex)
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s hangs on unknown switch %s."), *NodeId, *ParentId));
				}
				else if (*ParentIndex == Index)
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Switch %s cannot hang on itself."), *NodeId));
				}
				else if (Graph.Switches[*ParentIndex].Kind != EHTNestedToggleSwitchKind::Visibility)
				{
					AddIssue({ NodeId, ParentId }, FString::Printf(TEXT("Switch %s hangs on %s, a %s. Texture and Material switches have no options to hang anything on."), *NodeId, *ParentId, NestedSwitchKindName(Graph.Switches[*ParentIndex].Kind)));
				}
				else if (Switch.ParentOption < 0 || Switch.ParentOption >= Graph.Switches[*ParentIndex].OptionCount)
				{
					AddIssue({ NodeId, ParentId }, FString::Printf(TEXT("Switch %s hangs on option %d of %s, which only has options 0..%d."), *NodeId, Switch.ParentOption, *ParentId, Graph.Switches[*ParentIndex].OptionCount - 1));
				}
			}
			if (Switch.Kind == EHTNestedToggleSwitchKind::Texture)
			{
				if (Trimmed(Switch.TextureParameterName).IsEmpty())
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Texture switch %s has no texture parameter."), *NodeId));
				}
				if (Switch.TextureTargets.IsEmpty())
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Texture switch %s has no target. Connect a part to one of its Target inputs."), *NodeId));
				}
			}
			else if (Switch.Kind == EHTNestedToggleSwitchKind::Material && Switch.MaterialIDs.IsEmpty())
			{
				TArray<FString> Ids = { NodeId };
				Ids.Append(Switch.TargetPartNodeIds);
				AddIssue(Ids, FString::Printf(TEXT("Material switch %s has no material slot. Connect a part with material IDs to one of its Slots inputs."), *NodeId));
			}
		}
		if (!OutErrors.IsEmpty())
		{
			return false;
		}

		auto ParentIndexOf = [&Graph, &SwitchIndexById](const int32 Index) -> int32
		{
			const FString ParentId = Trimmed(Graph.Switches[Index].ParentNodeId);
			const int32* ParentIndex = ParentId.IsEmpty() ? nullptr : SwitchIndexById.Find(ParentId);
			return ParentIndex ? *ParentIndex : INDEX_NONE;
		};
		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			TArray<int32> Chain = { Index };
			int32 Cursor = ParentIndexOf(Index);
			while (Cursor != INDEX_NONE)
			{
				if (Chain.Contains(Cursor))
				{
					TArray<FString> Ids;
					for (const int32 ChainIndex : Chain)
					{
						Ids.Add(Trimmed(Graph.Switches[ChainIndex].NodeId));
					}
					AddIssue(Ids, FString::Printf(TEXT("Switches %s form a parent cycle."), *JoinNodeIds(Ids)));
					break;
				}
				Chain.Add(Cursor);
				Cursor = ParentIndexOf(Cursor);
			}
		}
		if (!OutErrors.IsEmpty())
		{
			return false;
		}

		// Switches that share a key or an explicit variable name share one state variable.
		FNestedUnionFind Groups(SwitchCount);
		TMap<FString, int32> FirstByKey;
		TMap<FString, int32> FirstByName;
		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			const FString CanonicalKey = MakeNestedCanonicalKey(KeySpecs[Index]);
			if (const int32* First = FirstByKey.Find(CanonicalKey))
			{
				Groups.Union(*First, Index);
			}
			else
			{
				FirstByKey.Add(CanonicalKey, Index);
			}
			if (!ExplicitNames[Index].IsEmpty())
			{
				if (const int32* First = FirstByName.Find(ExplicitNames[Index]))
				{
					Groups.Union(*First, Index);
				}
				else
				{
					FirstByName.Add(ExplicitNames[Index], Index);
				}
			}
		}

		TMap<int32, int32> VariableIndexByRoot;
		OutCompiled.SwitchVariableIndices.SetNum(SwitchCount);
		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			const int32 Root = Groups.Find(Index);
			if (const int32* Existing = VariableIndexByRoot.Find(Root))
			{
				OutCompiled.SwitchVariableIndices[Index] = *Existing;
				OutCompiled.Variables[*Existing].SwitchIndices.Add(Index);
				continue;
			}
			const int32 VariableIndex = OutCompiled.Variables.AddDefaulted();
			VariableIndexByRoot.Add(Root, VariableIndex);
			OutCompiled.SwitchVariableIndices[Index] = VariableIndex;
			OutCompiled.Variables[VariableIndex].SwitchIndices.Add(Index);
		}

		TMap<FName, int32> VariableIndexByName;
		for (int32 VariableIndex = 0; VariableIndex < OutCompiled.Variables.Num(); ++VariableIndex)
		{
			FHTNestedToggleStateVariable& Variable = OutCompiled.Variables[VariableIndex];
			TArray<FString> MemberIds;
			TArray<FString> DistinctExplicitNames;
			TArray<int32> DistinctOptionCounts;
			TArray<int32> DistinctInitialOptions;
			TArray<FString> DistinctCanonicalKeys;
			for (const int32 SwitchIndex : Variable.SwitchIndices)
			{
				const FHTNestedToggleSwitch& Switch = Graph.Switches[SwitchIndex];
				MemberIds.Add(Trimmed(Switch.NodeId));
				if (!ExplicitNames[SwitchIndex].IsEmpty())
				{
					DistinctExplicitNames.AddUnique(ExplicitNames[SwitchIndex]);
				}
				DistinctOptionCounts.AddUnique(Switch.OptionCount);
				DistinctInitialOptions.AddUnique(Switch.InitialOption);
				const FString CanonicalKey = MakeNestedCanonicalKey(KeySpecs[SwitchIndex]);
				if (!DistinctCanonicalKeys.Contains(CanonicalKey))
				{
					DistinctCanonicalKeys.Add(CanonicalKey);
					Variable.KeyNames.Add(Switch.KeyName);
				}
			}
			const FString SharedBy = DistinctCanonicalKeys.Num() == 1
				? FString::Printf(TEXT("key %s"), *DistinctCanonicalKeys[0])
				: FString(TEXT("a variable name"));
			if (DistinctExplicitNames.Num() > 1)
			{
				AddIssue(MemberIds, FString::Printf(TEXT("Switches %s share %s and therefore one state variable, but declare different variable names: %s."), *JoinNodeIds(MemberIds), *SharedBy, *FString::Join(DistinctExplicitNames, TEXT(", "))));
			}
			if (DistinctOptionCounts.Num() > 1)
			{
				AddIssue(MemberIds, FString::Printf(TEXT("Switches %s share %s and must have the same option count (found %s)."), *JoinNodeIds(MemberIds), *SharedBy, *FString::JoinBy(DistinctOptionCounts, TEXT(", "), [](const int32 Count) { return FString::FromInt(Count); })));
			}
			if (DistinctInitialOptions.Num() > 1)
			{
				AddIssue(MemberIds, FString::Printf(TEXT("Switches %s share %s and must have the same initial option (found %s)."), *JoinNodeIds(MemberIds), *SharedBy, *FString::JoinBy(DistinctInitialOptions, TEXT(", "), [](const int32 Option) { return FString::FromInt(Option); })));
			}
			const FHTNestedToggleSwitch& FirstSwitch = Graph.Switches[Variable.SwitchIndices[0]];
			Variable.OptionCount = FirstSwitch.OptionCount;
			Variable.InitialOption = FirstSwitch.InitialOption;
			FString VariableName = DistinctExplicitNames.Num() > 0 ? DistinctExplicitNames[0] : FString();
			if (VariableName.IsEmpty())
			{
				// Unnamed switches are grouped by key, so the key is their stable, readable name.
				FString Suffix = SanitizeNestedIdentifier(DistinctCanonicalKeys.Num() > 0 ? DistinctCanonicalKeys[0] : FString());
				if (Suffix.IsEmpty())
				{
					Suffix = FString::Printf(TEXT("Switch%d"), Variable.SwitchIndices[0] + 1);
				}
				VariableName = OutCompiled.Name + TEXT("_") + Suffix;
			}
			Variable.VariableName = FName(*VariableName);
			if (const int32* Other = VariableIndexByName.Find(Variable.VariableName))
			{
				TArray<FString> Ids = MemberIds;
				for (const int32 SwitchIndex : OutCompiled.Variables[*Other].SwitchIndices)
				{
					Ids.Add(Trimmed(Graph.Switches[SwitchIndex].NodeId));
				}
				AddIssue(Ids, FString::Printf(TEXT("Switches %s resolve to the same variable name %s but do not share a key. Give them the same key or different variable names."), *JoinNodeIds(Ids), *VariableName));
			}
			else
			{
				VariableIndexByName.Add(Variable.VariableName, VariableIndex);
			}
		}

		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			for (int32 Cursor = ParentIndexOf(Index); Cursor != INDEX_NONE; Cursor = ParentIndexOf(Cursor))
			{
				if (OutCompiled.SwitchVariableIndices[Cursor] == OutCompiled.SwitchVariableIndices[Index])
				{
					const FString NodeId = Trimmed(Graph.Switches[Index].NodeId);
					const FString AncestorId = Trimmed(Graph.Switches[Cursor].NodeId);
					AddIssue({ NodeId, AncestorId }, FString::Printf(TEXT("Switch %s shares a state variable with its ancestor %s, so its parts could never show. Use a different key."), *NodeId, *AncestorId));
					break;
				}
			}
		}

		TMap<int32, FString> PartIdByMaterialID;
		TMap<FString, int32> PartIndexById;
		for (int32 PartIndex = 0; PartIndex < Graph.Parts.Num(); ++PartIndex)
		{
			const FHTNestedTogglePart& Part = Graph.Parts[PartIndex];
			const FString NodeId = Trimmed(Part.NodeId);
			if (NodeId.IsEmpty())
			{
				AddIssue({}, FString::Printf(TEXT("Part %d has no node id."), PartIndex + 1));
				continue;
			}
			if (PartIndexById.Contains(NodeId))
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Part node id %s is used more than once."), *NodeId));
				continue;
			}
			PartIndexById.Add(NodeId, PartIndex);

			FHTNestedToggleCompiledPart Compiled;
			Compiled.PartIndex = PartIndex;
			if (Part.MaterialIDs.IsEmpty())
			{
				AddIssue({ NodeId }, FString::Printf(TEXT("Part %s has no material ID."), *NodeId));
			}
			for (const int32 MaterialID : Part.MaterialIDs)
			{
				if (MaterialID < 0)
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Part %s has a negative material ID %d."), *NodeId, MaterialID));
					continue;
				}
				if (Compiled.MaterialIDs.Contains(MaterialID))
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Part %s lists material ID %d twice."), *NodeId, MaterialID));
					continue;
				}
				if (const FString* OtherPart = PartIdByMaterialID.Find(MaterialID))
				{
					AddIssue({ *OtherPart, NodeId }, FString::Printf(TEXT("Material ID %d is connected in parts %s and %s. A part can only be reached through one path."), MaterialID, **OtherPart, *NodeId));
					continue;
				}
				PartIdByMaterialID.Add(MaterialID, NodeId);
				Compiled.MaterialIDs.Add(MaterialID);
			}

			bool bPathValid = true;
			for (int32 StepIndex = 0; StepIndex < Part.Path.Num() && bPathValid; ++StepIndex)
			{
				const FHTNestedTogglePathStep& Step = Part.Path[StepIndex];
				const FString StepId = Trimmed(Step.SwitchNodeId);
				const int32* SwitchIndex = SwitchIndexById.Find(StepId);
				if (!SwitchIndex)
				{
					AddIssue({ NodeId }, FString::Printf(TEXT("Part %s path refers to unknown switch %s."), *NodeId, *StepId));
					bPathValid = false;
					break;
				}
				const FHTNestedToggleSwitch& Switch = Graph.Switches[*SwitchIndex];
				if (Switch.Kind != EHTNestedToggleSwitchKind::Visibility)
				{
					AddIssue({ NodeId, StepId }, FString::Printf(TEXT("Part %s hangs on %s, a %s. Connect parts to its Target or Slots inputs instead."), *NodeId, *StepId, NestedSwitchKindName(Switch.Kind)));
					bPathValid = false;
					break;
				}
				if (Step.Option < 0 || Step.Option >= Switch.OptionCount)
				{
					AddIssue({ NodeId, StepId }, FString::Printf(TEXT("Part %s hangs on option %d of switch %s, which only has options 0..%d."), *NodeId, Step.Option, *StepId, Switch.OptionCount - 1));
					bPathValid = false;
					break;
				}
				const int32 ExpectedParent = StepIndex == 0 ? INDEX_NONE : SwitchIndexById.FindChecked(Trimmed(Part.Path[StepIndex - 1].SwitchNodeId));
				const int32 ActualParent = ParentIndexOf(*SwitchIndex);
				const bool bParentMatches = ActualParent == ExpectedParent
					&& (ExpectedParent == INDEX_NONE || Switch.ParentOption == Part.Path[StepIndex - 1].Option);
				if (!bParentMatches)
				{
					AddIssue({ NodeId, StepId }, FString::Printf(TEXT("Part %s path step %d (%s option %d) does not follow the switch's parent attachment."), *NodeId, StepIndex + 1, *StepId, Step.Option));
					bPathValid = false;
					break;
				}
				FHTNestedToggleCondition& Condition = Compiled.Conditions.AddDefaulted_GetRef();
				Condition.VariableIndex = OutCompiled.SwitchVariableIndices[*SwitchIndex];
				Condition.Option = Step.Option;
			}
			if (bPathValid)
			{
				OutCompiled.Parts.Add(MoveTemp(Compiled));
			}
		}

		// Texture and Material switches: one effect per slot, gated by the switch's parent path.
		auto ConditionsOfSwitch = [&](const int32 Index)
		{
			TArray<FHTNestedToggleCondition> Conditions;
			int32 Child = Index;
			for (int32 Cursor = ParentIndexOf(Index); Cursor != INDEX_NONE; Cursor = ParentIndexOf(Cursor))
			{
				FHTNestedToggleCondition Condition;
				Condition.VariableIndex = OutCompiled.SwitchVariableIndices[Cursor];
				Condition.Option = Graph.Switches[Child].ParentOption;
				Conditions.Insert(Condition, 0);
				Child = Cursor;
			}
			return Conditions;
		};
		for (const TPair<int32, FString>& Slot : Graph.SlotMaterialPaths)
		{
			OutCompiled.SlotMaterialPaths.Add(Slot.Key, Trimmed(Slot.Value));
		}
		TSet<int32> EffectSlots;
		auto OriginalOf = [&OutCompiled](const int32 MaterialID)
		{
			const FString* Path = OutCompiled.SlotMaterialPaths.Find(MaterialID);
			return Path ? *Path : FString();
		};
		for (int32 Index = 0; Index < SwitchCount; ++Index)
		{
			const FHTNestedToggleSwitch& Switch = Graph.Switches[Index];
			if (Switch.Kind == EHTNestedToggleSwitchKind::Visibility)
			{
				continue;
			}
			const FString NodeId = Trimmed(Switch.NodeId);
			const TArray<FHTNestedToggleCondition> Conditions = ConditionsOfSwitch(Index);
			const int32 VariableIndex = OutCompiled.SwitchVariableIndices[Index];
			if (Switch.Kind == EHTNestedToggleSwitchKind::Material)
			{
				TArray<int32> Slots;
				for (const int32 MaterialID : Switch.MaterialIDs)
				{
					if (MaterialID < 0)
					{
						AddIssue({ NodeId }, FString::Printf(TEXT("Material switch %s has a negative material ID %d."), *NodeId, MaterialID));
						continue;
					}
					Slots.AddUnique(MaterialID);
				}
				Slots.Sort();
				for (const int32 MaterialID : Slots)
				{
					const FString Original = OriginalOf(MaterialID);
					if (Original.IsEmpty())
					{
						AddIssue({ NodeId }, FString::Printf(TEXT("Material switch %s acts on material ID %d, which has no material on the preview mesh."), *NodeId, MaterialID));
						continue;
					}
					FHTNestedToggleCompiledMaterialEffect& Effect = OutCompiled.MaterialEffects.AddDefaulted_GetRef();
					Effect.SwitchIndex = Index;
					Effect.VariableIndex = VariableIndex;
					Effect.Conditions = Conditions;
					Effect.MaterialID = MaterialID;
					for (int32 Option = 0; Option < Switch.OptionCount; ++Option)
					{
						const FString Path = Switch.OptionMaterialPaths.IsValidIndex(Option) ? Trimmed(Switch.OptionMaterialPaths[Option]) : FString();
						Effect.OptionMaterialPaths.Add(Path.IsEmpty() ? Original : Path);
					}
					EffectSlots.Add(MaterialID);
				}
				continue;
			}
			const FName Parameter(*Trimmed(Switch.TextureParameterName));
			for (int32 TargetIndex = 0; TargetIndex < Switch.TextureTargets.Num(); ++TargetIndex)
			{
				const FHTNestedToggleTextureTarget& Target = Switch.TextureTargets[TargetIndex];
				const FString PartId = Trimmed(Target.PartNodeId);
				TArray<FString> Ids = { NodeId };
				if (!PartId.IsEmpty())
				{
					Ids.Add(PartId);
				}
				if (Target.MaterialIDs.IsEmpty())
				{
					AddIssue(Ids, FString::Printf(TEXT("Texture switch %s target %d (%s) has no material ID."), *NodeId, TargetIndex + 1, *PartId));
				}
				TArray<int32> Seen;
				for (const int32 MaterialID : Target.MaterialIDs)
				{
					if (MaterialID < 0)
					{
						AddIssue(Ids, FString::Printf(TEXT("Texture switch %s target %d has a negative material ID %d."), *NodeId, TargetIndex + 1, MaterialID));
						continue;
					}
					if (Seen.Contains(MaterialID))
					{
						continue;
					}
					Seen.Add(MaterialID);
					const FString Original = OriginalOf(MaterialID);
					if (Original.IsEmpty())
					{
						AddIssue(Ids, FString::Printf(TEXT("Texture switch %s acts on material ID %d, which has no material on the preview mesh."), *NodeId, MaterialID));
						continue;
					}
					const FString ExplicitParent = Trimmed(Target.ParentMaterialPath);
					FHTNestedToggleCompiledTextureEffect& Effect = OutCompiled.TextureEffects.AddDefaulted_GetRef();
					Effect.SwitchIndex = Index;
					Effect.TargetIndex = TargetIndex;
					Effect.VariableIndex = VariableIndex;
					Effect.Conditions = Conditions;
					Effect.MaterialID = MaterialID;
					Effect.ParameterName = Parameter;
					Effect.ParentMaterialPath = ExplicitParent.IsEmpty() ? Original : ExplicitParent;
					for (int32 Option = 0; Option < Switch.OptionCount; ++Option)
					{
						Effect.TexturePaths.Add(Target.TexturePaths.IsValidIndex(Option) ? Trimmed(Target.TexturePaths[Option]) : FString());
					}
					EffectSlots.Add(MaterialID);
				}
			}
		}

		// Two effects on one slot (one parameter of one material, for textures) must never be
		// active together; mutually exclusive parent paths are fine.
		TSet<FString> ReportedPairs;
		auto SwitchId = [&Graph](const int32 Index) { return Trimmed(Graph.Switches[Index].NodeId); };
		for (int32 First = 0; First < OutCompiled.MaterialEffects.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < OutCompiled.MaterialEffects.Num(); ++Second)
			{
				const FHTNestedToggleCompiledMaterialEffect& A = OutCompiled.MaterialEffects[First];
				const FHTNestedToggleCompiledMaterialEffect& B = OutCompiled.MaterialEffects[Second];
				if (A.MaterialID != B.MaterialID || A.SwitchIndex == B.SwitchIndex || AreNestedConditionsExclusive(A.Conditions, B.Conditions))
				{
					continue;
				}
				const FString Pair = FString::Printf(TEXT("M%d-%d"), A.SwitchIndex, B.SwitchIndex);
				if (!ReportedPairs.Contains(Pair))
				{
					ReportedPairs.Add(Pair);
					AddIssue({ SwitchId(A.SwitchIndex), SwitchId(B.SwitchIndex) }, FString::Printf(TEXT("Material ID %d is switched by Material switches %s and %s, which can be active at the same time. Hang them on different options of one switch, or remove the slot from one of them."), A.MaterialID, *SwitchId(A.SwitchIndex), *SwitchId(B.SwitchIndex)));
				}
			}
		}
		for (int32 First = 0; First < OutCompiled.TextureEffects.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < OutCompiled.TextureEffects.Num(); ++Second)
			{
				const FHTNestedToggleCompiledTextureEffect& A = OutCompiled.TextureEffects[First];
				const FHTNestedToggleCompiledTextureEffect& B = OutCompiled.TextureEffects[Second];
				if (A.MaterialID != B.MaterialID || A.ParameterName != B.ParameterName
					|| !A.ParentMaterialPath.Equals(B.ParentMaterialPath, ESearchCase::IgnoreCase)
					|| AreNestedConditionsExclusive(A.Conditions, B.Conditions))
				{
					continue;
				}
				const FString Pair = FString::Printf(TEXT("T%d.%d-%d.%d"), A.SwitchIndex, A.TargetIndex, B.SwitchIndex, B.TargetIndex);
				if (ReportedPairs.Contains(Pair))
				{
					continue;
				}
				ReportedPairs.Add(Pair);
				const FHTNestedToggleSwitch& SwitchA = Graph.Switches[A.SwitchIndex];
				if (A.SwitchIndex == B.SwitchIndex)
				{
					const FString PartA = Trimmed(SwitchA.TextureTargets[A.TargetIndex].PartNodeId);
					const FString PartB = Trimmed(SwitchA.TextureTargets[B.TargetIndex].PartNodeId);
					AddIssue({ SwitchId(A.SwitchIndex), PartA, PartB }, FString::Printf(TEXT("Texture switch %s sets %s on material ID %d through two targets (%s and %s). Each slot can only be one target."), *SwitchId(A.SwitchIndex), *A.ParameterName.ToString(), A.MaterialID, *PartA, *PartB));
				}
				else
				{
					AddIssue({ SwitchId(A.SwitchIndex), SwitchId(B.SwitchIndex) }, FString::Printf(TEXT("Material ID %d parameter %s is set by Texture switches %s and %s, which can be active at the same time. Hang them on different options of one switch, or use different parameters."), A.MaterialID, *A.ParameterName.ToString(), *SwitchId(A.SwitchIndex), *SwitchId(B.SwitchIndex)));
				}
			}
		}
		OutCompiled.EffectMaterialIDs = EffectSlots.Array();
		OutCompiled.EffectMaterialIDs.Sort();

		if (!OutErrors.IsEmpty())
		{
			OutCompiled.Parts.Empty();
			OutCompiled.MaterialEffects.Empty();
			OutCompiled.TextureEffects.Empty();
			OutCompiled.EffectMaterialIDs.Empty();
			return false;
		}
		return true;
	}

	// Same recovery strategy as RemoveTextureTunerGroup, keyed by an arbitrary comment prefix.
	static void RemoveManagedCommentGroups(UEdGraph* Graph, const FString& Prefix)
	{
		if (!Graph)
		{
			return;
		}
		TArray<UEdGraphNode_Comment*> Comments;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node); Comment && Comment->NodeComment.StartsWith(Prefix))
			{
				Comments.Add(Comment);
			}
		}
		for (UEdGraphNode_Comment* Comment : Comments)
		{
			TArray<UEdGraphNode*> Members = GetNodesUnderComment(Comment);
			// Comment membership is editor-only and is not serialized. Recover the
			// generated group from its saved bounds when reopening an AnimBP.
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Node->IsA<UEdGraphNode_Comment>()
					|| (Node->IsA<UK2Node_Event>() && !Node->IsA<UK2Node_CustomEvent>())
					|| Node->IsA<UK2Node_ExecutionSequence>())
				{
					continue; // Shared animation entry/dispatch nodes are never generated by a tree.
				}
				if (Node->NodePosX >= Comment->NodePosX && Node->NodePosX <= Comment->NodePosX + Comment->NodeWidth
					&& Node->NodePosY >= Comment->NodePosY && Node->NodePosY <= Comment->NodePosY + Comment->NodeHeight)
				{
					Members.AddUnique(Node);
				}
			}
			for (UEdGraphNode* Node : Members)
			{
				Node->BreakAllNodeLinks();
				Graph->RemoveNode(Node);
			}
			Graph->RemoveNode(Comment);
		}
	}

	static void RemoveCustomEventNodes(UEdGraph* Graph, const FName EventName)
	{
		TArray<UEdGraphNode*> Stale;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node); Event && Event->CustomFunctionName == EventName)
			{
				Stale.Add(Event);
			}
		}
		for (UEdGraphNode* Node : Stale)
		{
			Node->BreakAllNodeLinks();
			Graph->RemoveNode(Node);
		}
	}

	struct FNestedGenerationContext
	{
		UBlueprint* AnimBlueprint = nullptr;
		UClass* SaveClass = nullptr;
		UEdGraph* Graph = nullptr;
		FName ApplyEvent;
		FString CharacterName;
		FHTBlueprintToggleGeneratorParams SectionParams;
		TArray<UEdGraphNode*> Nodes;

		void Track(std::initializer_list<UEdGraphNode*> NewNodes)
		{
			for (UEdGraphNode* Node : NewNodes)
			{
				if (Node)
				{
					Nodes.Add(Node);
				}
			}
		}
	};

	static UK2Node_CallFunction* SpawnNestedApplyCall(FNestedGenerationContext& Context, const FVector2D Position, FHTBlueprintToggleGeneratorResult& Result)
	{
		UClass* AnimClass = Context.AnimBlueprint->SkeletonGeneratedClass ? Context.AnimBlueprint->SkeletonGeneratedClass.Get() : Context.AnimBlueprint->GeneratedClass.Get();
		UK2Node_CallFunction* Call = SpawnCall(Context.Graph, AnimClass, Context.ApplyEvent, Position, Result);
		Context.Track({ Call });
		return Call;
	}

	static FName MakeNestedBaseVariable(const FString& TreeName, const int32 MaterialID)
	{
		return FName(*FString::Printf(TEXT("HTN_%s_Base%d"), *TreeName, MaterialID));
	}

	static FName MakeNestedMIDVariable(const FString& TreeName, const int32 MaterialID, const int32 ParentIndex)
	{
		return FName(*FString::Printf(TEXT("HTN_%s_MID%d_%d"), *TreeName, MaterialID, ParentIndex));
	}

	// True for "HTN_<Tree>_Base<digits>" and "HTN_<Tree>_MID<digits>_<digits>" of this tree only.
	static bool IsNestedEffectVariable(const FString& TreeName, const FString& Name)
	{
		auto DigitsThenEnd = [](const FString& Rest, const bool bAllowOneUnderscore)
		{
			int32 Underscores = 0;
			bool bDigitBefore = false;
			for (const TCHAR Character : Rest)
			{
				if (FChar::IsDigit(Character))
				{
					bDigitBefore = true;
				}
				else if (Character == TEXT('_') && bAllowOneUnderscore && bDigitBefore && Underscores == 0)
				{
					++Underscores;
					bDigitBefore = false;
				}
				else
				{
					return false;
				}
			}
			return bDigitBefore && (!bAllowOneUnderscore || Underscores == 1);
		};
		const FString BasePrefix = FString::Printf(TEXT("HTN_%s_Base"), *TreeName);
		const FString MIDPrefix = FString::Printf(TEXT("HTN_%s_MID"), *TreeName);
		if (Name.StartsWith(BasePrefix, ESearchCase::CaseSensitive))
		{
			return DigitsThenEnd(Name.Mid(BasePrefix.Len()), false);
		}
		if (Name.StartsWith(MIDPrefix, ESearchCase::CaseSensitive))
		{
			return DigitsThenEnd(Name.Mid(MIDPrefix.Len()), true);
		}
		return false;
	}

	// Distinct parent materials of the texture effects on one slot, in first-seen order.
	static TArray<FString> GetNestedTextureParents(const FHTNestedToggleCompiledGraph& Compiled, const int32 MaterialID)
	{
		TArray<FString> Parents;
		for (const FHTNestedToggleCompiledTextureEffect& Effect : Compiled.TextureEffects)
		{
			if (Effect.MaterialID == MaterialID && !Parents.ContainsByPredicate([&Effect](const FString& Path) { return Path.Equals(Effect.ParentMaterialPath, ESearchCase::IgnoreCase); }))
			{
				Parents.Add(Effect.ParentMaterialPath);
			}
		}
		return Parents;
	}

	// Every material and texture the effects use, loaded and checked before any asset is touched.
	struct FNestedEffectAssets
	{
		TMap<FString, UMaterialInterface*> Materials;
		TMap<FString, UTexture*> Textures;
		// "<parent path>|<parameter>" -> the parent material's own texture for that parameter.
		TMap<FString, UTexture*> ParameterDefaults;

		UMaterialInterface* Material(const FString& Path) const
		{
			UMaterialInterface* const* Found = Materials.Find(Path);
			return Found ? *Found : nullptr;
		}

		UTexture* Texture(const FString& Path) const
		{
			UTexture* const* Found = Textures.Find(Path);
			return Found ? *Found : nullptr;
		}

		static FString DefaultKey(const FString& ParentPath, const FName Parameter)
		{
			return ParentPath + TEXT("|") + Parameter.ToString();
		}
	};

	static bool LoadNestedEffectAssets(const FHTNestedToggleGraph& Graph, const FHTNestedToggleCompiledGraph& Compiled, FNestedEffectAssets& Out, TArray<FHTNestedToggleIssue>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		auto AddIssue = [&OutErrors](TArray<FString> NodeIds, const FString& Message)
		{
			FHTNestedToggleIssue& Issue = OutErrors.AddDefaulted_GetRef();
			Issue.NodeIds = MoveTemp(NodeIds);
			Issue.Message = Message;
		};
		auto LoadMaterialOnce = [&](const FString& Path, const TArray<FString>& NodeIds, const FString& What) -> UMaterialInterface*
		{
			if (UMaterialInterface* Found = Out.Material(Path))
			{
				return Found;
			}
			FHTBlueprintToggleGeneratorResult Scratch;
			UMaterialInterface* Material = LoadMaterial(Path, Scratch, What);
			if (Material)
			{
				Out.Materials.Add(Path, Material);
			}
			else
			{
				AddIssue(NodeIds, FString::Printf(TEXT("%s could not be loaded: %s"), *What, *Path));
			}
			return Material;
		};
		auto SwitchId = [&Graph](const int32 Index) { return Trimmed(Graph.Switches[Index].NodeId); };
		for (const int32 MaterialID : Compiled.EffectMaterialIDs)
		{
			TArray<FString> NodeIds;
			for (const FHTNestedToggleCompiledMaterialEffect& Effect : Compiled.MaterialEffects)
			{
				if (Effect.MaterialID == MaterialID)
				{
					NodeIds.AddUnique(SwitchId(Effect.SwitchIndex));
				}
			}
			for (const FHTNestedToggleCompiledTextureEffect& Effect : Compiled.TextureEffects)
			{
				if (Effect.MaterialID == MaterialID)
				{
					NodeIds.AddUnique(SwitchId(Effect.SwitchIndex));
				}
			}
			LoadMaterialOnce(Compiled.SlotMaterialPaths.FindRef(MaterialID), NodeIds, FString::Printf(TEXT("The original material of material ID %d"), MaterialID));
		}
		for (const FHTNestedToggleCompiledMaterialEffect& Effect : Compiled.MaterialEffects)
		{
			for (int32 Option = 0; Option < Effect.OptionMaterialPaths.Num(); ++Option)
			{
				LoadMaterialOnce(Effect.OptionMaterialPaths[Option], { SwitchId(Effect.SwitchIndex) }, FString::Printf(TEXT("Material switch %s option %d material"), *SwitchId(Effect.SwitchIndex), Option + 1));
			}
		}
		for (const FHTNestedToggleCompiledTextureEffect& Effect : Compiled.TextureEffects)
		{
			const FHTNestedToggleSwitch& Switch = Graph.Switches[Effect.SwitchIndex];
			const FString PartId = Trimmed(Switch.TextureTargets[Effect.TargetIndex].PartNodeId);
			const TArray<FString> NodeIds = { SwitchId(Effect.SwitchIndex), PartId };
			UMaterialInterface* Parent = LoadMaterialOnce(Effect.ParentMaterialPath, NodeIds, FString::Printf(TEXT("Texture switch %s target material"), *SwitchId(Effect.SwitchIndex)));
			const FString Key = FNestedEffectAssets::DefaultKey(Effect.ParentMaterialPath, Effect.ParameterName);
			if (Parent && !Out.ParameterDefaults.Contains(Key))
			{
				UTexture* Default = nullptr;
				if (Parent->GetTextureParameterValue(FHashedMaterialParameterInfo(FMaterialParameterInfo(Effect.ParameterName)), Default))
				{
					Out.ParameterDefaults.Add(Key, Default);
				}
				else
				{
					AddIssue(NodeIds, FString::Printf(TEXT("Texture switch %s: material %s (material ID %d, part %s) has no texture parameter %s."), *SwitchId(Effect.SwitchIndex), *Parent->GetName(), Effect.MaterialID, *PartId, *Effect.ParameterName.ToString()));
				}
			}
			for (int32 Option = 0; Option < Effect.TexturePaths.Num(); ++Option)
			{
				const FString& Path = Effect.TexturePaths[Option];
				if (Path.IsEmpty() || Out.Textures.Contains(Path))
				{
					continue;
				}
				FHTBlueprintToggleGeneratorResult Scratch;
				if (UTexture* Texture = LoadTexture(Path, Scratch, TEXT("Texture")))
				{
					Out.Textures.Add(Path, Texture);
				}
				else
				{
					AddIssue(NodeIds, FString::Printf(TEXT("Texture switch %s option %d texture could not be loaded: %s"), *SwitchId(Effect.SwitchIndex), Option + 1, *Path));
				}
			}
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	// Exec outputs that all continue into the next node (Switch cases, Branch exits, ...).
	struct FNestedExecFlow
	{
		TArray<UEdGraphPin*> Pending;

		bool Enter(UEdGraphNode* Node, FHTBlueprintToggleGeneratorResult& Result, const TCHAR* Label)
		{
			UEdGraphPin* In = Node ? GetSchema()->FindExecutionPin(*Node, EGPD_Input) : nullptr;
			bool bConnected = In != nullptr;
			for (UEdGraphPin* Pin : Pending)
			{
				bConnected &= Connect(Pin, In, Result, Label);
			}
			Pending.Reset();
			return bConnected;
		}

		bool Through(UEdGraphNode* Node, FHTBlueprintToggleGeneratorResult& Result, const TCHAR* Label)
		{
			if (!Enter(Node, Result, Label))
			{
				return false;
			}
			Pending.Add(GetSchema()->FindExecutionPin(*Node, EGPD_Output));
			return true;
		}
	};

	// AND(variable == option) over Conditions; nullptr when there are none (always true).
	static UEdGraphPin* BuildNestedConditionPin(
		FNestedGenerationContext& Context,
		const FHTNestedToggleCompiledGraph& Compiled,
		const TArray<FHTNestedToggleCondition>& Conditions,
		const FVector2D Base,
		bool& bOutOk,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		bOutOk = true;
		TArray<UEdGraphPin*> Pins;
		for (int32 Index = 0; Index < Conditions.Num(); ++Index)
		{
			const FName VariableName = Compiled.Variables[Conditions[Index].VariableIndex].VariableName;
			UK2Node_VariableGet* GetVariable = SpawnVariableGet(Context.Graph, VariableName, nullptr, Base + FVector2D(0.0f, 140.0f + Index * 150.0f));
			UK2Node_CallFunction* Equal = SpawnCall(Context.Graph, UKismetMathLibrary::StaticClass(), TEXT("EqualEqual_IntInt"), Base + FVector2D(260.0f, 120.0f + Index * 150.0f), Result);
			if (!GetVariable || !Equal)
			{
				bOutOk = false;
				return nullptr;
			}
			Context.Track({ GetVariable, Equal });
			SetDefaultValue(FindAnyPin(Equal, TEXT("B")), FString::FromInt(Conditions[Index].Option));
			bOutOk &= Connect(FindAnyPin(GetVariable, VariableName), FindAnyPin(Equal, TEXT("A")), Result, TEXT("Toggle tree effect state -> Equal"));
			Pins.Add(FindAnyPin(Equal, UEdGraphSchema_K2::PN_ReturnValue));
		}
		if (Pins.Num() <= 1)
		{
			return Pins.IsEmpty() ? nullptr : Pins[0];
		}
		UK2Node_CommutativeAssociativeBinaryOperator* And = SpawnBooleanOperator(Context.Graph, TEXT("BooleanAND"), Base + FVector2D(520.0f, 140.0f), Pins.Num(), Result);
		if (!And)
		{
			bOutOk = false;
			return nullptr;
		}
		Context.Track({ And });
		for (int32 Index = 0; Index < Pins.Num(); ++Index)
		{
			bOutOk &= Connect(Pins[Index], And->GetInputPin(Index), Result, TEXT("Toggle tree effect condition -> AND"));
		}
		return FindAnyPin(And, UEdGraphSchema_K2::PN_ReturnValue);
	}

	// Lets the flow pass only while Conditions hold; the false exit goes to OutSkipped.
	static bool BuildNestedGate(
		FNestedGenerationContext& Context,
		const FHTNestedToggleCompiledGraph& Compiled,
		const TArray<FHTNestedToggleCondition>& Conditions,
		FNestedExecFlow& Flow,
		TArray<UEdGraphPin*>& OutSkipped,
		const FVector2D Base,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		if (Conditions.IsEmpty())
		{
			return true;
		}
		bool bOk = true;
		UEdGraphPin* Condition = BuildNestedConditionPin(Context, Compiled, Conditions, Base, bOk, Result);
		UK2Node_IfThenElse* Branch = SpawnBranch(Context.Graph, Base + FVector2D(Conditions.Num() > 1 ? 760.0f : 520.0f, 0.0f));
		if (!bOk || !Condition || !Branch)
		{
			return false;
		}
		Context.Track({ Branch });
		bOk &= Connect(Condition, FindAnyPin(Branch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("Toggle tree effect condition -> Branch"));
		bOk &= Flow.Enter(Branch, Result, TEXT("Toggle tree effect -> Branch"));
		Flow.Pending.Add(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Then));
		OutSkipped.Add(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Else));
		return bOk;
	}

	// One managed slot: base = original, Material switches replace the base, Texture switches
	// whose material is the current base write into a per (slot, material) MID, then SetMaterial.
	static bool BuildNestedEffectSlot(
		FNestedGenerationContext& Context,
		const FHTNestedToggleCompiledGraph& Compiled,
		const FNestedEffectAssets& Assets,
		const int32 MaterialID,
		UEdGraphPin* OwningPin,
		FNestedExecFlow& Flow,
		const FVector2D Base,
		float& OutHeight,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UEdGraph* Graph = Context.Graph;
		const FName BaseVariable = MakeNestedBaseVariable(Compiled.Name, MaterialID);
		const FString OriginalPath = Compiled.SlotMaterialPaths.FindRef(MaterialID);
		float X = Base.X;
		const float Y = Base.Y;
		float Bottom = Y + 300.0f;
		auto Advance = [&X](const float Width = 380.0f)
		{
			const float Current = X;
			X += Width;
			return Current;
		};
		bool bOk = true;

		UK2Node_VariableSet* SetOriginal = SpawnVariableSet(Graph, BaseVariable, nullptr, FVector2D(Advance(), Y));
		if (!SetOriginal)
		{
			return false;
		}
		Context.Track({ SetOriginal });
		SetDefaultObject(FindAnyPin(SetOriginal, BaseVariable), Assets.Material(OriginalPath));
		bOk &= Flow.Through(SetOriginal, Result, TEXT("Toggle tree slot -> original material"));

		for (const FHTNestedToggleCompiledMaterialEffect& Effect : Compiled.MaterialEffects)
		{
			if (Effect.MaterialID != MaterialID)
			{
				continue;
			}
			TArray<UEdGraphPin*> Skipped;
			bOk &= BuildNestedGate(Context, Compiled, Effect.Conditions, Flow, Skipped, FVector2D(Advance(Effect.Conditions.IsEmpty() ? 0.0f : 1060.0f), Y), Result);
			const FName Variable = Compiled.Variables[Effect.VariableIndex].VariableName;
			const int32 OptionCount = Effect.OptionMaterialPaths.Num();
			UK2Node_VariableGet* GetState = SpawnVariableGet(Graph, Variable, nullptr, FVector2D(X, Y + 220.0f));
			UK2Node_SwitchInteger* Switch = SpawnSwitchOnInt(Graph, FVector2D(Advance(), Y), OptionCount);
			if (!GetState || !Switch)
			{
				return false;
			}
			Context.Track({ GetState, Switch });
			bOk &= Connect(FindAnyPin(GetState, Variable), FindAnyPin(Switch, TEXT("Selection")), Result, TEXT("Material switch state -> Switch"));
			bOk &= Flow.Enter(Switch, Result, TEXT("Toggle tree -> Material switch"));
			const float CaseX = Advance();
			for (int32 Option = 0; Option < OptionCount; ++Option)
			{
				UK2Node_VariableSet* SetMaterialOption = SpawnVariableSet(Graph, BaseVariable, nullptr, FVector2D(CaseX, Y + Option * 150.0f));
				if (!SetMaterialOption)
				{
					return false;
				}
				Context.Track({ SetMaterialOption });
				SetDefaultObject(FindAnyPin(SetMaterialOption, BaseVariable), Assets.Material(Effect.OptionMaterialPaths[Option]));
				bOk &= Connect(FindPin(Switch, FName(*FString::FromInt(Option)), EGPD_Output), GetSchema()->FindExecutionPin(*SetMaterialOption, EGPD_Input), Result, TEXT("Material switch option -> base material"));
				Flow.Pending.Add(GetSchema()->FindExecutionPin(*SetMaterialOption, EGPD_Output));
				Bottom = FMath::Max(Bottom, Y + Option * 150.0f + 200.0f);
			}
			Flow.Pending.Add(Switch->GetDefaultPin());
			Flow.Pending.Append(Skipped);
		}

		const TArray<FString> Parents = GetNestedTextureParents(Compiled, MaterialID);
		for (int32 ParentIndex = 0; ParentIndex < Parents.Num(); ++ParentIndex)
		{
			const FString& ParentPath = Parents[ParentIndex];
			UMaterialInterface* Parent = Assets.Material(ParentPath);
			const FName MIDVariable = MakeNestedMIDVariable(Compiled.Name, MaterialID, ParentIndex);

			// Textures belong to a material: they act only while the slot shows it.
			UK2Node_VariableGet* GetBase = SpawnVariableGet(Graph, BaseVariable, nullptr, FVector2D(X, Y + 220.0f));
			UK2Node_CallFunction* IsParent = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("EqualEqual_ObjectObject"), FVector2D(X + 220.0f, Y + 200.0f), Result);
			UK2Node_IfThenElse* ParentBranch = SpawnBranch(Graph, FVector2D(Advance(520.0f), Y));
			UK2Node_VariableGet* GetMID = SpawnVariableGet(Graph, MIDVariable, nullptr, FVector2D(X, Y + 360.0f));
			UK2Node_CallFunction* MIDValid = SpawnCall(Graph, UKismetSystemLibrary::StaticClass(), TEXT("IsValid"), FVector2D(X + 220.0f, Y + 220.0f), Result);
			UK2Node_IfThenElse* ValidBranch = SpawnBranch(Graph, FVector2D(Advance(520.0f), Y));
			UK2Node_CallFunction* Create = SpawnCall(Graph, UKismetMaterialLibrary::StaticClass(), TEXT("CreateDynamicMaterialInstance"), FVector2D(X, Y + 160.0f), Result);
			UK2Node_VariableSet* SetMID = SpawnVariableSet(Graph, MIDVariable, nullptr, FVector2D(Advance(420.0f) + 420.0f, Y + 160.0f));
			Advance(420.0f);
			if (!GetBase || !IsParent || !ParentBranch || !GetMID || !MIDValid || !ValidBranch || !Create || !SetMID)
			{
				return false;
			}
			Context.Track({ GetBase, IsParent, ParentBranch, GetMID, MIDValid, ValidBranch, Create, SetMID });
			bOk &= Connect(FindAnyPin(GetBase, BaseVariable), FindAnyPin(IsParent, TEXT("A")), Result, TEXT("Slot base -> Equal"));
			SetDefaultObject(FindAnyPin(IsParent, TEXT("B")), Parent);
			bOk &= Connect(FindAnyPin(IsParent, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(ParentBranch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("Slot shows the texture material -> Branch"));
			bOk &= Flow.Enter(ParentBranch, Result, TEXT("Toggle tree -> texture material check"));
			Flow.Pending.Add(FindAnyPin(ParentBranch, UEdGraphSchema_K2::PN_Then));
			const TArray<UEdGraphPin*> OtherMaterial = { FindAnyPin(ParentBranch, UEdGraphSchema_K2::PN_Else) };

			bOk &= Connect(FindAnyPin(GetMID, MIDVariable), FindAnyPin(MIDValid, TEXT("Object")), Result, TEXT("MID -> IsValid"));
			bOk &= Connect(FindAnyPin(MIDValid, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(ValidBranch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("IsValid -> Branch"));
			bOk &= Flow.Enter(ValidBranch, Result, TEXT("Texture material -> MID check"));
			SetDefaultObject(FindAnyPin(Create, TEXT("Parent")), Parent);
			bOk &= Connect(FindAnyPin(ValidBranch, UEdGraphSchema_K2::PN_Else), GetSchema()->FindExecutionPin(*Create, EGPD_Input), Result, TEXT("No MID yet -> Create"));
			bOk &= Connect(GetSchema()->FindExecutionPin(*Create, EGPD_Output), GetSchema()->FindExecutionPin(*SetMID, EGPD_Input), Result, TEXT("Create MID -> Set MID"));
			bOk &= Connect(FindAnyPin(Create, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetMID, MIDVariable), Result, TEXT("Created MID -> MID variable"));
			Flow.Pending.Add(FindAnyPin(ValidBranch, UEdGraphSchema_K2::PN_Then));
			Flow.Pending.Add(GetSchema()->FindExecutionPin(*SetMID, EGPD_Output));

			TArray<FName> Parameters;
			for (const FHTNestedToggleCompiledTextureEffect& Effect : Compiled.TextureEffects)
			{
				if (Effect.MaterialID == MaterialID && Effect.ParentMaterialPath.Equals(ParentPath, ESearchCase::IgnoreCase))
				{
					Parameters.AddUnique(Effect.ParameterName);
				}
			}
			for (const FName Parameter : Parameters)
			{
				// Start from the material's own texture, then the active option overrides it.
				UK2Node_CallFunction* Reset = SpawnCall(Graph, UMaterialInstanceDynamic::StaticClass(), TEXT("SetTextureParameterValue"), FVector2D(Advance(), Y), Result);
				if (!Reset)
				{
					return false;
				}
				Context.Track({ Reset });
				bOk &= Connect(FindAnyPin(GetMID, MIDVariable), FindSelfPin(Reset), Result, TEXT("MID -> reset texture"));
				SetDefaultValue(FindAnyPin(Reset, TEXT("ParameterName")), Parameter.ToString());
				SetDefaultObject(FindAnyPin(Reset, TEXT("Value")), Assets.ParameterDefaults.FindRef(FNestedEffectAssets::DefaultKey(ParentPath, Parameter)));
				bOk &= Flow.Through(Reset, Result, TEXT("Texture material -> reset texture"));
				for (const FHTNestedToggleCompiledTextureEffect& Effect : Compiled.TextureEffects)
				{
					if (Effect.MaterialID != MaterialID || Effect.ParameterName != Parameter || !Effect.ParentMaterialPath.Equals(ParentPath, ESearchCase::IgnoreCase))
					{
						continue;
					}
					TArray<UEdGraphPin*> Skipped;
					bOk &= BuildNestedGate(Context, Compiled, Effect.Conditions, Flow, Skipped, FVector2D(Advance(Effect.Conditions.IsEmpty() ? 0.0f : 1060.0f), Y), Result);
					const FName Variable = Compiled.Variables[Effect.VariableIndex].VariableName;
					UK2Node_VariableGet* GetState = SpawnVariableGet(Graph, Variable, nullptr, FVector2D(X, Y + 220.0f));
					UK2Node_SwitchInteger* Switch = SpawnSwitchOnInt(Graph, FVector2D(Advance(), Y), Effect.TexturePaths.Num());
					if (!GetState || !Switch)
					{
						return false;
					}
					Context.Track({ GetState, Switch });
					bOk &= Connect(FindAnyPin(GetState, Variable), FindAnyPin(Switch, TEXT("Selection")), Result, TEXT("Texture switch state -> Switch"));
					bOk &= Flow.Enter(Switch, Result, TEXT("Toggle tree -> Texture switch"));
					const float CaseX = Advance();
					for (int32 Option = 0; Option < Effect.TexturePaths.Num(); ++Option)
					{
						UEdGraphPin* CasePin = FindPin(Switch, FName(*FString::FromInt(Option)), EGPD_Output);
						if (Effect.TexturePaths[Option].IsEmpty())
						{
							Flow.Pending.Add(CasePin);
							continue;
						}
						UK2Node_CallFunction* SetTexture = SpawnCall(Graph, UMaterialInstanceDynamic::StaticClass(), TEXT("SetTextureParameterValue"), FVector2D(CaseX, Y + Option * 170.0f), Result);
						if (!SetTexture)
						{
							return false;
						}
						Context.Track({ SetTexture });
						bOk &= Connect(FindAnyPin(GetMID, MIDVariable), FindSelfPin(SetTexture), Result, TEXT("MID -> option texture"));
						SetDefaultValue(FindAnyPin(SetTexture, TEXT("ParameterName")), Parameter.ToString());
						SetDefaultObject(FindAnyPin(SetTexture, TEXT("Value")), Assets.Texture(Effect.TexturePaths[Option]));
						bOk &= Connect(CasePin, GetSchema()->FindExecutionPin(*SetTexture, EGPD_Input), Result, TEXT("Texture switch option -> texture"));
						Flow.Pending.Add(GetSchema()->FindExecutionPin(*SetTexture, EGPD_Output));
						Bottom = FMath::Max(Bottom, Y + Option * 170.0f + 260.0f);
					}
					Flow.Pending.Add(Switch->GetDefaultPin());
					Flow.Pending.Append(Skipped);
				}
			}
			UK2Node_VariableSet* ShowMID = SpawnVariableSet(Graph, BaseVariable, nullptr, FVector2D(Advance(), Y));
			if (!ShowMID)
			{
				return false;
			}
			Context.Track({ ShowMID });
			bOk &= Connect(FindAnyPin(GetMID, MIDVariable), FindAnyPin(ShowMID, BaseVariable), Result, TEXT("MID -> slot base"));
			bOk &= Flow.Through(ShowMID, Result, TEXT("Texture material -> slot shows the MID"));
			Flow.Pending.Append(OtherMaterial);
		}

		UK2Node_VariableGet* GetFinal = SpawnVariableGet(Graph, BaseVariable, nullptr, FVector2D(X, Y + 220.0f));
		UK2Node_CallFunction* SetMaterial = SpawnCall(Graph, UPrimitiveComponent::StaticClass(), TEXT("SetMaterial"), FVector2D(Advance(), Y), Result);
		if (!GetFinal || !SetMaterial)
		{
			return false;
		}
		Context.Track({ GetFinal, SetMaterial });
		bOk &= Connect(OwningPin, FindSelfPin(SetMaterial), Result, TEXT("Owning component -> SetMaterial"));
		SetDefaultValue(FindAnyPin(SetMaterial, TEXT("ElementIndex")), FString::FromInt(MaterialID));
		bOk &= Connect(FindAnyPin(GetFinal, BaseVariable), FindAnyPin(SetMaterial, TEXT("Material")), Result, TEXT("Slot base -> SetMaterial"));
		bOk &= Flow.Through(SetMaterial, Result, TEXT("Toggle tree slot -> SetMaterial"));
		OutHeight = Bottom - Y + 200.0f;
		return bOk;
	}

	// HTN_Apply_<Name>: Material switches, then Texture switches, then visibility: every managed
	// material ID gets visible = AND(variable == option) over its path.
	static bool BuildNestedApplyEvent(
		FNestedGenerationContext& Context,
		const FHTNestedToggleCompiledGraph& Compiled,
		const FNestedEffectAssets& Assets,
		const FVector2D Base,
		float& OutHeight,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UEdGraph* Graph = Context.Graph;
		UK2Node_CustomEvent* Event = SpawnCustomEvent(Graph, Context.ApplyEvent, Base);
		UK2Node_CallFunction* Owning = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), Base + FVector2D(0, 200), Result);
		if (!Event || !Owning)
		{
			return false;
		}
		Context.Track({ Event, Owning });
		UEdGraphPin* OwningPin = FindAnyPin(Owning, UEdGraphSchema_K2::PN_ReturnValue);
		FNestedExecFlow Flow;
		Flow.Pending.Add(GetSchema()->FindExecutionPin(*Event, EGPD_Output));
		float RowY = Base.Y + 420.0f;
		for (const int32 MaterialID : Compiled.EffectMaterialIDs)
		{
			float SlotHeight = 0.0f;
			if (!BuildNestedEffectSlot(Context, Compiled, Assets, MaterialID, OwningPin, Flow, FVector2D(Base.X, RowY), SlotHeight, Result))
			{
				Result.Errors.Add(FString::Printf(TEXT("Could not build the material and texture nodes for material ID %d."), MaterialID));
				return false;
			}
			RowY += SlotHeight;
		}
		for (const FHTNestedToggleCompiledPart& Part : Compiled.Parts)
		{
			TArray<UEdGraphPin*> ConditionPins;
			for (int32 ConditionIndex = 0; ConditionIndex < Part.Conditions.Num(); ++ConditionIndex)
			{
				const FHTNestedToggleCondition& Condition = Part.Conditions[ConditionIndex];
				const FName VariableName = Compiled.Variables[Condition.VariableIndex].VariableName;
				const float ConditionY = RowY + ConditionIndex * 170.0f;
				UK2Node_VariableGet* GetVariable = SpawnVariableGet(Graph, VariableName, nullptr, FVector2D(Base.X, ConditionY + 90.0f));
				UK2Node_CallFunction* Equal = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("EqualEqual_IntInt"), FVector2D(Base.X + 300.0f, ConditionY), Result);
				if (!GetVariable || !Equal)
				{
					return false;
				}
				Context.Track({ GetVariable, Equal });
				SetDefaultValue(FindAnyPin(Equal, TEXT("B")), FString::FromInt(Condition.Option));
				Connect(FindAnyPin(GetVariable, VariableName), FindAnyPin(Equal, TEXT("A")), Result, TEXT("Toggle tree state -> Equal"));
				ConditionPins.Add(FindAnyPin(Equal, UEdGraphSchema_K2::PN_ReturnValue));
			}

			UEdGraphPin* VisiblePin = nullptr;
			if (ConditionPins.Num() == 1)
			{
				VisiblePin = ConditionPins[0];
			}
			else if (ConditionPins.Num() > 1)
			{
				UK2Node_CommutativeAssociativeBinaryOperator* And = SpawnBooleanOperator(Graph, TEXT("BooleanAND"), FVector2D(Base.X + 640.0f, RowY + 40.0f), ConditionPins.Num(), Result);
				if (!And)
				{
					return false;
				}
				Context.Track({ And });
				for (int32 ConditionIndex = 0; ConditionIndex < ConditionPins.Num(); ++ConditionIndex)
				{
					Connect(ConditionPins[ConditionIndex], And->GetInputPin(ConditionIndex), Result, TEXT("Toggle tree condition -> AND"));
				}
				VisiblePin = FindAnyPin(And, UEdGraphSchema_K2::PN_ReturnValue);
			}

			for (int32 MaterialIndex = 0; MaterialIndex < Part.MaterialIDs.Num(); ++MaterialIndex)
			{
				UK2Node_CallFunction* Show = SpawnShowMaterialSectionNode(
					Graph,
					FVector2D(Base.X + 980.0f + MaterialIndex * 440.0f, RowY),
					Part.MaterialIDs[MaterialIndex],
					true,
					Context.SectionParams,
					OwningPin,
					Result,
					&Context.Nodes);
				if (!Show)
				{
					return false;
				}
				if (VisiblePin)
				{
					Connect(VisiblePin, FindAnyPin(Show, TEXT("bShow")), Result, TEXT("Toggle tree visible -> Show Material Section"));
				}
				Flow.Through(Show, Result, TEXT("Toggle tree apply chain"));
			}
			RowY += FMath::Max(280.0f, Part.Conditions.Num() * 170.0f + 110.0f);
		}
		OutHeight = RowY - Base.Y;
		return true;
	}

	// One SaveGame restore block per state variable. Every exit (restored, created, cast failed)
	// continues to OutContinuePins so the next block or the final Apply always runs.
	static bool BuildNestedInitVariable(
		FNestedGenerationContext& Context,
		const FHTNestedToggleStateVariable& Variable,
		const TArray<UEdGraphPin*>& EntryPins,
		const FVector2D Base,
		TArray<UEdGraphPin*>& OutContinuePins,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UEdGraph* Graph = Context.Graph;
		UClass* SaveClass = Context.SaveClass;
		const FName SaveVariable(*(Variable.VariableName.ToString() + TEXT("Save")));
		const FString SlotName = MakeNestedSlotName(Variable.VariableName, Context.CharacterName);
		const float TrueY = Base.Y - 120.0f;
		const float FalseY = Base.Y + 420.0f;

		UK2Node_CallFunction* DoesSaveExist = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("DoesSaveGameExist"), Base, Result);
		UK2Node_IfThenElse* Branch = SpawnBranch(Graph, Base + FVector2D(340, 0));
		UK2Node_CallFunction* LoadGame = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("LoadGameFromSlot"), FVector2D(Base.X + 700, TrueY), Result);
		UK2Node_DynamicCast* CastSave = SpawnCast(Graph, SaveClass, FVector2D(Base.X + 1060, TrueY));
		UK2Node_VariableGet* GetSaveValue = SpawnVariableGet(Graph, SaveVariable, SaveClass, FVector2D(Base.X + 1380, TrueY + 150));
		UK2Node_CallFunction* ClampSaved = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Clamp"), FVector2D(Base.X + 1660, TrueY + 150), Result);
		UK2Node_VariableSet* SetAnimValue = SpawnVariableSet(Graph, Variable.VariableName, nullptr, FVector2D(Base.X + 1960, TrueY));
		UK2Node_CallFunction* CreateSave = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("CreateSaveGameObject"), FVector2D(Base.X + 700, FalseY), Result);
		UK2Node_VariableGet* GetAnimDefault = SpawnVariableGet(Graph, Variable.VariableName, nullptr, FVector2D(Base.X + 1060, FalseY + 170));
		UK2Node_VariableSet* SetSaveDefault = SpawnVariableSet(Graph, SaveVariable, SaveClass, FVector2D(Base.X + 1380, FalseY));
		UK2Node_CallFunction* SaveDefaultSlot = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("SaveGameToSlot"), FVector2D(Base.X + 1720, FalseY), Result);
		if (!DoesSaveExist || !Branch || !LoadGame || !CastSave || !GetSaveValue || !ClampSaved || !SetAnimValue || !CreateSave || !GetAnimDefault || !SetSaveDefault || !SaveDefaultSlot)
		{
			return false;
		}
		Context.Track({ DoesSaveExist, Branch, LoadGame, CastSave, GetSaveValue, ClampSaved, SetAnimValue, CreateSave, GetAnimDefault, SetSaveDefault, SaveDefaultSlot });

		ConfigureSaveSlotPins(DoesSaveExist, SlotName);
		ConfigureSaveSlotPins(LoadGame, SlotName);
		ConfigureSaveSlotPins(SaveDefaultSlot, SlotName);
		SetDefaultObject(FindAnyPin(CreateSave, TEXT("SaveGameClass")), SaveClass);
		SetDefaultValue(FindAnyPin(ClampSaved, TEXT("Min")), TEXT("0"));
		SetDefaultValue(FindAnyPin(ClampSaved, TEXT("Max")), FString::FromInt(Variable.OptionCount - 1));

		for (UEdGraphPin* EntryPin : EntryPins)
		{
			Connect(EntryPin, GetSchema()->FindExecutionPin(*DoesSaveExist, EGPD_Input), Result, TEXT("Toggle tree init -> DoesSaveGameExist"));
		}
		Connect(GetSchema()->FindExecutionPin(*DoesSaveExist, EGPD_Output), GetSchema()->FindExecutionPin(*Branch, EGPD_Input), Result, TEXT("DoesSaveGameExist -> Branch"));
		Connect(FindAnyPin(DoesSaveExist, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Branch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("DoesSaveGameExist -> Branch Condition"));
		Connect(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Then), GetSchema()->FindExecutionPin(*LoadGame, EGPD_Input), Result, TEXT("Save exists -> LoadGameFromSlot"));
		Connect(GetSchema()->FindExecutionPin(*LoadGame, EGPD_Output), GetSchema()->FindExecutionPin(*CastSave, EGPD_Input), Result, TEXT("LoadGameFromSlot -> Cast"));
		Connect(FindAnyPin(LoadGame, UEdGraphSchema_K2::PN_ReturnValue), CastSave->GetCastSourcePin(), Result, TEXT("Loaded save -> Cast Object"));
		Connect(CastSave->GetCastResultPin(), FindSelfPin(GetSaveValue), Result, TEXT("Cast Result -> saved state target"));
		Connect(CastSave->GetValidCastPin(), GetSchema()->FindExecutionPin(*SetAnimValue, EGPD_Input), Result, TEXT("Cast valid -> Set state"));
		Connect(FindAnyPin(GetSaveValue, SaveVariable), FindAnyPin(ClampSaved, TEXT("Value")), Result, TEXT("Saved state -> Clamp"));
		Connect(FindAnyPin(ClampSaved, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetAnimValue, Variable.VariableName), Result, TEXT("Clamped state -> Set state"));
		Connect(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Else), GetSchema()->FindExecutionPin(*CreateSave, EGPD_Input), Result, TEXT("No save -> CreateSaveGameObject"));
		Connect(GetSchema()->FindExecutionPin(*CreateSave, EGPD_Output), GetSchema()->FindExecutionPin(*SetSaveDefault, EGPD_Input), Result, TEXT("CreateSave -> Set default"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(SetSaveDefault), Result, TEXT("CreateSave -> Set default target"));
		Connect(FindAnyPin(GetAnimDefault, Variable.VariableName), FindAnyPin(SetSaveDefault, SaveVariable), Result, TEXT("Initial option -> Save default"));
		Connect(GetSchema()->FindExecutionPin(*SetSaveDefault, EGPD_Output), GetSchema()->FindExecutionPin(*SaveDefaultSlot, EGPD_Input), Result, TEXT("Set default -> SaveGameToSlot"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SaveDefaultSlot, TEXT("SaveGameObject")), Result, TEXT("CreateSave -> SaveGameToSlot"));

		OutContinuePins = {
			GetSchema()->FindExecutionPin(*SetAnimValue, EGPD_Output),
			CastSave->GetInvalidCastPin(),
			GetSchema()->FindExecutionPin(*SaveDefaultSlot, EGPD_Output)
		};
		return true;
	}

	// One key handler per (variable, key): pressed && rendered -> (state + 1) % N -> save -> Apply.
	// The parent condition is deliberately not checked, matching LoyalTools. Handlers are chained:
	// every exit of one handler enters the next, so the chain costs one pin on the shared event.
	static bool BuildNestedKeyHandler(
		FNestedGenerationContext& Context,
		const FHTNestedToggleStateVariable& Variable,
		const FString& RawKeyName,
		const TArray<UEdGraphPin*>& EntryPins,
		const FVector2D Base,
		TArray<UEdGraphPin*>& OutContinuePins,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UEdGraph* Graph = Context.Graph;
		UClass* SaveClass = Context.SaveClass;
		FInputKeySpec KeySpec;
		FString KeyError;
		if (!ResolveInputKeySpec(RawKeyName, KeySpec, KeyError))
		{
			Result.Errors.Add(KeyError);
			return false;
		}
		const FName SaveVariable(*(Variable.VariableName.ToString() + TEXT("Save")));
		const FString SlotName = MakeNestedSlotName(Variable.VariableName, Context.CharacterName);
		const float ModifierOffset = KeySpec.HasModifier() ? 300.0f : 0.0f;

		UK2Node_CallFunction* GetController = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("GetPlayerController"), Base + FVector2D(0, 120), Result);
		UK2Node_CallFunction* WasPressed = SpawnCall(Graph, APlayerController::StaticClass(), TEXT("WasInputKeyJustPressed"), Base + FVector2D(340, 60), Result);
		UK2Node_CallFunction* ModifierRightDown = nullptr;
		UK2Node_CallFunction* ModifierLeftDown = nullptr;
		UK2Node_CommutativeAssociativeBinaryOperator* ModifierOr = nullptr;
		if (KeySpec.HasModifier())
		{
			ModifierRightDown = SpawnCall(Graph, APlayerController::StaticClass(), TEXT("IsInputKeyDown"), Base + FVector2D(340, 240), Result);
			ModifierLeftDown = SpawnCall(Graph, APlayerController::StaticClass(), TEXT("IsInputKeyDown"), Base + FVector2D(340, 420), Result);
			ModifierOr = SpawnBooleanOperator(Graph, TEXT("BooleanOR"), Base + FVector2D(700, 330), 2, Result);
		}
		UK2Node_CallFunction* GetOwning = SpawnCall(Graph, UAnimInstance::StaticClass(), TEXT("GetOwningComponent"), Base + FVector2D(340, 340 + ModifierOffset), Result);
		UK2Node_CallFunction* WasRendered = SpawnCall(Graph, UPrimitiveComponent::StaticClass(), TEXT("WasRecentlyRendered"), Base + FVector2D(700, 340 + ModifierOffset), Result);
		UK2Node_CommutativeAssociativeBinaryOperator* And = SpawnBooleanOperator(Graph, TEXT("BooleanAND"), Base + FVector2D(1040, 190 + ModifierOffset * 0.5f), KeySpec.HasModifier() ? 3 : 2, Result);
		UK2Node_IfThenElse* Branch = SpawnBranch(Graph, Base + FVector2D(1380, 0));
		UK2Node_VariableGet* GetState = SpawnVariableGet(Graph, Variable.VariableName, nullptr, Base + FVector2D(1720, 190));
		UK2Node_CallFunction* Add = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Add_IntInt"), Base + FVector2D(1960, 190), Result);
		UK2Node_CallFunction* Mod = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Percent_IntInt"), Base + FVector2D(2200, 190), Result);
		UK2Node_VariableSet* SetState = SpawnVariableSet(Graph, Variable.VariableName, nullptr, Base + FVector2D(2460, 0));
		UK2Node_CallFunction* CreateSave = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("CreateSaveGameObject"), Base + FVector2D(2800, 0), Result);
		UK2Node_VariableSet* SetSaveValue = SpawnVariableSet(Graph, SaveVariable, SaveClass, Base + FVector2D(3140, 0));
		UK2Node_CallFunction* SaveSlot = SpawnCall(Graph, UGameplayStatics::StaticClass(), TEXT("SaveGameToSlot"), Base + FVector2D(3480, 0), Result);
		UK2Node_CallFunction* Apply = SpawnNestedApplyCall(Context, Base + FVector2D(3840, 0), Result);
		if (!GetController || !WasPressed || !GetOwning || !WasRendered || !And || !Branch || !GetState || !Add || !Mod || !SetState || !CreateSave || !SetSaveValue || !SaveSlot || !Apply)
		{
			return false;
		}
		if (KeySpec.HasModifier() && (!ModifierRightDown || !ModifierLeftDown || !ModifierOr))
		{
			return false;
		}
		Context.Track({ GetController, WasPressed, ModifierRightDown, ModifierLeftDown, ModifierOr, GetOwning, WasRendered, And, Branch, GetState, Add, Mod, SetState, CreateSave, SetSaveValue, SaveSlot });

		SetDefaultValue(FindAnyPin(GetController, TEXT("PlayerIndex")), TEXT("0"));
		SetDefaultValue(FindAnyPin(WasPressed, TEXT("Key")), KeySpec.MainKey.GetFName().ToString());
		if (KeySpec.HasModifier())
		{
			FKey LeftModifierKey;
			FKey RightModifierKey;
			GetModifierKeys(KeySpec.Modifier, LeftModifierKey, RightModifierKey);
			SetDefaultValue(FindAnyPin(ModifierRightDown, TEXT("Key")), RightModifierKey.GetFName().ToString());
			SetDefaultValue(FindAnyPin(ModifierLeftDown, TEXT("Key")), LeftModifierKey.GetFName().ToString());
		}
		SetDefaultValue(FindAnyPin(WasRendered, TEXT("Tolerance")), TEXT("0.2"));
		SetDefaultValue(FindAnyPin(Add, TEXT("B")), TEXT("1"));
		SetDefaultValue(FindAnyPin(Mod, TEXT("B")), FString::FromInt(Variable.OptionCount));
		SetDefaultObject(FindAnyPin(CreateSave, TEXT("SaveGameClass")), SaveClass);
		ConfigureSaveSlotPins(SaveSlot, SlotName);

		UEdGraphPin* ControllerPin = FindAnyPin(GetController, UEdGraphSchema_K2::PN_ReturnValue);
		Connect(ControllerPin, FindSelfPin(WasPressed), Result, TEXT("PlayerController -> WasInputKeyJustPressed"));
		Connect(FindAnyPin(WasPressed, UEdGraphSchema_K2::PN_ReturnValue), And->GetInputPin(0), Result, TEXT("WasPressed -> AND"));
		if (KeySpec.HasModifier())
		{
			Connect(ControllerPin, FindSelfPin(ModifierRightDown), Result, TEXT("PlayerController -> right modifier"));
			Connect(ControllerPin, FindSelfPin(ModifierLeftDown), Result, TEXT("PlayerController -> left modifier"));
			Connect(FindAnyPin(ModifierRightDown, UEdGraphSchema_K2::PN_ReturnValue), ModifierOr->GetInputPin(0), Result, TEXT("Right modifier -> OR"));
			Connect(FindAnyPin(ModifierLeftDown, UEdGraphSchema_K2::PN_ReturnValue), ModifierOr->GetInputPin(1), Result, TEXT("Left modifier -> OR"));
			Connect(FindAnyPin(ModifierOr, UEdGraphSchema_K2::PN_ReturnValue), And->GetInputPin(1), Result, TEXT("Modifier OR -> AND"));
			Connect(FindAnyPin(WasRendered, UEdGraphSchema_K2::PN_ReturnValue), And->GetInputPin(2), Result, TEXT("WasRendered -> AND"));
		}
		else
		{
			Connect(FindAnyPin(WasRendered, UEdGraphSchema_K2::PN_ReturnValue), And->GetInputPin(1), Result, TEXT("WasRendered -> AND"));
		}
		Connect(FindAnyPin(GetOwning, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(WasRendered), Result, TEXT("OwningComponent -> WasRecentlyRendered"));
		for (UEdGraphPin* EntryPin : EntryPins)
		{
			Connect(EntryPin, GetSchema()->FindExecutionPin(*Branch, EGPD_Input), Result, TEXT("Toggle tree update -> Branch"));
		}
		Connect(FindAnyPin(And, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Branch, UEdGraphSchema_K2::PN_Condition), Result, TEXT("AND -> Branch condition"));
		Connect(FindAnyPin(GetState, Variable.VariableName), FindAnyPin(Add, TEXT("A")), Result, TEXT("State -> Add"));
		Connect(FindAnyPin(Add, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Mod, TEXT("A")), Result, TEXT("Add -> Percent"));
		Connect(FindAnyPin(Branch, UEdGraphSchema_K2::PN_Then), GetSchema()->FindExecutionPin(*SetState, EGPD_Input), Result, TEXT("Pressed -> Set state"));
		Connect(FindAnyPin(Mod, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SetState, Variable.VariableName), Result, TEXT("Percent -> Set state"));
		Connect(GetSchema()->FindExecutionPin(*SetState, EGPD_Output), GetSchema()->FindExecutionPin(*CreateSave, EGPD_Input), Result, TEXT("Set state -> CreateSave"));
		Connect(GetSchema()->FindExecutionPin(*CreateSave, EGPD_Output), GetSchema()->FindExecutionPin(*SetSaveValue, EGPD_Input), Result, TEXT("CreateSave -> Set save"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindSelfPin(SetSaveValue), Result, TEXT("CreateSave -> Set save target"));
		Connect(FindSetOutputPin(SetState), FindAnyPin(SetSaveValue, SaveVariable), Result, TEXT("New state -> Set save"));
		Connect(GetSchema()->FindExecutionPin(*SetSaveValue, EGPD_Output), GetSchema()->FindExecutionPin(*SaveSlot, EGPD_Input), Result, TEXT("Set save -> SaveGameToSlot"));
		Connect(FindAnyPin(CreateSave, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(SaveSlot, TEXT("SaveGameObject")), Result, TEXT("CreateSave -> SaveGameToSlot"));
		Connect(GetSchema()->FindExecutionPin(*SaveSlot, EGPD_Output), GetSchema()->FindExecutionPin(*Apply, EGPD_Input), Result, TEXT("SaveGameToSlot -> Apply"));

		// Both exits continue to the next handler so several keys can be handled in one frame.
		OutContinuePins = { FindAnyPin(Branch, UEdGraphSchema_K2::PN_Else), GetSchema()->FindExecutionPin(*Apply, EGPD_Output) };
		return true;
	}

	static FString BuildNestedMetadata(const FHTNestedToggleGraph& Graph, const FHTNestedToggleCompiledGraph& Compiled, const FName ApplyEvent)
	{
		TMap<FString, int32> SwitchIndexById;
		for (int32 Index = 0; Index < Graph.Switches.Num(); ++Index)
		{
			SwitchIndexById.Add(Trimmed(Graph.Switches[Index].NodeId), Index);
		}
		// An option is empty when nothing hangs on it: no part and no child switch.
		TSet<TPair<int32, int32>> UsedOptions;
		for (int32 Index = 0; Index < Graph.Switches.Num(); ++Index)
		{
			const int32* ParentIndex = SwitchIndexById.Find(Trimmed(Graph.Switches[Index].ParentNodeId));
			if (ParentIndex)
			{
				UsedOptions.Add(TPair<int32, int32>(*ParentIndex, Graph.Switches[Index].ParentOption));
			}
		}
		for (const FHTNestedTogglePart& Part : Graph.Parts)
		{
			if (Part.Path.Num() > 0)
			{
				const FHTNestedTogglePathStep& Last = Part.Path.Last();
				if (const int32* SwitchIndex = SwitchIndexById.Find(Trimmed(Last.SwitchNodeId)))
				{
					UsedOptions.Add(TPair<int32, int32>(*SwitchIndex, Last.Option));
				}
			}
		}

		FString Metadata = FString::Printf(TEXT("%s%s;Apply=%s"), NestedToggleCommentPrefix, *Compiled.Name, *ApplyEvent.ToString());
		for (int32 Index = 0; Index < Graph.Switches.Num(); ++Index)
		{
			const FHTNestedToggleSwitch& Switch = Graph.Switches[Index];
			const FHTNestedToggleStateVariable& Variable = Compiled.Variables[Compiled.SwitchVariableIndices[Index]];
			FInputKeySpec KeySpec;
			FString KeyError;
			ResolveInputKeySpec(Switch.KeyName, KeySpec, KeyError);
			const int32* ParentIndex = SwitchIndexById.Find(Trimmed(Switch.ParentNodeId));
			const FString ParentVariable = ParentIndex ? Compiled.Variables[Compiled.SwitchVariableIndices[*ParentIndex]].VariableName.ToString() : FString();
			const int32 ParentOption = ParentIndex ? Switch.ParentOption : INDEX_NONE;
			// The in-game panel names the row after the state variable. The switch comment is a
			// graph-only note; the field keeps its position so older panels still parse it.
			const FString Label = SanitizeNestedMetadataField(Variable.VariableName.ToString());
			TArray<FString> OptionLabels;
			// One character per option: '1' = nothing hangs on it (no part, no child switch).
			// The panel uses this, not the labels, to decide between ON/OFF and cycle controls.
			FString EmptyMask;
			for (int32 Option = 0; Option < Switch.OptionCount; ++Option)
			{
				// Every option of a Texture or Material switch does something: never an ON/OFF row.
				const bool bUsed = Switch.Kind != EHTNestedToggleSwitchKind::Visibility || UsedOptions.Contains(TPair<int32, int32>(Index, Option));
				FString OptionLabel = Switch.OptionLabels.IsValidIndex(Option) ? SanitizeNestedMetadataField(Switch.OptionLabels[Option]) : FString();
				if (OptionLabel.IsEmpty() && bUsed)
				{
					OptionLabel = FString::FromInt(Option + 1);
				}
				OptionLabels.Add(OptionLabel);
				EmptyMask.AppendChar(bUsed ? TEXT('0') : TEXT('1'));
			}
			Metadata += FString::Printf(
				TEXT(";Switch=%s,%s,%d,%s,%d,%s,%s,%s"),
				*Variable.VariableName.ToString(),
				*MakeNestedCanonicalKey(KeySpec),
				Switch.OptionCount,
				*ParentVariable,
				ParentOption,
				*Label,
				*FString::Join(OptionLabels, TEXT("|")),
				*EmptyMask);
		}
		return Metadata;
	}
}

FString FHTBlueprintToggleGeneratorResult::ToDisplayString() const
{
	TArray<FString> Lines;
	if (bSuccess)
	{
		Lines.Add(TEXT("Generation finished"));
	}
	else
	{
		Lines.Add(TEXT("Generation failed"));
	}

	for (const FString& Error : Errors)
	{
		Lines.Add(TEXT("Error: ") + Error);
	}
	for (const FString& Message : Messages)
	{
		Lines.Add(Message);
	}
	return FString::Join(Lines, TEXT("\n"));
}

FHTBlueprintToggleGeneratorResult FHTBlueprintToggleGenerator::Generate(const FHTBlueprintToggleGeneratorParams& Params)
{
	using namespace HTToggleGenerator;

	FHTBlueprintToggleGeneratorResult Result;

	const FScopedTransaction Transaction(LOCTEXT("GenerateToggleNodes", "Generate HT Blueprint Toggle Nodes"));
	if (Params.Mode == EHTBlueprintToggleMode::TextureChannelTuner)
	{
		return GenerateTextureChannelTuner(Params);
	}

	UBlueprint* AnimBlueprint = LoadBlueprint(Params.AnimBlueprintPath, Result, TEXT("Animation Blueprint"));
	UBlueprint* SaveBlueprint = LoadBlueprint(Params.SaveGameBlueprintPath, Result, TEXT("SaveGame Blueprint"));
	if (!AnimBlueprint || !SaveBlueprint)
	{
		return Result;
	}

	if (Params.Mode == EHTBlueprintToggleMode::MaterialSection &&
		!Params.bIncludeHiddenMaterialState && GetMaterialVisibilityGroups(Params).Num() < 2)
	{
		Result.Errors.Add(TEXT("Material group cycling without a hide-all state requires at least two non-empty groups."));
		return Result;
	}

	AnimBlueprint->Modify();
	SaveBlueprint->Modify();

	const FName ToggleVariable = ToName(Params.ToggleVariableName, TEXT("Glove"));
	const TArray<int32> MaterialElementIndices = GetMaterialElementIndices(Params);
	const TArray<FName> MIDVariables = BuildMIDVariableNames(ToggleVariable, MaterialElementIndices);
	const FString RawSaveVariableName = Trimmed(Params.SaveVariableName);
	const FName SaveVariable = RawSaveVariableName.IsEmpty()
		? FName(*(ToggleVariable.ToString() + TEXT("Save")))
		: ToName(RawSaveVariableName, TEXT("GloveSave"));
	FString SlotName = Trimmed(Params.SlotName);
	if (SlotName.IsEmpty())
	{
		SlotName = ToggleVariable.ToString();
	}

	const int32 StateCount = GetCycleStateCount(Params);
	if (Params.InitialState < 0 || Params.InitialState >= StateCount)
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Initial State must be between 0 and %d for this toggle."),
			StateCount - 1));
		return Result;
	}

	TArray<UMaterialInterface*> TextureSourceMaterials;
	TArray<TArray<UTexture*>> TextureStatesByMaterial;
	TArray<UMaterialInterface*> MaterialInterfaces;
	if (Params.Mode == EHTBlueprintToggleMode::Texture)
	{
		if (!Params.bGenerateInitializeGraph)
		{
			Result.Errors.Add(TEXT("Texture switch mode requires the Initialize graph."));
			return Result;
		}
		if (MaterialElementIndices.Num() == 0)
		{
			Result.Errors.Add(TEXT("Texture switch mode requires at least one Material Slot."));
			return Result;
		}
		for (const int32 MaterialElementIndex : MaterialElementIndices)
		{
			if (MaterialElementIndex < 0)
			{
				Result.Errors.Add(TEXT("Material Slot must be zero or greater."));
				return Result;
			}
		}
		if (Trimmed(Params.TextureParameterName).IsEmpty())
		{
			Result.Errors.Add(TEXT("Texture Parameter cannot be empty."));
			return Result;
		}
		TMap<int32, UMaterialInterface*> SourceMaterialByElementIndex;
		TMap<int32, TArray<UTexture*>> TexturesByElementIndex;
		const TArray<FHTTextureMaterialSlotGroup> TextureMaterialSlotGroups = GetTextureMaterialSlotGroups(Params);
		if (TextureMaterialSlotGroups.Num() == 0)
		{
			Result.Errors.Add(TEXT("Texture switch mode requires at least one Source Material group."));
			return Result;
		}
		int32 TextureStateCount = INDEX_NONE;
		for (const FHTTextureMaterialSlotGroup& Group : TextureMaterialSlotGroups)
		{
			if (Trimmed(Group.SourceMaterialPath).IsEmpty())
			{
				Result.Errors.Add(TEXT("Choose a Source Material for every texture material group."));
				return Result;
			}
			if (Group.MaterialElementIndices.Num() == 0)
			{
				Result.Errors.Add(TEXT("Every texture material group needs at least one Material Slot."));
				return Result;
			}

			UMaterialInterface* GroupSourceMaterial = LoadMaterial(Group.SourceMaterialPath, Result);
			if (!GroupSourceMaterial)
			{
				return Result;
			}

			if (Group.TexturePaths.Num() < 2)
			{
				Result.Errors.Add(TEXT("Every texture material group needs at least two textures."));
				return Result;
			}
			if (TextureStateCount == INDEX_NONE)
			{
				TextureStateCount = Group.TexturePaths.Num();
			}
			else if (Group.TexturePaths.Num() != TextureStateCount)
			{
				Result.Errors.Add(TEXT("All texture material groups must have the same number of texture states."));
				return Result;
			}

			TArray<UTexture*> GroupTextures;
			GroupTextures.Reserve(Group.TexturePaths.Num());
			for (int32 TextureIndex = 0; TextureIndex < Group.TexturePaths.Num(); ++TextureIndex)
			{
				UTexture* Texture = LoadTexture(
					Group.TexturePaths[TextureIndex],
					Result,
					FString::Printf(TEXT("Material Group texture %d"), TextureIndex + 1));
				if (!Texture)
				{
					return Result;
				}
				GroupTextures.Add(Texture);
			}

			for (const int32 MaterialElementIndex : Group.MaterialElementIndices)
			{
				if (SourceMaterialByElementIndex.Contains(MaterialElementIndex))
				{
					Result.Errors.Add(FString::Printf(TEXT("Material Slot %d is assigned to more than one Source Material group."), MaterialElementIndex));
					return Result;
				}
				SourceMaterialByElementIndex.Add(MaterialElementIndex, GroupSourceMaterial);
				TexturesByElementIndex.Add(MaterialElementIndex, GroupTextures);
			}
		}

		for (const int32 MaterialElementIndex : MaterialElementIndices)
		{
			UMaterialInterface** SourceMaterialForElement = SourceMaterialByElementIndex.Find(MaterialElementIndex);
			if (!SourceMaterialForElement || !*SourceMaterialForElement)
			{
				Result.Errors.Add(FString::Printf(TEXT("Material Slot %d has no Source Material."), MaterialElementIndex));
				return Result;
			}
			TextureSourceMaterials.Add(*SourceMaterialForElement);

			TArray<UTexture*>* TexturesForElement = TexturesByElementIndex.Find(MaterialElementIndex);
			if (!TexturesForElement || TexturesForElement->Num() != TextureStateCount)
			{
				Result.Errors.Add(FString::Printf(TEXT("Material Slot %d has no texture group."), MaterialElementIndex));
				return Result;
			}
			TextureStatesByMaterial.Add(*TexturesForElement);
		}
		if (TextureSourceMaterials.Num() != MaterialElementIndices.Num() || TextureStatesByMaterial.Num() != MaterialElementIndices.Num())
		{
			return Result;
		}
	}
	else if (Params.Mode == EHTBlueprintToggleMode::MaterialInterface)
	{
		if (MaterialElementIndices.Num() == 0)
		{
			Result.Errors.Add(TEXT("Material switch mode requires at least one Material Slot."));
			return Result;
		}
		for (const int32 MaterialElementIndex : MaterialElementIndices)
		{
			if (MaterialElementIndex < 0)
			{
				Result.Errors.Add(TEXT("Material Slot must be zero or greater."));
				return Result;
			}
		}
		if (Params.MaterialInterfacePaths.Num() < 2)
		{
			Result.Errors.Add(TEXT("Material switch mode requires at least two materials."));
			return Result;
		}

		for (int32 MaterialIndex = 0; MaterialIndex < Params.MaterialInterfacePaths.Num(); ++MaterialIndex)
		{
			UMaterialInterface* Material = LoadMaterial(
				Params.MaterialInterfacePaths[MaterialIndex],
				Result,
				FString::Printf(TEXT("Material %d"), MaterialIndex + 1));
			if (Material)
			{
				MaterialInterfaces.Add(Material);
			}
		}
		if (MaterialInterfaces.Num() != Params.MaterialInterfacePaths.Num())
		{
			return Result;
		}
	}

	const TOptional<int32> InitialVariableValue = Params.Mode == EHTBlueprintToggleMode::MaterialSection
		? TOptional<int32>(Params.InitialState)
		: TOptional<int32>();
	if (!EnsureIntVariable(AnimBlueprint, ToggleVariable, InitialVariableValue, Result, TEXT("Animation Blueprint")) ||
		!EnsureIntVariable(SaveBlueprint, SaveVariable, InitialVariableValue, Result, TEXT("SaveGame Blueprint")))
	{
		return Result;
	}

	if (Params.Mode == EHTBlueprintToggleMode::Texture)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < MIDVariables.Num(); ++MaterialIndex)
		{
			if (!EnsureObjectVariable(
				AnimBlueprint,
				MIDVariables[MaterialIndex],
				UMaterialInstanceDynamic::StaticClass(),
				Result,
				FString::Printf(TEXT("Animation Blueprint MID for Material Slot %d"), MaterialElementIndices[MaterialIndex])))
			{
				return Result;
			}
		}
	}

	FKismetEditorUtilities::CompileBlueprint(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

	UClass* SaveClass = SaveBlueprint->GeneratedClass ? SaveBlueprint->GeneratedClass.Get() : SaveBlueprint->SkeletonGeneratedClass.Get();
	if (!SaveClass)
	{
		Result.Errors.Add(TEXT("The SaveGame Blueprint has no usable generated class."));
		return Result;
	}

	if (!SaveClass->IsChildOf(USaveGame::StaticClass()))
	{
		Result.Messages.Add(FString::Printf(TEXT("Note: %s is not a USaveGame subclass; the nodes are still generated."), *SaveClass->GetName()));
	}

	UEdGraph* LayoutGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
	int32 NextLayoutBaseY = FindNextLayoutBaseY(LayoutGraph);

	if (Params.bGenerateInitializeGraph)
	{
		GenerateInitializeGraph(AnimBlueprint, SaveClass, ToggleVariable, SaveVariable, MIDVariables, SlotName, Params, TextureSourceMaterials, TextureStatesByMaterial, MaterialInterfaces, NextLayoutBaseY, Result);
		if (Params.Mode == EHTBlueprintToggleMode::Texture)
		{
			NextLayoutBaseY += FMath::Max(1600, 1200 + GetCycleStateCount(Params) * 360 + MaterialElementIndices.Num() * 100);
		}
		else if (Params.Mode == EHTBlueprintToggleMode::MaterialInterface)
		{
			NextLayoutBaseY += FMath::Max(1600, 1200 + MaterialInterfaces.Num() * 360 + MaterialElementIndices.Num() * 100);
		}
		else
		{
			NextLayoutBaseY += FMath::Max(1600, 1200 + GetCycleStateCount(Params) * 360);
		}
	}

	if (Params.bGenerateUpdateGraph)
	{
		GenerateUpdateGraph(AnimBlueprint, SaveClass, ToggleVariable, SaveVariable, MIDVariables, SlotName, Params, TextureStatesByMaterial, MaterialInterfaces, NextLayoutBaseY, Result);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (SaveBlueprint->Status == BS_Error)
	{
		Result.Errors.Add(TEXT("The SaveGame Blueprint has compile errors after generation."));
	}
	if (AnimBlueprint->Status == BS_Error)
	{
		Result.Errors.Add(TEXT("The Animation Blueprint has compile errors after generation."));
	}

	if (Params.bSaveAssets)
	{
		TArray<UPackage*> Packages;
		Packages.AddUnique(AnimBlueprint->GetOutermost());
		Packages.AddUnique(SaveBlueprint->GetOutermost());

		TArray<UPackage*> FailedPackages;
		FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false, &FailedPackages, true, false);
		if (FailedPackages.Num() > 0)
		{
			Result.Errors.Add(TEXT("The Blueprints were generated, but the assets could not be saved. Save them manually in the editor."));
		}
		else
		{
			Result.Messages.Add(TEXT("Assets saved."));
		}
	}

	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

FString FHTNestedToggleIssue::ToDisplayString() const
{
	return NodeIds.IsEmpty() ? Message : FString::Printf(TEXT("[%s] %s"), *FString::Join(NodeIds, TEXT(", ")), *Message);
}

FString FHTNestedToggleGeneratorResult::ToDisplayString() const
{
	TArray<FString> Lines;
	Lines.Add(bSuccess ? TEXT("Toggle tree generated.") : TEXT("Toggle tree generation failed."));
	for (const FHTNestedToggleIssue& Error : Errors)
	{
		Lines.Add(TEXT("Error: ") + Error.ToDisplayString());
	}
	Lines.Append(Messages);
	return FString::Join(Lines, TEXT("\n"));
}

FString FHTBlueprintToggleGenerator::SanitizeNestedToggleGraphName(const FString& GraphName)
{
	const FString Sanitized = HTToggleGenerator::SanitizeNestedIdentifier(GraphName);
	return Sanitized.IsEmpty() ? FString(TEXT("ToggleTree")) : Sanitized;
}

FString FHTBlueprintToggleGenerator::MakeNestedToggleApplyEventName(const FString& GraphName)
{
	return FString(HTToggleGenerator::NestedToggleApplyPrefix) + SanitizeNestedToggleGraphName(GraphName);
}

FString FHTBlueprintToggleGenerator::MakeNestedToggleMetadataPrefix(const FString& GraphName)
{
	return FString(HTToggleGenerator::NestedToggleCommentPrefix) + SanitizeNestedToggleGraphName(GraphName) + TEXT(";");
}

bool FHTBlueprintToggleGenerator::CompileNestedToggleGraph(const FHTNestedToggleGraph& Graph, FHTNestedToggleCompiledGraph& OutCompiled, TArray<FHTNestedToggleIssue>& OutErrors)
{
	return HTToggleGenerator::CompileNestedToggleGraphImpl(Graph, OutCompiled, OutErrors);
}

FHTNestedToggleGeneratorResult FHTBlueprintToggleGenerator::GenerateNestedVisibility(const FHTNestedToggleGeneratorParams& Params)
{
	using namespace HTToggleGenerator;

	FHTNestedToggleGeneratorResult Result;
	FHTNestedToggleCompiledGraph Compiled;
	if (!CompileNestedToggleGraph(Params.Graph, Compiled, Result.Errors))
	{
		return Result;
	}
	if (Compiled.Parts.IsEmpty() && Compiled.EffectMaterialIDs.IsEmpty())
	{
		FHTNestedToggleIssue& Issue = Result.Errors.AddDefaulted_GetRef();
		Issue.Message = TEXT("The toggle tree has no parts and no Texture or Material switch. Connect at least one to the generate node.");
		return Result;
	}
	FNestedEffectAssets EffectAssets;
	if (!LoadNestedEffectAssets(Params.Graph, Compiled, EffectAssets, Result.Errors))
	{
		return Result;
	}

	// Graph-building helpers report plain messages; keep them in a legacy result and merge at the end.
	FHTBlueprintToggleGeneratorResult Inner;
	auto Finish = [&Result, &Inner]() -> FHTNestedToggleGeneratorResult&
	{
		for (const FString& Error : Inner.Errors)
		{
			FHTNestedToggleIssue& Issue = Result.Errors.AddDefaulted_GetRef();
			Issue.Message = Error;
		}
		for (const FString& Message : Inner.Messages)
		{
			Result.Messages.Add(Message);
		}
		Result.bSuccess = Result.Errors.IsEmpty();
		return Result;
	};

	const FScopedTransaction Transaction(LOCTEXT("GenerateNestedToggleNodes", "Generate HT Toggle Tree Nodes"));
	UBlueprint* AnimBlueprint = LoadBlueprint(Params.AnimBlueprintPath, Inner, TEXT("Animation Blueprint"));
	UBlueprint* SaveBlueprint = LoadBlueprint(Params.SaveGameBlueprintPath, Inner, TEXT("SaveGame Blueprint"));
	if (!AnimBlueprint || !SaveBlueprint)
	{
		return Finish();
	}

	AnimBlueprint->Modify();
	SaveBlueprint->Modify();
	for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
	{
		const FName SaveVariable(*(Variable.VariableName.ToString() + TEXT("Save")));
		if (!EnsureIntVariable(AnimBlueprint, Variable.VariableName, Variable.InitialOption, Inner, TEXT("Animation Blueprint"))
			|| !EnsureIntVariable(SaveBlueprint, SaveVariable, Variable.InitialOption, Inner, TEXT("SaveGame Blueprint")))
		{
			return Finish();
		}
	}
	// Per-slot working variables of the Texture and Material switches.
	TSet<FName> EffectVariables;
	for (const int32 MaterialID : Compiled.EffectMaterialIDs)
	{
		const FName BaseVariable = MakeNestedBaseVariable(Compiled.Name, MaterialID);
		EffectVariables.Add(BaseVariable);
		if (!EnsureObjectVariable(AnimBlueprint, BaseVariable, UMaterialInterface::StaticClass(), Inner, FString::Printf(TEXT("Toggle tree material for slot %d"), MaterialID)))
		{
			return Finish();
		}
		const TArray<FString> Parents = GetNestedTextureParents(Compiled, MaterialID);
		for (int32 ParentIndex = 0; ParentIndex < Parents.Num(); ++ParentIndex)
		{
			const FName MIDVariable = MakeNestedMIDVariable(Compiled.Name, MaterialID, ParentIndex);
			EffectVariables.Add(MIDVariable);
			if (!EnsureObjectVariable(AnimBlueprint, MIDVariable, UMaterialInstanceDynamic::StaticClass(), Inner, FString::Printf(TEXT("Toggle tree texture MID for slot %d"), MaterialID)))
			{
				return Finish();
			}
		}
	}

	FKismetEditorUtilities::CompileBlueprint(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	UClass* SaveClass = SaveBlueprint->GeneratedClass ? SaveBlueprint->GeneratedClass.Get() : SaveBlueprint->SkeletonGeneratedClass.Get();
	if (!SaveClass)
	{
		Inner.Errors.Add(TEXT("The SaveGame Blueprint has no generated class."));
		return Finish();
	}
	if (!SaveClass->IsChildOf(USaveGame::StaticClass()))
	{
		Inner.Messages.Add(FString::Printf(TEXT("Note: %s is not a USaveGame subclass; nodes are still generated."), *SaveClass->GetName()));
	}

	FNestedGenerationContext Context;
	Context.AnimBlueprint = AnimBlueprint;
	Context.SaveClass = SaveClass;
	Context.Graph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
	Context.ApplyEvent = FName(*MakeNestedToggleApplyEventName(Compiled.Name));
	Context.CharacterName = Params.CharacterName;
	Context.SectionParams.SectionIndex = Params.SectionIndex;
	Context.SectionParams.LODIndex = Params.LODIndex;
	if (!Context.Graph)
	{
		Inner.Errors.Add(TEXT("The Animation Blueprint has no Event Graph."));
		return Finish();
	}

	// The tree is one managed region: replace it as a whole, never append.
	RemoveManagedCommentGroups(Context.Graph, MakeNestedToggleMetadataPrefix(Compiled.Name));
	RemoveCustomEventNodes(Context.Graph, Context.ApplyEvent);
	// Slot variables of slots this tree no longer switches.
	{
		TArray<FName> Stale;
		for (const FBPVariableDescription& Variable : AnimBlueprint->NewVariables)
		{
			if (!EffectVariables.Contains(Variable.VarName) && IsNestedEffectVariable(Compiled.Name, Variable.VarName.ToString()))
			{
				Stale.Add(Variable.VarName);
			}
		}
		for (const FName Name : Stale)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(AnimBlueprint, Name);
			Inner.Messages.Add(FString::Printf(TEXT("Removed unused toggle tree variable: %s"), *Name.ToString()));
		}
	}

	const int32 BaseY = FindNextLayoutBaseY(Context.Graph);
	constexpr float LeftX = -1500.0f;
	constexpr float InitBlockHeight = 1100.0f;
	constexpr float UpdateBlockHeight = 1000.0f;
	int32 HandlerCount = 0;
	for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
	{
		HandlerCount += Variable.KeyNames.Num();
	}
	const float InitY = BaseY;
	const float UpdateY = InitY + FMath::Max(1, Compiled.Variables.Num()) * InitBlockHeight + 200.0f;
	const float ApplyY = UpdateY + FMath::Max(1, HandlerCount) * UpdateBlockHeight + 200.0f;

	float ApplyHeight = 0.0f;
	if (!BuildNestedApplyEvent(Context, Compiled, EffectAssets, FVector2D(LeftX, ApplyY), ApplyHeight, Inner))
	{
		Inner.Errors.Add(TEXT("Could not create the toggle tree Apply event."));
		return Finish();
	}
	// The Apply custom event must exist on the skeleton class before it can be called.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UEdGraphPin* InitEntry = GetEventExecutionEntry(Context.Graph, TEXT("BlueprintInitializeAnimation"), FVector2D(LeftX - 400.0f, InitY), Inner);
	if (!InitEntry)
	{
		return Finish();
	}
	TArray<UEdGraphPin*> InitContinuePins = { InitEntry };
	for (int32 VariableIndex = 0; VariableIndex < Compiled.Variables.Num(); ++VariableIndex)
	{
		TArray<UEdGraphPin*> NextPins;
		if (!BuildNestedInitVariable(Context, Compiled.Variables[VariableIndex], InitContinuePins, FVector2D(LeftX, InitY + VariableIndex * InitBlockHeight + 300.0f), NextPins, Inner))
		{
			Inner.Errors.Add(FString::Printf(TEXT("Could not create the SaveGame restore nodes for %s."), *Compiled.Variables[VariableIndex].VariableName.ToString()));
			return Finish();
		}
		InitContinuePins = MoveTemp(NextPins);
	}
	UK2Node_CallFunction* InitApply = SpawnNestedApplyCall(Context, FVector2D(LeftX + 2400.0f, InitY + FMath::Max(0, Compiled.Variables.Num() - 1) * InitBlockHeight + 300.0f), Inner);
	if (!InitApply)
	{
		return Finish();
	}
	for (UEdGraphPin* ContinuePin : InitContinuePins)
	{
		Connect(ContinuePin, GetSchema()->FindExecutionPin(*InitApply, EGPD_Input), Inner, TEXT("Toggle tree init -> Apply"));
	}

	if (HandlerCount > 0)
	{
		UEdGraphPin* UpdateEntry = GetEventExecutionEntry(Context.Graph, TEXT("BlueprintUpdateAnimation"), FVector2D(LeftX - 400.0f, UpdateY), Inner);
		if (!UpdateEntry)
		{
			return Finish();
		}
		TArray<UEdGraphPin*> UpdateContinuePins = { UpdateEntry };
		int32 HandlerIndex = 0;
		for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
		{
			for (const FString& KeyName : Variable.KeyNames)
			{
				TArray<UEdGraphPin*> NextPins;
				if (!BuildNestedKeyHandler(Context, Variable, KeyName, UpdateContinuePins, FVector2D(LeftX, UpdateY + HandlerIndex * UpdateBlockHeight + 200.0f), NextPins, Inner))
				{
					Inner.Errors.Add(FString::Printf(TEXT("Could not create the key handler for %s."), *Variable.VariableName.ToString()));
					return Finish();
				}
				UpdateContinuePins = MoveTemp(NextPins);
				++HandlerIndex;
			}
		}
	}

	const FString Metadata = BuildNestedMetadata(Params.Graph, Compiled, Context.ApplyEvent);
	float MinX = LeftX;
	float MinY = InitY;
	float MaxX = LeftX;
	float MaxY = ApplyY + ApplyHeight;
	for (const UEdGraphNode* Node : Context.Nodes)
	{
		MinX = FMath::Min(MinX, static_cast<float>(Node->NodePosX));
		MinY = FMath::Min(MinY, static_cast<float>(Node->NodePosY));
		MaxX = FMath::Max(MaxX, static_cast<float>(Node->NodePosX));
		MaxY = FMath::Max(MaxY, static_cast<float>(Node->NodePosY));
	}
	UEdGraphNode_Comment* Comment = SpawnCommentBox(
		Context.Graph,
		Metadata,
		FVector2D(MinX - 100.0f, MinY - 300.0f),
		FVector2D(MaxX - MinX + 900.0f, MaxY - MinY + 700.0f),
		FLinearColor(0.34f, 0.17f, 0.06f, 1.0f));
	AddNodesToComment(Comment, Context.Nodes);

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(SaveBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (SaveBlueprint->Status == BS_Error)
	{
		Inner.Errors.Add(TEXT("The SaveGame Blueprint has compile errors after generation."));
	}
	if (AnimBlueprint->Status == BS_Error)
	{
		Inner.Errors.Add(TEXT("The Animation Blueprint has compile errors after generation."));
	}

	if (Params.bSaveAssets && Inner.Errors.IsEmpty())
	{
		TArray<UPackage*> Packages;
		Packages.AddUnique(AnimBlueprint->GetOutermost());
		Packages.AddUnique(SaveBlueprint->GetOutermost());
		TArray<UPackage*> FailedPackages;
		FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false, &FailedPackages, true, false);
		if (FailedPackages.Num() > 0)
		{
			Inner.Errors.Add(TEXT("Nodes were generated, but the assets could not be saved. Save them manually in the editor."));
		}
		else
		{
			Inner.Messages.Add(TEXT("Assets saved."));
		}
	}

	if (Inner.Errors.IsEmpty())
	{
		Result.Variables = Compiled.Variables;
		int32 MaterialCount = 0;
		for (const FHTNestedToggleCompiledPart& Part : Compiled.Parts)
		{
			MaterialCount += Part.MaterialIDs.Num();
		}
		Inner.Messages.Add(FString::Printf(
			TEXT("Generated toggle tree %s: %d state variable(s), %d part(s), %d material ID(s)."),
			*Compiled.Name,
			Compiled.Variables.Num(),
			Compiled.Parts.Num(),
			MaterialCount));
		if (!Compiled.EffectMaterialIDs.IsEmpty())
		{
			Inner.Messages.Add(FString::Printf(
				TEXT("  Texture and Material switches act on %d slot(s): %s (%d texture and %d material effect(s))."),
				Compiled.EffectMaterialIDs.Num(),
				*FString::JoinBy(Compiled.EffectMaterialIDs, TEXT(", "), [](const int32 ID) { return FString::FromInt(ID); }),
				Compiled.TextureEffects.Num(),
				Compiled.MaterialEffects.Num()));
		}
		for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
		{
			Inner.Messages.Add(FString::Printf(TEXT("  %s: keys %s, %d options, initial %d, slot \"%s\""),
				*Variable.VariableName.ToString(),
				*FString::Join(Variable.KeyNames, TEXT(" / ")),
				Variable.OptionCount,
				Variable.InitialOption,
				*MakeNestedSlotName(Variable.VariableName, Params.CharacterName)));
		}
	}
	return Finish();
}
#undef LOCTEXT_NAMESPACE
