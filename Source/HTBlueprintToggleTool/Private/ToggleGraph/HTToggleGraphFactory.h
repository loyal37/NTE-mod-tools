#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "Factories/Factory.h"
#include "HTToggleGraphFactory.generated.h"

class UAnimBlueprint;
class UHTToggleGraph;

UCLASS()
class UHTToggleGraphFactory : public UFactory
{
	GENERATED_BODY()

public:
	UHTToggleGraphFactory();

	//~ UFactory
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool CanCreateNew() const override { return true; }
	virtual FText GetDisplayName() const override;

	// Finds the graph asset bound to an Animation Blueprint (TG_<AnimBP> next to it) or creates it.
	static UHTToggleGraph* FindOrCreateForAnimBlueprint(UAnimBlueprint* AnimBlueprint, const FString& SaveGameBlueprintPath, const FString& CharacterName, FString& OutError);
};

UCLASS()
class UAssetDefinition_HTToggleGraph : public UAssetDefinitionDefault
{
	GENERATED_BODY()

protected:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
