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
			Result.Errors.Add(FString::Printf(TEXT("%s 加载失败: %s"), *Label, *Path));
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
			Result.Messages.Add(FString::Printf(TEXT("%s 已存在变量: %s"), *Label, *VariableName.ToString()));
			return true;
		}

		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;

		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType, DefaultValue))
		{
			Result.Errors.Add(FString::Printf(TEXT("%s 添加变量失败: %s"), *Label, *VariableName.ToString()));
			return false;
		}

		Result.Messages.Add(FString::Printf(TEXT("%s 已添加 int 变量: %s"), *Label, *VariableName.ToString()));
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
			Result.Errors.Add(FString::Printf(TEXT("连线失败，缺少 Pin: %s"), *Label));
			return false;
		}

		if (A->LinkedTo.Contains(B))
		{
			return true;
		}

		if (!GetSchema()->TryCreateConnection(A, B))
		{
			Result.Errors.Add(FString::Printf(TEXT("连线失败: %s (%s -> %s)"), *Label, *A->PinName.ToString(), *B->PinName.ToString()));
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
			for (UEdGraphNode* Node : GetNodesUnderComment(Comment))
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
			Result.Errors.Add(FString::Printf(TEXT("找不到函数: %s.%s"), OwnerClass ? *OwnerClass->GetName() : TEXT("None"), *FunctionName.ToString()));
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
			Result.Errors.Add(FString::Printf(TEXT("找不到函数: UKismetMathLibrary.%s"), *FunctionName.ToString()));
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
			Result.Messages.Add(FString::Printf(TEXT("已创建事件节点: %s"), *EventName.ToString()));
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
			Result.Errors.Add(FString::Printf(TEXT("无法创建或找到事件节点: %s"), *EventName.ToString()));
			return nullptr;
		}

		UEdGraphPin* EventOutput = GetSchema()->FindExecutionPin(*EventNode, EGPD_Output);
		if (!EventOutput)
		{
			Result.Errors.Add(FString::Printf(TEXT("事件没有执行输出: %s"), *EventName.ToString()));
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
		return FMath::Max(2, GetMaterialVisibilityGroups(Params).Num() + 1);
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
		const int32 StateCount = Groups.Num() + 1;
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
			Result.Errors.Add(TEXT("动画蓝图没有 EventGraph"));
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
		Connect(FindAnyPin(GetSaveValue, SaveVariable), FindAnyPin(SetAnimValue, ToggleVariable), Result, TEXT("Save value -> Anim value"));

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
			Result.Errors.Add(TEXT("动画蓝图没有 EventGraph"));
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
		FName AdjustmentVariable;
		FName ApplyEvent;
		FName ResetEvent;
		FName ExportEvent;
		FString ExportFileName;
	};

	static bool BuildTextureTunerBlendColors(
		UEdGraph* Graph,
		UEdGraphPin* Adjustment,
		const FVector2D Base,
		UEdGraphPin*& OutScale,
		UEdGraphPin*& OutBias,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		UK2Node_CallFunction* Break = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("BreakColor"), Base, Result);
		UK2Node_CallFunction* Scale = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("MakeColor"), Base + FVector2D(1100, 0), Result);
		UK2Node_CallFunction* Bias = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("MakeColor"), Base + FVector2D(1100, 360), Result);
		if (!Break || !Scale || !Bias)
		{
			return false;
		}
		Connect(Adjustment, FindAnyPin(Break, TEXT("InColor")), Result, TEXT("Adjustment -> RGB components"));
		SetDefaultValue(FindAnyPin(Scale, TEXT("A")), TEXT("1.0"));
		SetDefaultValue(FindAnyPin(Bias, TEXT("A")), TEXT("1.0"));
		static const FName Channels[] = { TEXT("R"), TEXT("G"), TEXT("B") };
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			const FVector2D Position = Base + FVector2D(240, Channel * 220);
			UK2Node_CallFunction* Clamp = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("FClamp"), Position, Result);
			UK2Node_CallFunction* Abs = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Abs"), Position + FVector2D(250, 0), Result);
			UK2Node_CallFunction* OneMinus = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Subtract_DoubleDouble"), Position + FVector2D(500, 0), Result);
			UK2Node_CallFunction* Positive = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("FMax"), Position + FVector2D(500, 100), Result);
			if (!Clamp || !Abs || !OneMinus || !Positive)
			{
				return false;
			}
			// Signed adjustment t: source * (1 - abs(t)) + max(t, 0).
			// t=-1 -> 0, t=0 -> original, t=1 -> 1, including originally black pixels.
			SetDefaultValue(FindAnyPin(Clamp, TEXT("Min")), TEXT("-1.0"));
			SetDefaultValue(FindAnyPin(Clamp, TEXT("Max")), TEXT("1.0"));
			SetDefaultValue(FindAnyPin(OneMinus, TEXT("A")), TEXT("1.0"));
			SetDefaultValue(FindAnyPin(Positive, TEXT("B")), TEXT("0.0"));
			Connect(FindAnyPin(Break, Channels[Channel]), FindAnyPin(Clamp, TEXT("Value")), Result, TEXT("Channel -> clamp adjustment"));
			Connect(FindAnyPin(Clamp, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Abs, TEXT("A")), Result, TEXT("Clamped adjustment -> absolute value"));
			Connect(FindAnyPin(Abs, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(OneMinus, TEXT("B")), Result, TEXT("Absolute adjustment -> source weight"));
			Connect(FindAnyPin(Clamp, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Positive, TEXT("A")), Result, TEXT("Clamped adjustment -> white weight"));
			Connect(FindAnyPin(OneMinus, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Scale, Channels[Channel]), Result, TEXT("Source weight -> draw tint"));
			Connect(FindAnyPin(Positive, UEdGraphSchema_K2::PN_ReturnValue), FindAnyPin(Bias, Channels[Channel]), Result, TEXT("White weight -> additive tint"));
		}
		OutScale = FindAnyPin(Scale, UEdGraphSchema_K2::PN_ReturnValue);
		OutBias = FindAnyPin(Bias, UEdGraphSchema_K2::PN_ReturnValue);
		return Result.Errors.IsEmpty();
	}

	static void GenerateTextureTunerApplyGraph(
		UEdGraph* Graph,
		const FTextureTunerMapSpec& Map,
		const TArray<FName>& MIDVariables,
		const FVector2D Base,
		TArray<UEdGraphNode*>& OutNodes,
		FHTBlueprintToggleGeneratorResult& Result)
	{
		const int32 FirstNodeIndex = Graph->Nodes.Num();
		UK2Node_CustomEvent* Event = SpawnCustomEvent(Graph, Map.ApplyEvent, Base);
		UK2Node_VariableGet* GetRT = SpawnVariableGet(Graph, Map.RenderTargetVariable, nullptr, Base + FVector2D(260, 220));
		UK2Node_VariableGet* GetSource = SpawnVariableGet(Graph, Map.SourceVariable, nullptr, Base + FVector2D(520, 360));
		UK2Node_VariableGet* GetAdjustment = SpawnVariableGet(Graph, Map.AdjustmentVariable, nullptr, Base + FVector2D(520, 500));
		UK2Node_CallFunction* BeginDraw = SpawnCall(Graph, UKismetRenderingLibrary::StaticClass(), TEXT("BeginDrawCanvasToRenderTarget"), Base + FVector2D(260, 0), Result);
		UK2Node_CallFunction* DrawTexture = SpawnCall(Graph, UCanvas::StaticClass(), TEXT("K2_DrawTexture"), Base + FVector2D(760, 0), Result);
		UK2Node_CallFunction* DrawWhite = SpawnCall(Graph, UCanvas::StaticClass(), TEXT("K2_DrawTexture"), Base + FVector2D(1120, 0), Result);
		UK2Node_CallFunction* EndDraw = SpawnCall(Graph, UKismetRenderingLibrary::StaticClass(), TEXT("EndDrawCanvasToRenderTarget"), Base + FVector2D(1480, 0), Result);
		if (!Event || !GetRT || !GetSource || !GetAdjustment || !BeginDraw || !DrawTexture || !DrawWhite || !EndDraw)
		{
			return;
		}

		UEdGraphPin* Scale = nullptr;
		UEdGraphPin* Bias = nullptr;
		if (!BuildTextureTunerBlendColors(Graph, FindAnyPin(GetAdjustment, Map.AdjustmentVariable), Base + FVector2D(800, 360), Scale, Bias, Result))
		{
			return;
		}
		for (UK2Node_CallFunction* Draw : { DrawTexture, DrawWhite })
		{
			SetDefaultValue(FindAnyPin(Draw, TEXT("ScreenPosition")), TEXT("(X=0.000000,Y=0.000000)"));
			SetDefaultValue(FindAnyPin(Draw, TEXT("CoordinatePosition")), TEXT("(X=0.000000,Y=0.000000)"));
			SetDefaultValue(FindAnyPin(Draw, TEXT("CoordinateSize")), TEXT("(X=1.000000,Y=1.000000)"));
			SetDefaultValue(FindAnyPin(Draw, TEXT("Rotation")), TEXT("0.0"));
			Connect(FindAnyPin(BeginDraw, TEXT("Size")), FindAnyPin(Draw, TEXT("ScreenSize")), Result, TEXT("Full render target size -> Draw Texture"));
			Connect(FindAnyPin(BeginDraw, TEXT("Canvas")), FindSelfPin(Draw), Result, TEXT("Canvas -> Draw Texture"));
		}
		SetDefaultValue(FindAnyPin(DrawTexture, TEXT("BlendMode")), TEXT("BLEND_Opaque"));
		// A null Canvas texture uses the engine's white texture. No new material/asset is needed.
		// Additive Canvas blending only writes RGB, preserving the source alpha from the first pass.
		SetDefaultValue(FindAnyPin(DrawWhite, TEXT("BlendMode")), TEXT("BLEND_Additive"));

		Connect(GetSchema()->FindExecutionPin(*Event, EGPD_Output), GetSchema()->FindExecutionPin(*BeginDraw, EGPD_Input), Result, TEXT("Texture tuner Apply -> Begin Draw"));
		Connect(FindAnyPin(GetRT, Map.RenderTargetVariable), FindAnyPin(BeginDraw, TEXT("TextureRenderTarget")), Result, TEXT("Texture tuner RT -> Begin Draw"));
		Connect(GetSchema()->FindExecutionPin(*BeginDraw, EGPD_Output), GetSchema()->FindExecutionPin(*DrawTexture, EGPD_Input), Result, TEXT("Begin Draw -> Draw Texture"));
		Connect(FindAnyPin(GetSource, Map.SourceVariable), FindAnyPin(DrawTexture, TEXT("RenderTexture")), Result, TEXT("Original texture -> Draw Texture"));
		Connect(Scale, FindAnyPin(DrawTexture, TEXT("RenderColor")), Result, TEXT("Original contribution -> opaque draw"));
		Connect(Bias, FindAnyPin(DrawWhite, TEXT("RenderColor")), Result, TEXT("White contribution -> additive draw"));
		Connect(GetSchema()->FindExecutionPin(*DrawTexture, EGPD_Output), GetSchema()->FindExecutionPin(*DrawWhite, EGPD_Input), Result, TEXT("Overwrite previous preview -> add white contribution"));
		Connect(GetSchema()->FindExecutionPin(*DrawWhite, EGPD_Output), GetSchema()->FindExecutionPin(*EndDraw, EGPD_Input), Result, TEXT("Add white -> End Draw"));
		Connect(FindAnyPin(BeginDraw, TEXT("Context")), FindAnyPin(EndDraw, TEXT("Context")), Result, TEXT("Draw context -> End Draw"));

		UEdGraphPin* Exec = GetSchema()->FindExecutionPin(*EndDraw, EGPD_Output);
		for (int32 Index = 0; Index < MIDVariables.Num(); ++Index)
		{
			const FVector2D NodePosition = Base + FVector2D(1820 + Index * 360, 0);
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

		// Include any real-number conversion nodes inserted by the schema in regeneration cleanup.
		for (int32 Index = FirstNodeIndex; Index < Graph->Nodes.Num(); ++Index)
		{
			OutNodes.AddUnique(Graph->Nodes[Index]);
		}
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
		UK2Node_VariableSet* SetAdjustment = SpawnVariableSet(Graph, Map.AdjustmentVariable, nullptr, Base + FVector2D(280, 0));
		UK2Node_VariableGet* GetSource = SpawnVariableGet(Graph, Map.SourceVariable, nullptr, Base + FVector2D(560, 260));
		if (!Event || !SetAdjustment || !GetSource)
		{
			return;
		}
		SetDefaultValue(FindAnyPin(SetAdjustment, Map.AdjustmentVariable), TEXT("(R=0.000000,G=0.000000,B=0.000000,A=1.000000)"));
		Connect(GetSchema()->FindExecutionPin(*Event, EGPD_Output), GetSchema()->FindExecutionPin(*SetAdjustment, EGPD_Input), Result, TEXT("Texture tuner Reset -> neutral adjustment"));

		UEdGraphPin* Exec = GetSchema()->FindExecutionPin(*SetAdjustment, EGPD_Output);
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
			Connect(Exec, GetSchema()->FindExecutionPin(*SetTexture, EGPD_Input), Result, TEXT("Reset adjustment -> restore texture"));
			Connect(FindAnyPin(GetMID, MIDVariables[Index]), FindSelfPin(SetTexture), Result, TEXT("MID -> restore texture"));
			Connect(FindAnyPin(GetSource, Map.SourceVariable), FindAnyPin(SetTexture, TEXT("Value")), Result, TEXT("Original texture -> parameter"));
			Exec = GetSchema()->FindExecutionPin(*SetTexture, EGPD_Output);
			OutNodes.Add(GetMID);
			OutNodes.Add(SetTexture);
		}
		OutNodes.Append({ Event, SetAdjustment, GetSource });
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
			Map.AdjustmentVariable = FName(*(Prefix + TEXT("_Adjustment")));
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
				|| !EnsureStructVariable(AnimBlueprint, Map.AdjustmentVariable, TBaseStructure<FLinearColor>::Get(), TEXT("(R=0.000000,G=0.000000,B=0.000000,A=1.000000)"), Result, Map.ParameterName.ToString() + TEXT(" RGB adjustment (-1 = 0, 0 = original, 1 = 255)")))
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
			const float MapY = BaseY + 900.0f + MapIndex * 2000.0f;
			GenerateTextureTunerApplyGraph(Graph, Maps[MapIndex], MIDVariables, FVector2D(-1450, MapY), Nodes, Result);
			GenerateTextureTunerResetGraph(Graph, Maps[MapIndex], MIDVariables, FVector2D(-1450, MapY + 1100), Nodes, Result);
			GenerateTextureTunerExportGraph(Graph, Maps[MapIndex], FVector2D(-1450, MapY + 1500), Nodes, Result);
		}

		FString Metadata = FString::Printf(TEXT("HT Texture Tuner - %s;Display=%s;Mode=BlackWhiteV1"), *TunerName.ToString(), *SourceMaterial->GetName());
		for (const FTextureTunerMapSpec& Map : Maps)
		{
			Metadata += FString::Printf(
				TEXT(";%s=%s,%s,%s,%s,%s,%s"),
				*Map.ParameterName.ToString(),
				*Map.AdjustmentVariable.ToString(),
				*Map.ApplyEvent.ToString(),
				*Map.ResetEvent.ToString(),
				Map.PreviewTexture ? *Map.PreviewTexture->GetName() : TEXT("Unknown"),
				*Map.ExportEvent.ToString(),
				*Map.ExportFileName);
		}
		const float CommentHeight = 1100.0f + Maps.Num() * 2000.0f;
		const float CommentWidth = 2960.0f + MaterialSlots.Num() * 360.0f;
		UEdGraphNode_Comment* Comment = SpawnCommentBox(
			Graph,
			Metadata,
			FVector2D(-1550, BaseY - 300),
			FVector2D(CommentWidth, CommentHeight),
			FLinearColor(0.20f, 0.08f, 0.32f, 1.0f));
		AddNodesToComment(Comment, Nodes);

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
			Result.Messages.Add(TEXT("RGB adjustments start at 0 (original). Regenerate the NTE panel: -100% = 0, +100% = 255. PNG export uses the processed texture."));
		}
		Result.bSuccess = Result.Errors.IsEmpty();
		return Result;
	}
}

