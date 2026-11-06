// Script entry points: thin JSON wrappers around the existing generators, the toggle graph
// compiler and engine helpers that Python cannot reach. No generator logic lives here.

#include "HTModScriptLibrary.h"

#include "HTBlueprintToggleGenerator.h"
#include "HTMaterialInstanceBuilder.h"
#include "HTMaterialVisibilityInput.h"
#include "ToggleGraph/HTToggleGraph.h"
#include "ToggleGraph/HTToggleGraphCompiler.h"
#include "ToggleGraph/HTToggleGraphNodes.h"
#include "ToggleGraph/HTToggleGraphSchema.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "MaterialEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StaticParameterSet.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "HTModScriptLibrary"

namespace HTModScript
{
	using FJsonValues = TArray<TSharedPtr<FJsonValue>>;

	static FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static TSharedRef<FJsonObject> NewResult()
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetArrayField(TEXT("errors"), {});
		return Result;
	}

	static void AddError(const TSharedRef<FJsonObject>& Result, const FString& Message, const TArray<FString>& NodeIds = {})
	{
		FJsonValues Errors = Result->GetArrayField(TEXT("errors"));
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("message"), Message);
		if (!NodeIds.IsEmpty())
		{
			FJsonValues Ids;
			for (const FString& Id : NodeIds)
			{
				Ids.Add(MakeShared<FJsonValueString>(Id));
			}
			Error->SetArrayField(TEXT("nodes"), Ids);
		}
		Errors.Add(MakeShared<FJsonValueObject>(Error));
		Result->SetArrayField(TEXT("errors"), Errors);
	}

	static FString Fail(const TSharedRef<FJsonObject>& Result, const FString& Message)
	{
		AddError(Result, Message);
		Result->SetBoolField(TEXT("success"), false);
		return ToJsonString(Result);
	}

	static FJsonValues Strings(const TArray<FString>& Values)
	{
		FJsonValues Out;
		for (const FString& Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	static FJsonValues Ints(const TArray<int32>& Values)
	{
		FJsonValues Out;
		for (const int32 Value : Values)
		{
			Out.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return Out;
	}

	static void AddIssues(const TSharedRef<FJsonObject>& Result, const TArray<FHTNestedToggleIssue>& Issues)
	{
		for (const FHTNestedToggleIssue& Issue : Issues)
		{
			AddError(Result, Issue.Message, Issue.NodeIds);
		}
	}

	// "/Game/A/B" -> "/Game/A/B.B"; strips Class'...' wrappers. Empty stays empty.
	static FString NormalizeObjectPath(FString Path)
	{
		Path.TrimStartAndEndInline();
		int32 Quote = INDEX_NONE;
		if (Path.FindChar(TEXT('\''), Quote))
		{
			Path = Path.Mid(Quote + 1);
			Path.RemoveFromEnd(TEXT("'"));
		}
		if (!Path.IsEmpty() && !Path.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(Path))
		{
			Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
		}
		return Path;
	}

	template <typename T>
	static T* LoadByPath(const FString& RawPath)
	{
		const FString Path = NormalizeObjectPath(RawPath);
		return Path.IsEmpty() ? nullptr : LoadObject<T>(nullptr, *Path);
	}

	static FString PathOf(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	template <typename T>
	static FString SoftPath(const TSoftObjectPtr<T>& Ptr)
	{
		return Ptr.IsNull() ? FString() : Ptr.ToSoftObjectPath().ToString();
	}

	static bool ParseJson(const FString& Text, TSharedPtr<FJsonValue>& Out)
	{
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
	}

	static bool ParseObject(const FString& Text, TSharedPtr<FJsonObject>& Out)
	{
		TSharedPtr<FJsonValue> Value;
		if (!ParseJson(Text, Value) || Value->Type != EJson::Object)
		{
			return false;
		}
		Out = Value->AsObject();
		return Out.IsValid();
	}

	static FString GetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const FString& Default = FString())
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Key, Value) ? Value : Default;
	}

	static int32 GetInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const int32 Default = 0)
	{
		int32 Value = 0;
		return Object.IsValid() && Object->TryGetNumberField(Key, Value) ? Value : Default;
	}

	static bool GetBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const bool bDefault = false)
	{
		bool bValue = false;
		return Object.IsValid() && Object->TryGetBoolField(Key, bValue) ? bValue : bDefault;
	}

	static bool GetIntArray(const TSharedPtr<FJsonValue>& Value, TArray<int32>& Out)
	{
		Out.Reset();
		if (!Value.IsValid() || Value->Type != EJson::Array)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			double Number = 0;
			if (!Item.IsValid() || !Item->TryGetNumber(Number))
			{
				return false;
			}
			Out.Add(static_cast<int32>(Number));
		}
		return true;
	}

	static TArray<int32> GetIntArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		TArray<int32> Out;
		if (Object.IsValid() && Object->HasField(Key))
		{
			GetIntArray(Object->TryGetField(Key), Out);
		}
		return Out;
	}

	static TArray<FString> GetStringArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Key, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value.IsValid() && Value->Type == EJson::String ? Value->AsString() : FString());
			}
		}
		return Out;
	}

	// ---- toggle graph helpers ----------------------------------------------------------------

	static UHTToggleGraph* LoadToggleGraph(const FString& Path, const TSharedRef<FJsonObject>& Result)
	{
		UHTToggleGraph* Asset = LoadByPath<UHTToggleGraph>(Path);
		if (!Asset)
		{
			AddError(Result, FString::Printf(TEXT("Toggle graph not found: %s"), *Path));
		}
		return Asset;
	}

	static FString KindOf(const UHTToggleGraphNode* Node)
	{
		if (Cast<UHTToggleGraphNode_Part>(Node)) return TEXT("part");
		if (Cast<UHTToggleGraphNode_TextureSwitch>(Node)) return TEXT("texture_switch");
		if (Cast<UHTToggleGraphNode_MaterialSwitch>(Node)) return TEXT("material_switch");
		if (Cast<UHTToggleGraphNode_Switch>(Node)) return TEXT("switch");
		if (Cast<UHTToggleGraphNode_Group>(Node)) return TEXT("group");
		if (Cast<UHTToggleGraphNode_Generate>(Node)) return TEXT("generate");
		return TEXT("unknown");
	}

	static UClass* ClassOfKind(const FString& Kind)
	{
		if (Kind == TEXT("part")) return UHTToggleGraphNode_Part::StaticClass();
		if (Kind == TEXT("switch")) return UHTToggleGraphNode_Switch::StaticClass();
		if (Kind == TEXT("texture_switch")) return UHTToggleGraphNode_TextureSwitch::StaticClass();
		if (Kind == TEXT("material_switch")) return UHTToggleGraphNode_MaterialSwitch::StaticClass();
		if (Kind == TEXT("group")) return UHTToggleGraphNode_Group::StaticClass();
		return nullptr;
	}

	static FString NodeIdOf(const UEdGraphNode* Node)
	{
		const UHTToggleGraphNode* ToggleNode = Cast<UHTToggleGraphNode>(Node);
		return ToggleNode ? ToggleNode->GetNodeId() : FString();
	}

	static TSharedRef<FJsonObject> DescribeNode(const UHTToggleGraphNode* Node)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("id"), Node->GetNodeId());
		Out->SetStringField(TEXT("kind"), KindOf(Node));
		Out->SetArrayField(TEXT("pos"), Ints({ Node->NodePosX, Node->NodePosY }));
		if (!Node->GetLabel().IsEmpty())
		{
			Out->SetStringField(TEXT("comment"), Node->GetLabel());
		}
		if (Node->IsCollapsed())
		{
			Out->SetBoolField(TEXT("collapsed"), true);
		}
		if (const UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(Node))
		{
			Out->SetArrayField(TEXT("material_ids"), Ints(Part->MaterialIDs));
			Out->SetArrayField(TEXT("effective_material_ids"), Ints(Part->GetEffectiveMaterialIDs()));
			TArray<FString> SlotNames;
			for (const FName Name : Part->SlotNames)
			{
				SlotNames.Add(Name.IsNone() ? FString() : Name.ToString());
			}
			Out->SetArrayField(TEXT("slot_names"), Strings(SlotNames));
			if (!Part->SourceMaterial.IsNull())
			{
				Out->SetStringField(TEXT("by_material"), SoftPath(Part->SourceMaterial));
			}
		}
		if (const UHTToggleGraphNode_SwitchBase* Switch = Cast<UHTToggleGraphNode_SwitchBase>(Node))
		{
			Out->SetStringField(TEXT("variable"), Switch->VariableName);
			Out->SetStringField(TEXT("key"), Switch->KeyName);
			Out->SetNumberField(TEXT("option_count"), Switch->OptionCount);
			Out->SetNumberField(TEXT("initial_option"), Switch->InitialOption);
			Out->SetArrayField(TEXT("option_labels"), Strings(Switch->OptionLabels));
		}
		if (const UHTToggleGraphNode_TextureSwitch* Texture = Cast<UHTToggleGraphNode_TextureSwitch>(Node))
		{
			Out->SetStringField(TEXT("parameter"), Texture->ParameterName);
			FJsonValues Targets;
			for (int32 Target = 0; Target < Texture->GetDynamicInputCount(); ++Target)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("input"), Target);
				const UHTToggleGraphNode_Part* Part = Texture->GetTargetPart(Target);
				Entry->SetStringField(TEXT("part"), Part ? Part->GetNodeId() : FString());
				TArray<FString> Textures;
				for (int32 Option = 0; Option < Texture->OptionCount; ++Option)
				{
					Textures.Add(SoftPath(Texture->GetTargetTexture(Target, Option)));
				}
				Entry->SetArrayField(TEXT("textures"), Strings(Textures));
				Targets.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Out->SetArrayField(TEXT("targets"), Targets);
		}
		if (const UHTToggleGraphNode_MaterialSwitch* Material = Cast<UHTToggleGraphNode_MaterialSwitch>(Node))
		{
			TArray<FString> Materials;
			for (int32 Option = 0; Option < Material->OptionCount; ++Option)
			{
				Materials.Add(SoftPath(Material->GetOptionMaterial(Option)));
			}
			Out->SetArrayField(TEXT("option_materials"), Strings(Materials));
		}
		FJsonValues Inputs;
		FJsonValues OutputTo;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			if (Pin->Direction == EGPD_Input)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("pin"), Pin->PinName.ToString());
				Entry->SetStringField(TEXT("label"), UHTToggleGraphNode::MakePinDisplayName(Pin).ToString());
				TArray<FString> From;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked)
					{
						From.Add(NodeIdOf(Linked->GetOwningNode()));
					}
				}
				Entry->SetArrayField(TEXT("from"), Strings(From));
				Inputs.Add(MakeShared<FJsonValueObject>(Entry));
			}
			else
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked)
					{
						TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
						Entry->SetStringField(TEXT("node"), NodeIdOf(Linked->GetOwningNode()));
						Entry->SetStringField(TEXT("pin"), Linked->PinName.ToString());
						OutputTo.Add(MakeShared<FJsonValueObject>(Entry));
					}
				}
			}
		}
		Out->SetArrayField(TEXT("inputs"), Inputs);
		Out->SetArrayField(TEXT("output_to"), OutputTo);
		return Out;
	}

	static UHTToggleGraphNode* ResolveNode(UEdGraph& Graph, const TMap<FString, UHTToggleGraphNode*>& Aliases, const FString& Ref, FString& OutError)
	{
		if (UHTToggleGraphNode* const* Aliased = Aliases.Find(Ref))
		{
			return *Aliased;
		}
		FString GuidPart = Ref;
		int32 Hash = INDEX_NONE;
		if (Ref.FindLastChar(TEXT('#'), Hash))
		{
			GuidPart = Ref.Mid(Hash + 1);
		}
		TArray<UHTToggleGraphNode*> ByLabel;
		for (UEdGraphNode* EdNode : Graph.Nodes)
		{
			UHTToggleGraphNode* Node = Cast<UHTToggleGraphNode>(EdNode);
			if (!Node)
			{
				continue;
			}
			if (Node->GetNodeId() == Ref)
			{
				return Node;
			}
			if (GuidPart.Len() >= 6 && Node->NodeGuid.ToString(EGuidFormats::Digits).StartsWith(GuidPart, ESearchCase::IgnoreCase))
			{
				return Node;
			}
			if (!Node->GetLabel().IsEmpty() && Node->GetLabel() == Ref)
			{
				ByLabel.Add(Node);
			}
		}
		if (ByLabel.Num() == 1)
		{
			return ByLabel[0];
		}
		OutError = ByLabel.Num() > 1
			? FString::Printf(TEXT("Several nodes have the comment '%s'; use the node id."), *Ref)
			: FString::Printf(TEXT("No node '%s' (use the id from describe, an alias from this batch, or a unique comment)."), *Ref);
		return nullptr;
	}

	static UEdGraphPin* ResolveInputPin(UHTToggleGraphNode& Node, const FString& RawSpec, FString& OutError)
	{
		FString Spec = RawSpec.TrimStartAndEnd().ToLower();
		Spec.ReplaceInline(TEXT("_"), TEXT(" "));
		if (Spec.IsEmpty() || Spec == TEXT("input") || Spec == TEXT("in") || Spec == TEXT("target") || Spec == TEXT("slots"))
		{
			if (Cast<UHTToggleGraphNode_Switch>(&Node) && !Cast<UHTToggleGraphNode_SwitchBase>(&Node)->AcceptsTargetParts())
			{
				OutError = TEXT("A switch takes nodes on its options: give pin \"option N\".");
				return nullptr;
			}
			if (Node.HasDynamicInputs())
			{
				for (int32 Index = 0; Index < Node.GetDynamicInputCount(); ++Index)
				{
					UEdGraphPin* Pin = Node.GetDynamicInputPin(Index);
					if (Pin && Pin->LinkedTo.IsEmpty())
					{
						return Pin;
					}
				}
			}
			if (UEdGraphPin* Pin = Node.GetInputPin())
			{
				return Pin;
			}
			OutError = TEXT("The node has no free input.");
			return nullptr;
		}
		int32 Number = INDEX_NONE;
		FString Word;
		FString Digits;
		if (Spec.Split(TEXT(" "), &Word, &Digits, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && Digits.IsNumeric())
		{
			Number = FCString::Atoi(*Digits);
		}
		if (Number != INDEX_NONE && Word == TEXT("option"))
		{
			if (UHTToggleGraphNode_Switch* Switch = Cast<UHTToggleGraphNode_Switch>(&Node))
			{
				if (UEdGraphPin* Pin = Switch->GetOptionPin(Number))
				{
					return Pin;
				}
			}
			OutError = FString::Printf(TEXT("No option %d on this node."), Number);
			return nullptr;
		}
		if (Number != INDEX_NONE && (Word == TEXT("input") || Word == TEXT("in") || Word == TEXT("target") || Word == TEXT("slots")))
		{
			if (UEdGraphPin* Pin = Node.GetDynamicInputPin(Number))
			{
				return Pin;
			}
			OutError = FString::Printf(TEXT("No input %d on this node (it has %d)."), Number, Node.GetDynamicInputCount());
			return nullptr;
		}
		if (UEdGraphPin* Pin = Node.FindPin(FName(*RawSpec), EGPD_Input))
		{
			return Pin;
		}
		OutError = FString::Printf(TEXT("Unknown pin '%s' (use \"option N\", \"input N\" or a pin name from describe)."), *RawSpec);
		return nullptr;
	}

	static const TCHAR* PropOrder[] = {
		TEXT("comment"), TEXT("pos"), TEXT("collapsed"), TEXT("material_ids"), TEXT("by_material"), TEXT("variable"),
		TEXT("key"), TEXT("option_count"), TEXT("initial_option"), TEXT("option_labels"), TEXT("parameter"),
		TEXT("textures"), TEXT("option_materials") };

	static bool ApplyProps(UHTToggleGraphNode& Node, const TSharedPtr<FJsonObject>& Props, FString& OutError)
	{
		if (!Props.IsValid())
		{
			return true;
		}
		UHTToggleGraphNode_Part* Part = Cast<UHTToggleGraphNode_Part>(&Node);
		UHTToggleGraphNode_SwitchBase* Switch = Cast<UHTToggleGraphNode_SwitchBase>(&Node);
		UHTToggleGraphNode_TextureSwitch* Texture = Cast<UHTToggleGraphNode_TextureSwitch>(&Node);
		UHTToggleGraphNode_MaterialSwitch* Material = Cast<UHTToggleGraphNode_MaterialSwitch>(&Node);

		TSet<FString> Allowed = { TEXT("comment"), TEXT("pos"), TEXT("collapsed") };
		if (Part) Allowed.Append({ TEXT("material_ids"), TEXT("by_material") });
		if (Switch) Allowed.Append({ TEXT("variable"), TEXT("key"), TEXT("option_count"), TEXT("initial_option"), TEXT("option_labels") });
		if (Texture) Allowed.Append({ TEXT("parameter"), TEXT("textures") });
		if (Material) Allowed.Add(TEXT("option_materials"));
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Props->Values)
		{
			if (!Allowed.Contains(Pair.Key))
			{
				OutError = FString::Printf(TEXT("A %s node has no property '%s' (allowed: %s)."), *KindOf(&Node), *Pair.Key,
					*FString::Join(Allowed.Array(), TEXT(", ")));
				return false;
			}
		}

		for (const TCHAR* Key : PropOrder)
		{
			const TSharedPtr<FJsonValue> Value = Props->TryGetField(Key);
			if (!Value.IsValid())
			{
				continue;
			}
			const FString Name(Key);
			if (Name == TEXT("comment"))
			{
				Node.SetLabelInline(Value->AsString());
			}
			else if (Name == TEXT("pos"))
			{
				TArray<int32> Pos;
				if (!GetIntArray(Value, Pos) || Pos.Num() != 2)
				{
					OutError = TEXT("pos must be [x, y].");
					return false;
				}
				Node.Modify();
				Node.NodePosX = Pos[0];
				Node.NodePosY = Pos[1];
			}
			else if (Name == TEXT("collapsed"))
			{
				Node.SetCollapsedInline(Value->AsBool());
			}
			else if (Name == TEXT("material_ids"))
			{
				TArray<int32> IDs;
				if (!GetIntArray(Value, IDs))
				{
					OutError = TEXT("material_ids must be a list of slot indices.");
					return false;
				}
				Part->SetMaterialIDsInline(IDs);
			}
			else if (Name == TEXT("by_material"))
			{
				UMaterialInterface* Mat = nullptr;
				if (Value->Type == EJson::String && !Value->AsString().IsEmpty())
				{
					Mat = LoadByPath<UMaterialInterface>(Value->AsString());
					if (!Mat)
					{
						OutError = FString::Printf(TEXT("Material not found: %s"), *Value->AsString());
						return false;
					}
				}
				Part->SetSourceMaterialInline(Mat);
			}
			else if (Name == TEXT("variable"))
			{
				Switch->SetVariableNameInline(Value->AsString());
			}
			else if (Name == TEXT("key"))
			{
				Switch->SetKeyNameInline(Value->AsString());
			}
			else if (Name == TEXT("option_count"))
			{
				const int32 Count = static_cast<int32>(Value->AsNumber());
				if (Count < UHTToggleGraphNode_SwitchBase::MinOptions || Count > UHTToggleGraphNode_SwitchBase::MaxOptions)
				{
					OutError = FString::Printf(TEXT("option_count must be %d..%d."), UHTToggleGraphNode_SwitchBase::MinOptions, UHTToggleGraphNode_SwitchBase::MaxOptions);
					return false;
				}
				Switch->SetOptionCountInline(Count);
			}
			else if (Name == TEXT("initial_option"))
			{
				const int32 Option = static_cast<int32>(Value->AsNumber());
				if (Option < 0 || Option >= Switch->OptionCount)
				{
					OutError = FString::Printf(TEXT("initial_option must be 0..%d."), Switch->OptionCount - 1);
					return false;
				}
				Switch->SetInitialOptionInline(Option);
			}
			else if (Name == TEXT("option_labels"))
			{
				const TArray<TSharedPtr<FJsonValue>>& Labels = Value->AsArray();
				for (int32 Option = 0; Option < Labels.Num() && Option < Switch->OptionCount; ++Option)
				{
					Switch->SetOptionLabelInline(Option, Labels[Option].IsValid() ? Labels[Option]->AsString() : FString());
				}
			}
			else if (Name == TEXT("parameter"))
			{
				Texture->SetParameterNameInline(Value->AsString());
			}
			else if (Name == TEXT("textures"))
			{
				// [[target 0: option 0 texture, option 1 texture, ...], [target 1: ...]]; null = keep the material's own.
				const TArray<TSharedPtr<FJsonValue>>& Targets = Value->AsArray();
				for (int32 Target = 0; Target < Targets.Num(); ++Target)
				{
					const TArray<TSharedPtr<FJsonValue>>& Options = Targets[Target]->AsArray();
					for (int32 Option = 0; Option < Options.Num() && Option < Texture->OptionCount; ++Option)
					{
						UTexture* Tex = nullptr;
						if (Options[Option].IsValid() && Options[Option]->Type == EJson::String && !Options[Option]->AsString().IsEmpty())
						{
							Tex = LoadByPath<UTexture>(Options[Option]->AsString());
							if (!Tex)
							{
								OutError = FString::Printf(TEXT("Texture not found: %s"), *Options[Option]->AsString());
								return false;
							}
						}
						Texture->SetTargetTextureInline(Target, Option, Tex);
					}
				}
			}
			else if (Name == TEXT("option_materials"))
			{
				const TArray<TSharedPtr<FJsonValue>>& Materials = Value->AsArray();
				for (int32 Option = 0; Option < Materials.Num() && Option < Material->OptionCount; ++Option)
				{
					UMaterialInterface* Mat = nullptr;
					if (Materials[Option].IsValid() && Materials[Option]->Type == EJson::String && !Materials[Option]->AsString().IsEmpty())
					{
						Mat = LoadByPath<UMaterialInterface>(Materials[Option]->AsString());
						if (!Mat)
						{
							OutError = FString::Printf(TEXT("Material not found: %s"), *Materials[Option]->AsString());
							return false;
						}
					}
					Material->SetOptionMaterialInline(Option, Mat);
				}
			}
		}
		return true;
	}

	static FVector2f FreePosition(const UEdGraph& Graph)
	{
		int32 MinX = 0;
		int32 MaxY = 0;
		bool bAny = false;
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node)
			{
				MinX = bAny ? FMath::Min(MinX, Node->NodePosX) : Node->NodePosX;
				MaxY = bAny ? FMath::Max(MaxY, Node->NodePosY) : Node->NodePosY;
				bAny = true;
			}
		}
		return FVector2f(MinX - 400.0f, MaxY + 200.0f);
	}

	static bool SetTarget(UHTToggleGraph& Asset, const TSharedPtr<FJsonObject>& Op, FString& OutError)
	{
		Asset.Modify();
		FString Path;
		if (Op->TryGetStringField(TEXT("anim_blueprint"), Path))
		{
			UAnimBlueprint* AnimBlueprint = Path.IsEmpty() ? nullptr : LoadByPath<UAnimBlueprint>(Path);
			if (!Path.IsEmpty() && !AnimBlueprint)
			{
				OutError = FString::Printf(TEXT("Animation Blueprint not found: %s"), *Path);
				return false;
			}
			Asset.AnimBlueprint = AnimBlueprint;
		}
		if (Op->TryGetStringField(TEXT("savegame_blueprint"), Path))
		{
			UBlueprint* SaveGame = Path.IsEmpty() ? nullptr : LoadByPath<UBlueprint>(Path);
			if (!Path.IsEmpty() && !SaveGame)
			{
				OutError = FString::Printf(TEXT("SaveGame Blueprint not found: %s"), *Path);
				return false;
			}
			Asset.SaveGameBlueprint = SaveGame;
		}
		FString Text;
		if (Op->TryGetStringField(TEXT("character_name"), Text))
		{
			Asset.CharacterName = Text.TrimStartAndEnd();
		}
		if (Op->TryGetStringField(TEXT("tree_name"), Text))
		{
			Asset.TreeName = Text.TrimStartAndEnd();
		}
		int32 Number = 0;
		if (Op->TryGetNumberField(TEXT("section_index"), Number))
		{
			Asset.SectionIndex = FMath::Max(0, Number);
		}
		if (Op->TryGetNumberField(TEXT("lod_index"), Number))
		{
			Asset.LODIndex = FMath::Max(0, Number);
		}
		bool bFlag = false;
		if (Op->TryGetBoolField(TEXT("save_after_compile"), bFlag))
		{
			Asset.bSaveBlueprintsAfterCompile = bFlag;
		}
		return true;
	}

	static bool ApplyOperation(UHTToggleGraph& Asset, UEdGraph& Graph, const TSharedPtr<FJsonObject>& Op,
		TMap<FString, UHTToggleGraphNode*>& Aliases, const TSharedRef<FJsonObject>& Done, FString& OutError)
	{
		if (!Op.IsValid())
		{
			OutError = TEXT("Each operation must be an object.");
			return false;
		}
		const FString Kind = GetString(Op, TEXT("op"));
		Done->SetStringField(TEXT("op"), Kind);
		if (Kind == TEXT("add"))
		{
			const FString NodeKind = GetString(Op, TEXT("kind"));
			UClass* Class = ClassOfKind(NodeKind);
			if (!Class)
			{
				OutError = FString::Printf(TEXT("Unknown node kind '%s' (part, switch, texture_switch, material_switch, group)."), *NodeKind);
				return false;
			}
			FVector2f Location = FreePosition(Graph);
			TArray<int32> Pos = GetIntArrayField(Op, TEXT("pos"));
			if (Pos.Num() == 2)
			{
				Location = FVector2f(Pos[0], Pos[1]);
			}
			UHTToggleGraphNode* Node = FHTToggleGraphSchemaAction_NewNode::SpawnNode(&Graph, Class, Location, nullptr, false);
			if (!Node)
			{
				OutError = TEXT("The node could not be created.");
				return false;
			}
			const FString Alias = GetString(Op, TEXT("alias"));
			if (!Alias.IsEmpty())
			{
				Aliases.Add(Alias, Node);
				Done->SetStringField(TEXT("alias"), Alias);
			}
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (Op->TryGetObjectField(TEXT("props"), Props) && !ApplyProps(*Node, *Props, OutError))
			{
				return false;
			}
			Done->SetStringField(TEXT("node"), Node->GetNodeId());
			return true;
		}
		if (Kind == TEXT("set"))
		{
			UHTToggleGraphNode* Node = ResolveNode(Graph, Aliases, GetString(Op, TEXT("node")), OutError);
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (!Node || !Op->TryGetObjectField(TEXT("props"), Props))
			{
				OutError = OutError.IsEmpty() ? TEXT("set needs \"props\".") : OutError;
				return false;
			}
			if (!ApplyProps(*Node, *Props, OutError))
			{
				return false;
			}
			Done->SetStringField(TEXT("node"), Node->GetNodeId());
			return true;
		}
		if (Kind == TEXT("connect") || Kind == TEXT("disconnect"))
		{
			UHTToggleGraphNode* From = ResolveNode(Graph, Aliases, GetString(Op, TEXT("from")), OutError);
			UHTToggleGraphNode* To = From ? ResolveNode(Graph, Aliases, GetString(Op, TEXT("to")), OutError) : nullptr;
			if (!From || !To)
			{
				return false;
			}
			UEdGraphPin* Output = From->GetOutputPin();
			if (!Output)
			{
				OutError = TEXT("The source node has no output.");
				return false;
			}
			const UEdGraphSchema* Schema = Graph.GetSchema();
			if (Kind == TEXT("disconnect"))
			{
				int32 Broken = 0;
				for (UEdGraphPin* Pin : To->Pins)
				{
					if (Pin && Output->LinkedTo.Contains(Pin))
					{
						Schema->BreakSinglePinLink(Output, Pin);
						++Broken;
					}
				}
				if (Broken == 0)
				{
					OutError = TEXT("Those nodes are not connected.");
					return false;
				}
				Done->SetNumberField(TEXT("links_removed"), Broken);
				return true;
			}
			UEdGraphPin* Input = ResolveInputPin(*To, GetString(Op, TEXT("pin")), OutError);
			if (!Input)
			{
				return false;
			}
			const FPinConnectionResponse Response = Schema->CanCreateConnection(Output, Input);
			if (Response.Response == CONNECT_RESPONSE_DISALLOW || !Schema->TryCreateConnection(Output, Input))
			{
				OutError = FString::Printf(TEXT("Cannot connect: %s"), *Response.Message.ToString());
				return false;
			}
			Done->SetStringField(TEXT("from"), From->GetNodeId());
			Done->SetStringField(TEXT("to"), To->GetNodeId());
			Done->SetStringField(TEXT("pin"), Input->PinName.ToString());
			return true;
		}
		if (Kind == TEXT("remove"))
		{
			UHTToggleGraphNode* Node = ResolveNode(Graph, Aliases, GetString(Op, TEXT("node")), OutError);
			if (!Node)
			{
				return false;
			}
			if (Cast<UHTToggleGraphNode_Generate>(Node))
			{
				OutError = TEXT("The generate node cannot be removed.");
				return false;
			}
			Done->SetStringField(TEXT("node"), Node->GetNodeId());
			for (auto It = Aliases.CreateIterator(); It; ++It)
			{
				if (It.Value() == Node)
				{
					It.RemoveCurrent();
				}
			}
			Node->Modify();
			Node->DestroyNode();
			return true;
		}
		if (Kind == TEXT("set_target"))
		{
			return SetTarget(Asset, Op, OutError);
		}
		OutError = FString::Printf(TEXT("Unknown op '%s' (add, set, connect, disconnect, remove, set_target)."), *Kind);
		return false;
	}

	// Same rule as the main panel: the character folder name without a leading "<digits>_".
	static FString MakeCharacterScopedSlotName(const FString& VariableName, const FString& CharacterFolder)
	{
		FString SlotName = VariableName.TrimStartAndEnd();
		FString FolderName = FPackageName::GetLongPackageAssetName(CharacterFolder);
		int32 Underscore = INDEX_NONE;
		if (FolderName.FindChar(TEXT('_'), Underscore))
		{
			const FString Prefix = FolderName.Left(Underscore);
			if (!Prefix.IsEmpty() && Prefix.IsNumeric() && Underscore + 1 < FolderName.Len())
			{
				FolderName = FolderName.Mid(Underscore + 1);
			}
		}
		const FString CharacterName = ObjectTools::SanitizeObjectName(FolderName);
		if (!SlotName.IsEmpty() && !CharacterName.IsEmpty())
		{
			SlotName += TEXT(" ") + CharacterName;
		}
		return SlotName;
	}
}

