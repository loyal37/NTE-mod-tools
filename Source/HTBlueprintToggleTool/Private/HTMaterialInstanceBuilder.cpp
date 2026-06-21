#include "HTMaterialInstanceBuilder.h"

#include "AssetToolsModule.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace HTMaterialInstanceBuilder
{
	static const FName BaseColorParameter(TEXT("BaseColor"));
	static const FName IDTextureParameter(TEXT("ID_Tex"));
	static const FName LightMapParameter(TEXT("LightMap"));
	static const FName NormalParameter(TEXT("NormalMap"));
	static const FName DiffuseWeightParameter(TEXT("DiffuseColorMapWeight"));
	static const TCHAR* PhongFunctionPath = TEXT("/InterchangeAssets/Functions/MF_PhongToMetalRoughness.MF_PhongToMetalRoughness");

	static FString NormalizeObjectPath(FString Path)
	{
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}
		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			Path += TEXT(".") + AssetName;
		}
		return Path;
	}

	template <typename ObjectType>
	static ObjectType* LoadAsset(const FString& RawPath)
	{
		return LoadObject<ObjectType>(nullptr, *NormalizeObjectPath(RawPath));
	}

	static FString CleanInputName(FString Name)
	{
		int32 TypeStart = INDEX_NONE;
		if (Name.FindChar(TEXT('('), TypeStart))
		{
			Name.LeftInline(TypeStart);
		}
		Name.TrimStartAndEndInline();
		return Name;
	}

	static bool FindInputName(UMaterialExpression* Expression, const FString& WantedName, FString& OutInputName)
	{
		for (const FString& InputName : UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression))
		{
			if (CleanInputName(InputName).Equals(WantedName, ESearchCase::IgnoreCase))
			{
				OutInputName = InputName;
				return true;
			}
		}
		return false;
	}

	static UMaterialExpressionMaterialFunctionCall* CreatePhongFunction(
		UMaterial* Material,
		UMaterialFunctionInterface* MaterialFunction,
		const int32 X,
		const int32 Y,
		TMap<FString, FString>& OutInputNames)
	{
		UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionMaterialFunctionCall::StaticClass(),
				X,
				Y));
		if (!FunctionCall || !FunctionCall->SetMaterialFunction(MaterialFunction))
		{
			return nullptr;
		}

		const TCHAR* RequiredInputs[] = { TEXT("AmbientColor"), TEXT("DiffuseColor"), TEXT("SpecularColor"), TEXT("Shininess") };
		for (const TCHAR* RequiredInput : RequiredInputs)
		{
			FString ActualInputName;
			if (!FindInputName(FunctionCall, RequiredInput, ActualInputName))
			{
				return nullptr;
			}
			OutInputNames.Add(RequiredInput, MoveTemp(ActualInputName));
		}
		return FunctionCall;
	}

	static UMaterialExpressionTextureSampleParameter2D* FindTextureParameter(UMaterial* Material, const FName ParameterName)
	{
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
			if (TextureParameter && TextureParameter->ParameterName == ParameterName)
			{
				return TextureParameter;
			}
		}
		return nullptr;
	}

	static UMaterialExpressionTextureSampleParameter2D* FindOrCreateTextureParameter(
		UMaterial* Material,
		const FName ParameterName,
		UTexture2D* Texture,
		const EMaterialSamplerType SamplerType,
		const int32 X,
		const int32 Y)
	{
		UMaterialExpressionTextureSampleParameter2D* Expression = FindTextureParameter(Material, ParameterName);
		if (!Expression)
		{
			Expression = Cast<UMaterialExpressionTextureSampleParameter2D>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material,
					UMaterialExpressionTextureSampleParameter2D::StaticClass(),
					X,
					Y));
		}
		if (Expression)
		{
			Expression->Modify();
			Expression->ParameterName = ParameterName;
			Expression->Texture = Texture;
			Expression->SamplerType = SamplerType;
			Expression->MaterialExpressionEditorX = X;
			Expression->MaterialExpressionEditorY = Y;
		}
		return Expression;
	}

	static UMaterialExpressionScalarParameter* FindOrCreateDiffuseWeight(UMaterial* Material, const int32 X, const int32 Y)
	{
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression);
			if (Scalar && Scalar->ParameterName == DiffuseWeightParameter)
			{
				Scalar->Modify();
				Scalar->DefaultValue = 1.0f;
				return Scalar;
			}
		}

		UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionScalarParameter::StaticClass(),
				X,
				Y));
		if (Scalar)
		{
			Scalar->ParameterName = DiffuseWeightParameter;
			Scalar->DefaultValue = 1.0f;
		}
		return Scalar;
	}

	static UMaterialExpressionLinearInterpolate* FindOrCreateLerp(
		UMaterial* Material,
		UMaterialExpressionTextureSampleParameter2D* BaseColor,
		const int32 X,
		const int32 Y)
	{
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			UMaterialExpressionLinearInterpolate* Lerp = Cast<UMaterialExpressionLinearInterpolate>(Expression);
			if (Lerp && Lerp->B.Expression == BaseColor)
			{
				return Lerp;
			}
		}
		return Cast<UMaterialExpressionLinearInterpolate>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionLinearInterpolate::StaticClass(),
				X,
				Y));
	}

	static UMaterialExpressionConstant3Vector* FindOrCreateBlackConstant(
		UMaterial* Material,
		UMaterialExpressionLinearInterpolate* Lerp,
		const int32 X,
		const int32 Y)
	{
		if (Lerp)
		{
			if (UMaterialExpressionConstant3Vector* Existing = Cast<UMaterialExpressionConstant3Vector>(Lerp->A.Expression))
			{
				Existing->Modify();
				Existing->Constant = FLinearColor::Black;
				return Existing;
			}
		}

		UMaterialExpressionConstant3Vector* Constant = Cast<UMaterialExpressionConstant3Vector>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionConstant3Vector::StaticClass(),
				X,
				Y));
		if (Constant)
		{
			Constant->Constant = FLinearColor::Black;
		}
		return Constant;
	}

	static UMaterialExpressionConstant* FindOrCreateShininessConstant(
		UMaterial* Material,
		UMaterialExpressionMaterialFunctionCall* FunctionCall,
		const FString& ShininessInputName,
		const int32 X,
		const int32 Y)
	{
		const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(FunctionCall);
		for (int32 InputIndex = 0; InputIndex < InputNames.Num(); ++InputIndex)
		{
			if (InputNames[InputIndex].Equals(ShininessInputName, ESearchCase::CaseSensitive))
			{
				if (FExpressionInput* Input = FunctionCall->GetInput(InputIndex))
				{
					if (UMaterialExpressionConstant* Existing = Cast<UMaterialExpressionConstant>(Input->Expression))
					{
						Existing->Modify();
						Existing->R = 25.0f;
						return Existing;
					}
				}
				break;
			}
		}

		UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionConstant::StaticClass(),
				X,
				Y));
		if (Constant)
		{
			Constant->R = 25.0f;
		}
		return Constant;
	}

	static bool Connect(
		UMaterialExpression* From,
		const FString& OutputName,
		UMaterialExpression* To,
		const FString& InputName,
		FHTMaterialInstanceBuilderResult& Result,
		const FString& Description)
	{
		if (From && To && UMaterialEditingLibrary::ConnectMaterialExpressions(From, OutputName, To, InputName))
		{
			return true;
		}
		Result.Errors.Add(FString::Printf(TEXT("Could not connect %s."), *Description));
		return false;
	}

	static bool ConnectProperty(
		UMaterialExpression* From,
		const FString& OutputName,
		const EMaterialProperty Property,
		FHTMaterialInstanceBuilderResult& Result,
		const FString& Description)
	{
		if (From && UMaterialEditingLibrary::ConnectMaterialProperty(From, OutputName, Property))
		{
			return true;
		}
		Result.Errors.Add(FString::Printf(TEXT("Could not connect %s."), *Description));
		return false;
	}

	static UMaterialInstanceConstant* FindOrCreateInstance(
		UMaterial* Material,
		const FString& RequestedName,
		FHTMaterialInstanceBuilderResult& Result)
	{
		const FString MaterialPackageName = Material->GetOutermost()->GetName();
		const FString PackagePath = FPackageName::GetLongPackagePath(MaterialPackageName);
		const FString DefaultName = Material->GetName() + TEXT("_Inst");
		const FString AssetName = RequestedName.TrimStartAndEnd().IsEmpty() ? DefaultName : RequestedName.TrimStartAndEnd();
		if (AssetName.Contains(TEXT("/")) || AssetName.Contains(TEXT(".")))
		{
			Result.Errors.Add(TEXT("Instance Name must contain only an asset name, not a path."));
			return nullptr;
		}

		const FString InstancePackageName = PackagePath / AssetName;
		const FString InstanceObjectPath = InstancePackageName + TEXT(".") + AssetName;
		if (UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *InstanceObjectPath))
		{
			UMaterialInstanceConstant* ExistingInstance = Cast<UMaterialInstanceConstant>(ExistingObject);
			if (!ExistingInstance)
			{
				Result.Errors.Add(FString::Printf(TEXT("An asset already exists at %s and is not a material instance."), *InstanceObjectPath));
			}
			Result.InstancePath = InstanceObjectPath;
			return ExistingInstance;
		}

		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Material;
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(
			AssetToolsModule.Get().CreateAsset(
				AssetName,
				PackagePath,
				UMaterialInstanceConstant::StaticClass(),
				Factory));
		if (!Instance)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not create material instance: %s"), *InstanceObjectPath));
			return nullptr;
		}
		Result.InstancePath = InstanceObjectPath;
		return Instance;
	}
}

