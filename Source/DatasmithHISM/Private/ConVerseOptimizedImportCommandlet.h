#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "ConVerseOptimizedImportCommandlet.generated.h"

/**
 * Headless driver for the optimized Datasmith import, for CI and batch conversion.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project> -run=ConVerseOptimizedImport
 *       -Source="C:/Path/Scene.udatasmith"
 *       [-Destination=/Game/DatasmithOptimized]
 *       [-InstanceType=ISM|HISM]
 *       [-MinInstances=2]
 *       [-AnalyzeOnly]
 *
 * Returns 0 only when the requested operation fully succeeded. Any other outcome, including a
 * rolled-back failure or a blocked reimport, returns non-zero so a build step fails loudly.
 */
UCLASS()
class UConVerseOptimizedImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UConVerseOptimizedImportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