FString FHTBlueprintToggleGeneratorResult::ToDisplayString() const
{
	TArray<FString> Lines;
	if (bSuccess)
	{
		Lines.Add(TEXT("生成完成"));
	}
	else
	{
		Lines.Add(TEXT("生成失败"));
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

	UBlueprint* AnimBlueprint = LoadBlueprint(Params.AnimBlueprintPath, Result, TEXT("动画蓝图"));
	UBlueprint* SaveBlueprint = LoadBlueprint(Params.SaveGameBlueprintPath, Result, TEXT("SaveGame 蓝图"));
	if (!AnimBlueprint || !SaveBlueprint)
	{
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
		Result.Errors.Add(TEXT("SaveGame 蓝图没有可用的 GeneratedClass"));
		return Result;
	}

	if (!SaveClass->IsChildOf(USaveGame::StaticClass()))
	{
		Result.Messages.Add(FString::Printf(TEXT("提示: %s 不是 USaveGame 子类，节点仍会尝试生成。"), *SaveClass->GetName()));
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
			Result.Errors.Add(TEXT("蓝图已生成，但保存资产失败，请在编辑器里手动保存。"));
		}
		else
		{
			Result.Messages.Add(TEXT("资产已保存。"));
		}
	}

	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

#undef LOCTEXT_NAMESPACE