// ---- Materials ------------------------------------------------------------------------------

TArray<FName> UHTModScriptLibrary::GetVisibleMaterialParameters(UMaterialInterface* Material)
{
	TArray<FName> Names;
	UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
	if (!Base)
	{
		return Names;
	}
	UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	UMaterialInstanceConstant* Transient = nullptr;
	if (!Instance)
	{
		Transient = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
		Transient->SetParentEditorOnly(Material, false);
		Instance = Transient;
	}
	TArray<FMaterialParameterInfo> Visible;
	FMaterialEditorUtilities::GetVisibleMaterialParameters(Base, Instance, Visible);
	for (const FMaterialParameterInfo& Info : Visible)
	{
		Names.AddUnique(Info.Name);
	}
	if (Transient)
	{
		Transient->MarkAsGarbage();
	}
	return Names;
}

TArray<UMaterialExpression*> UHTModScriptLibrary::GetMaterialExpressions(UObject* MaterialOrFunction)
{
	TArray<UMaterialExpression*> Out;
	if (const UMaterial* Material = Cast<UMaterial>(MaterialOrFunction))
	{
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (Expression)
			{
				Out.Add(Expression);
			}
		}
	}
	else if (const UMaterialFunction* Function = Cast<UMaterialFunction>(MaterialOrFunction))
	{
		for (UMaterialExpression* Expression : Function->GetExpressions())
		{
			if (Expression)
			{
				Out.Add(Expression);
			}
		}
	}
	return Out;
}

