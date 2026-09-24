#pragma once

#include "CoreMinimal.h"
#include "EditorReimportHandler.h"

/**
 * Prevents Unreal's ordinary reimport path from replacing assets owned by a
 * committed Optimized Datasmith Import session.
 *
 * FReimportHandler registers itself with FReimportManager for its lifetime.
 * The DatasmithHISM module therefore owns one instance of this class while the
 * module is loaded.
 */
class FConVerseOptimizedReimportHandler final : public FReimportHandler
{
public:
	virtual ~FConVerseOptimizedReimportHandler() override = default;

	//~ Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override;
	//~ End FReimportHandler Interface
};
