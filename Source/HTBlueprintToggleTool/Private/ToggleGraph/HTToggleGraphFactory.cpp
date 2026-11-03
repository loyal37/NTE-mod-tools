#include "HTToggleGraphFactory.h"

#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphEditor.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "HTToggleGraphFactory"

UHTToggleGraphFactory::UHTToggleGraphFactory()
{
	SupportedClass = UHTToggleGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UHTToggleGraphFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UHTToggleGraph* Asset = NewObject<UHTToggleGraph>(InParent, Class, Name, Flags | RF_Transactional);
	Asset->EnsureGraph();
	return Asset;
}

FText UHTToggleGraphFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryName", "HT Toggle Graph");
}

UHTToggleGraph* UHTToggleGraphFactory::FindOrCreateForAnimBlueprint(UAnimBlueprint* AnimBlueprint, const FString& SaveGameBlueprintPath, const FString& CharacterName, FString& OutError)
{
	if (!AnimBlueprint)
	{
		OutError = TEXT("Choose an Animation Blueprint first.");
		return nullptr;
	}
	const FString PackagePath = FPackageName::GetLongPackagePath(AnimBlueprint->GetOutermost()->GetName());
	const FString AssetName = TEXT("TG_") + AnimBlueprint->GetName();
	const FString ObjectPath = PackagePath / AssetName + TEXT(".") + AssetName;
	if (UHTToggleGraph* Existing = LoadObject<UHTToggleGraph>(nullptr, *ObjectPath))
	{
		Existing->EnsureGraph();
		return Existing;
	}

	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UHTToggleGraph* Asset = Cast<UHTToggleGraph>(AssetTools.Get().CreateAsset(AssetName, PackagePath, UHTToggleGraph::StaticClass(), NewObject<UHTToggleGraphFactory>()));
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Could not create %s."), *ObjectPath);
		return nullptr;
	}
	Asset->AnimBlueprint = AnimBlueprint;
	if (!SaveGameBlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		Asset->SaveGameBlueprint = TSoftObjectPtr<UBlueprint>(FSoftObjectPath(SaveGameBlueprintPath.TrimStartAndEnd()));
	}
	Asset->CharacterName = CharacterName.TrimStartAndEnd();
	Asset->TreeName = Asset->CharacterName.IsEmpty() ? AssetName : Asset->CharacterName;
	Asset->EnsureGraph();
	Asset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);
	TArray<UPackage*> Packages = { Asset->GetOutermost() };
	UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
	return Asset;
}

FText UAssetDefinition_HTToggleGraph::GetAssetDisplayName() const
{
	return LOCTEXT("AssetName", "HT Toggle Graph");
}

FLinearColor UAssetDefinition_HTToggleGraph::GetAssetColor() const
{
	return FLinearColor(0.95f, 0.55f, 0.15f);
}

TSoftClassPtr<UObject> UAssetDefinition_HTToggleGraph::GetAssetClass() const
{
	return UHTToggleGraph::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_HTToggleGraph::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(LOCTEXT("Category", "HT")) };
	return Categories;
}

EAssetCommandResult UAssetDefinition_HTToggleGraph::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UHTToggleGraph* Asset : OpenArgs.LoadObjects<UHTToggleGraph>())
	{
		TSharedRef<FHTToggleGraphEditor> Editor = MakeShared<FHTToggleGraphEditor>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