bool UHTModScriptLibrary::IsStaticSwitchOverridden(UMaterialInstance* Instance, FName ParameterName)
{
	if (!Instance)
	{
		return false;
	}
	const FStaticParameterSet Params = Instance->GetStaticParameters();
	for (const FStaticSwitchParameter& Param : Params.StaticSwitchParameters)
	{
		if (Param.ParameterInfo.Name == ParameterName && Param.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
		{
			return Param.bOverride;
		}
	}
	for (const FStaticComponentMaskParameter& Param : Params.EditorOnly.StaticComponentMaskParameters)
	{
		if (Param.ParameterInfo.Name == ParameterName && Param.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
		{
			return Param.bOverride;
		}
	}
	return false;
}

bool UHTModScriptLibrary::ClearMaterialInstanceParameter(UMaterialInstanceConstant* Instance, FName ParameterName)
{
	if (!Instance)
	{
		return false;
	}
	FStaticParameterSet Params = Instance->GetStaticParameters();
	bool bChanged = false;
	for (FStaticSwitchParameter& Param : Params.StaticSwitchParameters)
	{
		if (Param.ParameterInfo.Name == ParameterName && Param.bOverride)
		{
			Param.bOverride = false;
			bChanged = true;
		}
	}
	for (FStaticComponentMaskParameter& Param : Params.EditorOnly.StaticComponentMaskParameters)
	{
		if (Param.ParameterInfo.Name == ParameterName && Param.bOverride)
		{
			Param.bOverride = false;
			bChanged = true;
		}
	}
	if (!bChanged)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("ClearStaticOverride", "Clear Static Parameter Override"));
	Instance->Modify();
	Instance->UpdateStaticPermutation(Params);
	Instance->MarkPackageDirty();
	return true;
}

// ---- Asset editors --------------------------------------------------------------------------

TArray<UObject*> UHTModScriptLibrary::GetOpenAssetEditors()
{
	UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetAllEditedAssets() : TArray<UObject*>();
}

bool UHTModScriptLibrary::IsAssetOpenInEditor(UObject* Asset)
{
	UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	return Subsystem && Asset && Subsystem->FindEditorForAsset(Asset, false) != nullptr;
}

// ---- Toggle trees ---------------------------------------------------------------------------

FString UHTModScriptLibrary::DescribeToggleGraph(const FString& ToggleGraphPath)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	UHTToggleGraph* Asset = LoadToggleGraph(ToggleGraphPath, Result);
	if (!Asset)
	{
		return ToJsonString(Result);
	}
	UEdGraph* Graph = Asset->EnsureGraph();
	Result->SetStringField(TEXT("asset"), Asset->GetPathName());
	Result->SetStringField(TEXT("tree_name"), Asset->GetEffectiveTreeName());
	Result->SetStringField(TEXT("anim_blueprint"), SoftPath(Asset->AnimBlueprint));
	Result->SetStringField(TEXT("savegame_blueprint"), SoftPath(Asset->SaveGameBlueprint));
	Result->SetStringField(TEXT("character_name"), Asset->CharacterName);
	Result->SetNumberField(TEXT("section_index"), Asset->SectionIndex);
	Result->SetNumberField(TEXT("lod_index"), Asset->LODIndex);
	Result->SetBoolField(TEXT("save_after_compile"), Asset->bSaveBlueprintsAfterCompile);
	Result->SetStringField(TEXT("preview_mesh"), PathOf(Asset->ResolvePreviewMesh()));
	FJsonValues Nodes;
	for (const UEdGraphNode* EdNode : Graph ? Graph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
	{
		if (const UHTToggleGraphNode* Node = Cast<UHTToggleGraphNode>(EdNode))
		{
			Nodes.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
		}
	}
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetBoolField(TEXT("success"), true);
	return ToJsonString(Result);
}

FString UHTModScriptLibrary::CheckToggleGraph(const FString& ToggleGraphPath)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	UHTToggleGraph* Asset = LoadToggleGraph(ToggleGraphPath, Result);
	if (!Asset)
	{
		return ToJsonString(Result);
	}
	FHTNestedToggleGraph Model;
	TArray<FHTNestedToggleIssue> Issues;
	TArray<FString> Warnings;
	const bool bModel = FHTToggleGraphCompiler::CompileToModel(Asset, Model, Issues, Warnings);
	FHTNestedToggleCompiledGraph Compiled;
	const bool bCompiled = bModel && FHTBlueprintToggleGenerator::CompileNestedToggleGraph(Model, Compiled, Issues);
	const FHTPartSlotReport Slots = FHTToggleGraphCompiler::CheckPartSlots(Asset);
	AddIssues(Result, Issues);
	AddIssues(Result, Slots.Errors);
	if (Asset->AnimBlueprint.IsNull() || Asset->SaveGameBlueprint.IsNull())
	{
		Warnings.Add(TEXT("The Animation Blueprint or SaveGame Blueprint is not set; Compile will refuse."));
	}
	Result->SetArrayField(TEXT("warnings"), Strings(Warnings));
	TSharedRef<FJsonObject> SlotReport = MakeShared<FJsonObject>();
	SlotReport->SetArrayField(TEXT("changes"), Strings(Slots.Changes));
	SlotReport->SetArrayField(TEXT("bindings"), Strings(Slots.Bindings));
	Result->SetObjectField(TEXT("slot_report"), SlotReport);
	Result->SetNumberField(TEXT("switches"), Model.Switches.Num());
	Result->SetNumberField(TEXT("parts"), Model.Parts.Num());
	if (bCompiled)
	{
		FJsonValues Variables;
		for (const FHTNestedToggleStateVariable& Variable : Compiled.Variables)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Variable.VariableName.ToString());
			Entry->SetArrayField(TEXT("keys"), Strings(Variable.KeyNames));
			Entry->SetNumberField(TEXT("option_count"), Variable.OptionCount);
			Entry->SetNumberField(TEXT("initial_option"), Variable.InitialOption);
			TArray<FString> SwitchIds;
			for (const int32 Index : Variable.SwitchIndices)
			{
				if (Model.Switches.IsValidIndex(Index))
				{
					SwitchIds.Add(Model.Switches[Index].NodeId);
				}
			}
			Entry->SetArrayField(TEXT("switches"), Strings(SwitchIds));
			Variables.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("variables"), Variables);
		Result->SetArrayField(TEXT("effect_material_ids"), Ints(Compiled.EffectMaterialIDs));
	}
	Result->SetBoolField(TEXT("success"), bModel && bCompiled && Slots.Errors.IsEmpty());
	return ToJsonString(Result);
}

