#include "ConVerseStaticMeshConsolidationWidget.h"

#include "Editor.h"
#include "Engine/Selection.h"

FConVerseStaticMeshConsolidationResult UConVerseStaticMeshConsolidationWidget::ConsolidateSelection()
{
	LastResult = UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshesInSelection(bRequireMatchingMaterials);
	return LastResult;
}

bool UConVerseStaticMeshConsolidationWidget::HasSelection() const
{
	if (GEditor == nullptr)
	{
		return false;
	}

	const USelection* SelectedActors = GEditor->GetSelectedActors();
	const USelection* SelectedObjects = GEditor->GetSelectedObjects();
	return (SelectedActors != nullptr && SelectedActors->Num() > 0)
		|| (SelectedObjects != nullptr && SelectedObjects->Num() > 0);
}
