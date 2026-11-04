#include "HTToggleGraphPartDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraph.h"
#include "Engine/SkeletalMesh.h"
#include "HTToggleGraph.h"
#include "HTToggleGraphNodes.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HTToggleGraphPartDetails"

TSharedRef<IDetailCustomization> FHTToggleGraphPartDetails::MakeInstance()
{
	return MakeShared<FHTToggleGraphPartDetails>();
}

void FHTToggleGraphPartDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}
	Node = Cast<UHTToggleGraphNode_Part>(Objects[0].Get());
	if (!Node.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("MaterialSlots", LOCTEXT("MaterialSlots", "Material Slots"), ECategoryPriority::Important);
	const UHTToggleGraph* Asset = Node->GetToggleGraphAsset();
	USkeletalMesh* Mesh = Asset ? Asset->ResolvePreviewMesh() : nullptr;
	if (!Mesh)
	{
		Category.AddCustomRow(LOCTEXT("NoMeshFilter", "Mesh"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoMesh", "Set the asset's Anim Blueprint (deselect all nodes) to list the preview mesh material slots here."))
			];
		return;
	}

	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
	{
		const FSkeletalMaterial& Material = Materials[SlotIndex];
		const FName SlotName = !Material.MaterialSlotName.IsNone() ? Material.MaterialSlotName : Material.ImportedMaterialSlotName;
		const FString MaterialName = Material.MaterialInterface ? Material.MaterialInterface->GetName() : TEXT("None");
		const FText RowText = FText::FromString(FString::Printf(TEXT("%d  %s  (%s)"), SlotIndex, *SlotName.ToString(), *MaterialName));
		TWeakObjectPtr<UHTToggleGraphNode_Part> WeakNode = Node;
		Category.AddCustomRow(RowText)
			.WholeRowContent()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([WeakNode, SlotIndex]()
				{
					return WeakNode.IsValid() && WeakNode->MaterialIDs.Contains(SlotIndex) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakNode, SlotIndex](ECheckBoxState State)
				{
					UHTToggleGraphNode_Part* PartNode = WeakNode.Get();
					if (!PartNode)
					{
						return;
					}
					const FScopedTransaction Transaction(LOCTEXT("ToggleMaterialSlot", "Toggle Material Slot"));
					PartNode->Modify();
					if (State == ECheckBoxState::Checked)
					{
						PartNode->MaterialIDs.AddUnique(SlotIndex);
						PartNode->MaterialIDs.Sort();
					}
					else
					{
						PartNode->MaterialIDs.Remove(SlotIndex);
					}
					if (UEdGraph* Graph = PartNode->GetGraph())
					{
						Graph->NotifyGraphChanged();
					}
					PartNode->MarkPackageDirty();
				})
				[
					SNew(STextBlock).Text(RowText)
				]
			];
	}
}

#undef LOCTEXT_NAMESPACE
