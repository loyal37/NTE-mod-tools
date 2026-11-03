#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UHTToggleGraphNode_Part;

// Details panel of a part node: lists the preview mesh's material slots as checkboxes so the
// user picks Material IDs by name instead of typing indices.
class FHTToggleGraphPartDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UHTToggleGraphNode_Part> Node;
};
