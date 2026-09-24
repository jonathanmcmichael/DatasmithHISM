#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SConVerseDatasmithToolsPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConVerseDatasmithToolsPanel)
	{
	}
		SLATE_EVENT(FSimpleDelegate, OnOpenOptimizedImport)
		SLATE_EVENT(FSimpleDelegate, OnDedupeMeshes)
		SLATE_EVENT(FSimpleDelegate, OnCreateManagedISMs)
		SLATE_EVENT(FSimpleDelegate, OnBatchISMs)
		SLATE_EVENT(FSimpleDelegate, OnAnalyzeISMs)
		SLATE_EVENT(FSimpleDelegate, OnEnableNanite)
		SLATE_EVENT(FSimpleDelegate, OnExplodeISMs)
		SLATE_EVENT(FSimpleDelegate, OnDedupeAndCreateISMs)
		SLATE_EVENT(FSimpleDelegate, OnToggleUseHISM)
		SLATE_ATTRIBUTE(bool, CanRunSelectionTools)
		SLATE_ATTRIBUTE(bool, UseHISMEnabled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply ExecuteAction(const FSimpleDelegate& Action);
	ECheckBoxState GetUseHISMCheckState() const;
	void HandleUseHISMChanged(ECheckBoxState NewState);

	FSimpleDelegate OnOpenOptimizedImport;
	FSimpleDelegate OnDedupeMeshes;
	FSimpleDelegate OnCreateManagedISMs;
	FSimpleDelegate OnBatchISMs;
	FSimpleDelegate OnAnalyzeISMs;
	FSimpleDelegate OnEnableNanite;
	FSimpleDelegate OnExplodeISMs;
	FSimpleDelegate OnDedupeAndCreateISMs;
	FSimpleDelegate OnToggleUseHISM;
	TAttribute<bool> CanRunSelectionTools;
	TAttribute<bool> UseHISMEnabled;
};