FString FHTMaterialInstanceBuilderResult::ToDisplayString() const
{
	TArray<FString> Lines;
	Lines.Append(Errors);
	Lines.Append(Notes);
	return FString::Join(Lines, TEXT("\n"));
}

FHTMaterialInstanceBuilderResult FHTMaterialInstanceBuilder::Build(const FHTMaterialInstanceBuilderParams& Params)
{
	using namespace HTMaterialInstanceBuilder;

	FHTMaterialInstanceBuilderResult Result;
	UMaterial* Material = LoadAsset<UMaterial>(Params.MaterialPath);
	UTexture2D* BaseColorTexture = LoadAsset<UTexture2D>(Params.BaseColorTexturePath);
	UTexture2D* IDTexture = LoadAsset<UTexture2D>(Params.IDTexturePath);
	UTexture2D* LightMapTexture = LoadAsset<UTexture2D>(Params.LightMapTexturePath);
	UTexture2D* NormalTexture = LoadAsset<UTexture2D>(Params.NormalTexturePath);
	UMaterialFunctionInterface* PhongFunction = LoadAsset<UMaterialFunctionInterface>(PhongFunctionPath);
	if (!Material)
	{
		Result.Errors.Add(TEXT("Choose a valid Material asset."));
	}
	if (!BaseColorTexture)
	{
		Result.Errors.Add(TEXT("Choose a valid BaseColor texture."));
	}
	if (!IDTexture)
	{
		Result.Errors.Add(TEXT("Choose a valid ID texture."));
	}
	if (!LightMapTexture)
	{
		Result.Errors.Add(TEXT("Choose a valid LightMap texture."));
	}
	if (!NormalTexture)
	{
		Result.Errors.Add(TEXT("Choose a valid NormalMap texture."));
	}
	if (!PhongFunction)
	{
		Result.Errors.Add(TEXT("Could not load the engine MF_PhongToMetalRoughness material function."));
	}
	if (!Result.Errors.IsEmpty())
	{
		return Result;
	}

	Material->Modify();
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

	const int32 FunctionX = 200;
	const int32 FunctionY = 0;
	TMap<FString, FString> FunctionInputNames;
	UMaterialExpressionMaterialFunctionCall* FunctionCall = CreatePhongFunction(
		Material,
		PhongFunction,
		FunctionX,
		FunctionY,
		FunctionInputNames);
	if (!FunctionCall)
	{
		Result.Errors.Add(TEXT("Could not create the MF_PhongToMetalRoughness function node."));
		return Result;
	}

	UMaterialExpressionTextureSampleParameter2D* BaseColor = FindOrCreateTextureParameter(Material, BaseColorParameter, BaseColorTexture, SAMPLERTYPE_Color, FunctionX - 900, FunctionY + 130);
	UMaterialExpressionTextureSampleParameter2D* IDTex = FindOrCreateTextureParameter(Material, IDTextureParameter, IDTexture, SAMPLERTYPE_LinearColor, FunctionX - 430, FunctionY + 360);
	UMaterialExpressionTextureSampleParameter2D* LightMap = FindOrCreateTextureParameter(Material, LightMapParameter, LightMapTexture, SAMPLERTYPE_LinearColor, FunctionX - 420, FunctionY - 280);
	UMaterialExpressionTextureSampleParameter2D* NormalMap = FindOrCreateTextureParameter(Material, NormalParameter, NormalTexture, SAMPLERTYPE_Normal, FunctionX + 10, FunctionY + 540);
	UMaterialExpressionLinearInterpolate* Lerp = FindOrCreateLerp(Material, BaseColor, FunctionX - 360, FunctionY + 80);
	UMaterialExpressionConstant3Vector* Black = FindOrCreateBlackConstant(Material, Lerp, FunctionX - 900, FunctionY - 80);
	UMaterialExpressionScalarParameter* DiffuseWeight = FindOrCreateDiffuseWeight(Material, FunctionX - 880, FunctionY + 520);
	UMaterialExpressionConstant* Shininess = FindOrCreateShininessConstant(Material, FunctionCall, FunctionInputNames[TEXT("Shininess")], FunctionX - 150, FunctionY + 640);
	UMaterialExpressionConstant3Vector* EmissiveBlack = Cast<UMaterialExpressionConstant3Vector>(
		UMaterialEditingLibrary::CreateMaterialExpression(
			Material,
			UMaterialExpressionConstant3Vector::StaticClass(),
			FunctionX + 350,
			FunctionY + 360));
	if (EmissiveBlack)
	{
		EmissiveBlack->Constant = FLinearColor::Black;
	}

	if (!BaseColor || !IDTex || !LightMap || !NormalMap || !Lerp || !Black || !DiffuseWeight || !Shininess || !EmissiveBlack)
	{
		Result.Errors.Add(TEXT("One or more material expressions could not be created."));
		return Result;
	}

	Connect(Black, TEXT(""), Lerp, TEXT("A"), Result, TEXT("black constant to Lerp A"));
	Connect(BaseColor, TEXT("RGB"), Lerp, TEXT("B"), Result, TEXT("BaseColor RGB to Lerp B"));
	Connect(DiffuseWeight, TEXT(""), Lerp, TEXT("Alpha"), Result, TEXT("DiffuseColorMapWeight to Lerp Alpha"));
	Connect(LightMap, TEXT("RGB"), FunctionCall, FunctionInputNames[TEXT("AmbientColor")], Result, TEXT("LightMap RGB to AmbientColor"));
	Connect(Lerp, TEXT(""), FunctionCall, FunctionInputNames[TEXT("DiffuseColor")], Result, TEXT("Lerp to DiffuseColor"));
	Connect(IDTex, TEXT("RGB"), FunctionCall, FunctionInputNames[TEXT("SpecularColor")], Result, TEXT("ID_Tex RGB to SpecularColor"));
	Connect(Shininess, TEXT(""), FunctionCall, FunctionInputNames[TEXT("Shininess")], Result, TEXT("25 to Shininess"));
	ConnectProperty(FunctionCall, TEXT("BaseColor"), MP_BaseColor, Result, TEXT("function BaseColor output"));
	ConnectProperty(FunctionCall, TEXT("Metallic"), MP_Metallic, Result, TEXT("function Metallic output"));
	ConnectProperty(FunctionCall, TEXT("Specular"), MP_Specular, Result, TEXT("function Specular output"));
	ConnectProperty(FunctionCall, TEXT("Roughness"), MP_Roughness, Result, TEXT("function Roughness output"));
	ConnectProperty(EmissiveBlack, TEXT(""), MP_EmissiveColor, Result, TEXT("black constant to Emissive Color"));
	ConnectProperty(NormalMap, TEXT("RGB"), MP_Normal, Result, TEXT("NormalMap RGB"));
	if (!Result.Errors.IsEmpty())
	{
		return Result;
	}

	Material->MarkPackageDirty();
	UMaterialEditingLibrary::RecompileMaterial(Material);

	UMaterialInstanceConstant* Instance = FindOrCreateInstance(Material, Params.InstanceName, Result);
	if (!Instance)
	{
		return Result;
	}

	Instance->Modify();
	UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Material);
	const bool bBaseColorSet = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, BaseColorParameter, BaseColorTexture);
	const bool bIDSet = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, IDTextureParameter, IDTexture);
	const bool bLightMapSet = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, LightMapParameter, LightMapTexture);
	const bool bNormalSet = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, NormalParameter, NormalTexture);
	if (!bBaseColorSet || !bIDSet || !bLightMapSet || !bNormalSet)
	{
		Result.Errors.Add(TEXT("The material instance could not override all four texture parameters."));
		return Result;
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->MarkPackageDirty();
	if (Params.bSaveAssets)
	{
		TArray<UPackage*> PackagesToSave = { Material->GetOutermost(), Instance->GetOutermost() };
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
		{
			Result.Errors.Add(TEXT("Material changes were made, but one or more assets could not be saved."));
			return Result;
		}
	}

	Result.bSuccess = true;
	Result.Notes.Add(FString::Printf(TEXT("Cleared and rebuilt material graph: %s"), *Material->GetPathName()));
	Result.Notes.Add(FString::Printf(TEXT("Created or updated material instance: %s"), *Result.InstancePath));
	return Result;
}