FString UHTModScriptLibrary::RefreshToggleGraphSlots(const FString& ToggleGraphPath)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	UHTToggleGraph* Asset = LoadToggleGraph(ToggleGraphPath, Result);
	if (!Asset)
	{
		return ToJsonString(Result);
	}
	const FHTPartSlotReport Report = FHTToggleGraphCompiler::RefreshPartSlots(Asset);
	AddIssues(Result, Report.Errors);
	Result->SetNumberField(TEXT("changed_parts"), Report.ChangedParts);
	Result->SetArrayField(TEXT("changes"), Strings(Report.Changes));
	Result->SetArrayField(TEXT("bindings"), Strings(Report.Bindings));
	Result->SetBoolField(TEXT("success"), Report.Errors.IsEmpty());
	return ToJsonString(Result);
}

FString UHTModScriptLibrary::GenerateToggleGraph(const FString& ToggleGraphPath, bool bSaveAssets)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	UHTToggleGraph* Asset = LoadToggleGraph(ToggleGraphPath, Result);
	if (!Asset)
	{
		return ToJsonString(Result);
	}
	const FHTNestedToggleGeneratorResult Generated = FHTToggleGraphCompiler::Generate(Asset, bSaveAssets);
	AddIssues(Result, Generated.Errors);
	Result->SetArrayField(TEXT("messages"), Strings(Generated.Messages));
	FJsonValues Variables;
	for (const FHTNestedToggleStateVariable& Variable : Generated.Variables)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Variable.VariableName.ToString());
		Entry->SetArrayField(TEXT("keys"), Strings(Variable.KeyNames));
		Entry->SetNumberField(TEXT("option_count"), Variable.OptionCount);
		Entry->SetNumberField(TEXT("initial_option"), Variable.InitialOption);
		Variables.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("variables"), Variables);
	Result->SetStringField(TEXT("anim_blueprint"), SoftPath(Asset->AnimBlueprint));
	Result->SetBoolField(TEXT("saved"), Generated.bSuccess && bSaveAssets);
	Result->SetBoolField(TEXT("success"), Generated.bSuccess);
	return ToJsonString(Result);
}

