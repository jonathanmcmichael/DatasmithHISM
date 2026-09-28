#pragma once

#include "ConVerseImportRecipe.h"

class SWindow;

namespace ConVerseMaterialReview
{
	TSharedRef<SWindow> Open(UConVerseImportRecipe* Recipe, const TArray<FConVerseAppearanceReviewRow>& Appearances, FSimpleDelegate OnChanged);
}