FString UHTModScriptLibrary::EditToggleGraph(const FString& ToggleGraphPath, const FString& OperationsJson)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	UHTToggleGraph* Asset = LoadToggleGraph(ToggleGraphPath, Result);
	if (!Asset)
	{
		return ToJsonString(Result);
	}
	TSharedPtr<FJsonValue> Parsed;
	if (!ParseJson(OperationsJson, Parsed))
	{
		return Fail(Result, TEXT("Operations are not valid JSON."));
	}
	TArray<TSharedPtr<FJsonValue>> Operations;
	bool bKeepPartial = false;
	if (Parsed->Type == EJson::Array)
	{
		Operations = Parsed->AsArray();
	}
	else if (Parsed->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Wrapper = Parsed->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Wrapper->TryGetArrayField(TEXT("operations"), List))
		{
			Operations = *List;
		}
		bKeepPartial = GetBool(Wrapper, TEXT("keep_partial"));
	}
	if (Operations.IsEmpty())
	{
		return Fail(Result, TEXT("Give a JSON array of operations, or {\"operations\": [...]}."));
	}

	UEdGraph* Graph = Asset->EnsureGraph();
	if (!Graph)
	{
		return Fail(Result, TEXT("The asset has no graph."));
	}
	// An outer transaction (opened by the caller) cannot be undone from here.
	const bool bNested = GEditor && GEditor->IsTransactionActive();
	TMap<FString, UHTToggleGraphNode*> Aliases;
	FJsonValues Applied;
	int32 FailedIndex = INDEX_NONE;
	FString FailMessage;
	{
		const FScopedTransaction Transaction(LOCTEXT("EditToggleGraph", "Edit Toggle Graph (script)"));
		Asset->Modify();
		Graph->Modify();
		for (int32 Index = 0; Index < Operations.Num(); ++Index)
		{
			const TSharedRef<FJsonObject> Done = MakeShared<FJsonObject>();
			FString Error;
			const TSharedPtr<FJsonObject> Op = Operations[Index].IsValid() ? Operations[Index]->AsObject() : nullptr;
			if (!ApplyOperation(*Asset, *Graph, Op, Aliases, Done, Error))
			{
				FailedIndex = Index;
				FailMessage = Error;
				break;
			}
			Applied.Add(MakeShared<FJsonValueObject>(Done));
		}
		Graph->NotifyGraphChanged();
		Asset->MarkPackageDirty();
	}
	bool bUndone = false;
	if (FailedIndex != INDEX_NONE && !bKeepPartial && !bNested && GEditor)
	{
		bUndone = GEditor->UndoTransaction();
	}
	TSharedRef<FJsonObject> Created = MakeShared<FJsonObject>();
	for (const TPair<FString, UHTToggleGraphNode*>& Pair : Aliases)
	{
		if (IsValid(Pair.Value))
		{
			Created->SetStringField(Pair.Key, Pair.Value->GetNodeId());
		}
	}
	Result->SetObjectField(TEXT("aliases"), Created);
	Result->SetArrayField(TEXT("applied"), Applied);
	if (FailedIndex != INDEX_NONE)
	{
		AddError(Result, FString::Printf(TEXT("Operation %d failed: %s"), FailedIndex, *FailMessage));
		Result->SetNumberField(TEXT("failed_operation"), FailedIndex);
		Result->SetBoolField(TEXT("undone"), bUndone);
	}
	Result->SetBoolField(TEXT("success"), FailedIndex == INDEX_NONE);
	return ToJsonString(Result);
}

// ---- Generators -----------------------------------------------------------------------------

FString UHTModScriptLibrary::GenerateToggle(const FString& ParamsJson)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	TSharedPtr<FJsonObject> In;
	if (!ParseObject(ParamsJson, In))
	{
		return Fail(Result, TEXT("Parameters are not a JSON object."));
	}
	const FString Mode = GetString(In, TEXT("mode"), TEXT("visibility")).ToLower();
	FHTBlueprintToggleGeneratorParams Params;
	if (Mode == TEXT("visibility") || Mode == TEXT("material_section"))
	{
		Params.Mode = EHTBlueprintToggleMode::MaterialSection;
	}
	else if (Mode == TEXT("texture"))
	{
		Params.Mode = EHTBlueprintToggleMode::Texture;
	}
	else if (Mode == TEXT("material") || Mode == TEXT("material_interface"))
	{
		Params.Mode = EHTBlueprintToggleMode::MaterialInterface;
	}
	else if (Mode == TEXT("texture_tuner") || Mode == TEXT("texture_channel_tuner"))
	{
		Params.Mode = EHTBlueprintToggleMode::TextureChannelTuner;
	}
	else
	{
		return Fail(Result, TEXT("mode must be visibility, texture, material or texture_tuner."));
	}

	Params.AnimBlueprintPath = NormalizeObjectPath(GetString(In, TEXT("anim_blueprint")));
	Params.SaveGameBlueprintPath = NormalizeObjectPath(GetString(In, TEXT("savegame_blueprint")));
	Params.ToggleVariableName = GetString(In, TEXT("variable")).TrimStartAndEnd();
	Params.KeyName = GetString(In, TEXT("key")).TrimStartAndEnd();
	if (Params.AnimBlueprintPath.IsEmpty() || Params.SaveGameBlueprintPath.IsEmpty() || Params.ToggleVariableName.IsEmpty())
	{
		return Fail(Result, TEXT("anim_blueprint, savegame_blueprint and variable are required."));
	}
	if (Params.KeyName.IsEmpty() && Params.Mode != EHTBlueprintToggleMode::TextureChannelTuner)
	{
		return Fail(Result, TEXT("key is required."));
	}
	FString CharacterFolder = GetString(In, TEXT("character_folder"));
	if (CharacterFolder.IsEmpty())
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(Params.AnimBlueprintPath);
		CharacterFolder = FPackageName::IsValidLongPackageName(PackageName) ? FPackageName::GetLongPackagePath(PackageName) : TEXT("/Game/Characters/Player");
	}
	Params.SlotName = GetString(In, TEXT("slot_name"), MakeCharacterScopedSlotName(Params.ToggleVariableName, CharacterFolder));

	const TArray<int32> Slots = GetIntArrayField(In, TEXT("slots"));
	Params.MaterialElementIndices = Slots;
	Params.MaterialElementIndex = Slots.Num() > 0 ? Slots[0] : 0;
	if (Params.Mode == EHTBlueprintToggleMode::MaterialSection)
	{
		const FString Visibility = GetString(In, TEXT("visibility"));
		FString Error;
		if (!HTMaterialVisibilityInput::Parse(Visibility, Params.MaterialVisibilityGroups, Error))
		{
			return Fail(Result, Error.IsEmpty() ? TEXT("visibility: give Material IDs like \"1+2;3\".") : Error);
		}
		for (const FHTMaterialVisibilityGroup& Group : Params.MaterialVisibilityGroups)
		{
			Params.MaterialIDs.Append(Group.MaterialIDs);
		}
		Params.MaterialID = Params.MaterialIDs.Num() > 0 ? Params.MaterialIDs[0] : 0;
		Params.bToggleMaterialIDsTogether = Params.MaterialVisibilityGroups.Num() == 1 && Params.MaterialVisibilityGroups[0].MaterialIDs.Num() > 1;
		Params.bIncludeHiddenMaterialState = !HTMaterialVisibilityInput::IsGroupCycle(Visibility);
		Params.InitialState = GetInt(In, TEXT("initial_state"), 0);
	}
	else if (Params.Mode == EHTBlueprintToggleMode::Texture)
	{
		const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
		if (!In->TryGetArrayField(TEXT("groups"), Groups) || Groups->IsEmpty())
		{
			return Fail(Result, TEXT("texture mode needs groups: [{\"source_material\", \"slots\", \"textures\"}]."));
		}
		for (const TSharedPtr<FJsonValue>& Value : *Groups)
		{
			const TSharedPtr<FJsonObject> Group = Value->AsObject();
			FHTTextureMaterialSlotGroup& Out = Params.TextureMaterialSlotGroups.AddDefaulted_GetRef();
			Out.SourceMaterialPath = NormalizeObjectPath(GetString(Group, TEXT("source_material")));
			Out.MaterialElementIndices = GetIntArrayField(Group, TEXT("slots"));
			for (const FString& Path : GetStringArrayField(Group, TEXT("textures")))
			{
				Out.TexturePaths.Add(NormalizeObjectPath(Path));
			}
			for (const int32 Slot : Out.MaterialElementIndices)
			{
				Params.MaterialElementIndices.AddUnique(Slot);
			}
		}
		Params.MaterialElementIndex = Params.MaterialElementIndices.Num() > 0 ? Params.MaterialElementIndices[0] : 0;
		Params.SourceMaterialPath = Params.TextureMaterialSlotGroups[0].SourceMaterialPath;
		Params.TexturePaths = Params.TextureMaterialSlotGroups[0].TexturePaths;
		Params.TextureParameterName = GetString(In, TEXT("parameter")).TrimStartAndEnd();
		if (Params.TextureParameterName.IsEmpty())
		{
			return Fail(Result, TEXT("texture mode needs parameter (for example BaseColor)."));
		}
	}
	else if (Params.Mode == EHTBlueprintToggleMode::MaterialInterface)
	{
		for (const FString& Path : GetStringArrayField(In, TEXT("materials")))
		{
			Params.MaterialInterfacePaths.Add(NormalizeObjectPath(Path));
		}
		if (Params.MaterialInterfacePaths.Num() < 2 || Slots.IsEmpty())
		{
			return Fail(Result, TEXT("material mode needs slots and at least two materials."));
		}
	}
	else
	{
		Params.SourceMaterialPath = NormalizeObjectPath(GetString(In, TEXT("source_material")));
		Params.LightMapTexturePath = NormalizeObjectPath(GetString(In, TEXT("lightmap")));
		Params.IDTexturePath = NormalizeObjectPath(GetString(In, TEXT("id_texture")));
		Params.bTuneLightMap = !Params.LightMapTexturePath.IsEmpty();
		Params.bTuneIDTexture = !Params.IDTexturePath.IsEmpty();
		if (Slots.IsEmpty() || Params.SourceMaterialPath.IsEmpty() || (!Params.bTuneLightMap && !Params.bTuneIDTexture))
		{
			return Fail(Result, TEXT("texture_tuner mode needs slots, source_material and lightmap and/or id_texture."));
		}
	}
	Params.bGenerateInitializeGraph = Params.Mode == EHTBlueprintToggleMode::TextureChannelTuner || GetBool(In, TEXT("generate_initialize"), true);
	Params.bGenerateUpdateGraph = Params.Mode != EHTBlueprintToggleMode::TextureChannelTuner && GetBool(In, TEXT("generate_update"), true);
	Params.bSaveAssets = GetBool(In, TEXT("save"), false);

	const FHTBlueprintToggleGeneratorResult Generated = FHTBlueprintToggleGenerator::Generate(Params);
	for (const FString& Error : Generated.Errors)
	{
		AddError(Result, Error);
	}
	Result->SetArrayField(TEXT("messages"), Strings(Generated.Messages));
	Result->SetStringField(TEXT("slot_name"), Params.SlotName);
	Result->SetBoolField(TEXT("saved"), Generated.bSuccess && Params.bSaveAssets);
	Result->SetBoolField(TEXT("success"), Generated.bSuccess);
	return ToJsonString(Result);
}

FString UHTModScriptLibrary::BuildMaterialInstance(const FString& ParamsJson)
{
	using namespace HTModScript;
	const TSharedRef<FJsonObject> Result = NewResult();
	TSharedPtr<FJsonObject> In;
	if (!ParseObject(ParamsJson, In))
	{
		return Fail(Result, TEXT("Parameters are not a JSON object."));
	}
	FHTMaterialInstanceBuilderParams Params;
	Params.MaterialPath = NormalizeObjectPath(GetString(In, TEXT("material")));
	Params.InstanceName = GetString(In, TEXT("name")).TrimStartAndEnd();
	Params.BaseColorTexturePath = NormalizeObjectPath(GetString(In, TEXT("base_color")));
	Params.IDTexturePath = NormalizeObjectPath(GetString(In, TEXT("id_texture")));
	Params.LightMapTexturePath = NormalizeObjectPath(GetString(In, TEXT("lightmap")));
	Params.NormalTexturePath = NormalizeObjectPath(GetString(In, TEXT("normal")));
	Params.bSaveAssets = GetBool(In, TEXT("save"), false);
	if (Params.MaterialPath.IsEmpty() || Params.InstanceName.IsEmpty())
	{
		return Fail(Result, TEXT("material and name are required."));
	}
	FHTMaterialInstanceBuilderResult Built;
	{
		const FScopedTransaction Transaction(LOCTEXT("BuildMaterialInstance", "Build Material Instance (script)"));
		Built = FHTMaterialInstanceBuilder::Build(Params);
	}
	for (const FString& Error : Built.Errors)
	{
		AddError(Result, Error);
	}
	Result->SetArrayField(TEXT("notes"), Strings(Built.Notes));
	Result->SetStringField(TEXT("instance"), Built.InstancePath);
	Result->SetBoolField(TEXT("saved"), Built.bSuccess && Params.bSaveAssets);
	Result->SetBoolField(TEXT("success"), Built.bSuccess);
	return ToJsonString(Result);
}

#undef LOCTEXT_NAMESPACE
