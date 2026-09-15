#pragma once

#include "CoreMinimal.h"

#include "ConVerseHISMLibrary.h"

class AActor;

namespace ConVerseHISM
{
	struct FBuildOutput
	{
		FConVerseHISMCreationResult Result;
		TArray<UObject*> ObjectsToDelete;
	};

	void CollectActorsFromRoots(const TArray<AActor*>& RootActors, TArray<AActor*>& OutActors);
	FBuildOutput BuildManagedHISMs(const TArray<AActor*>& Actors, const FString& ComponentNamePrefix);
	void FinalizeSummary(FConVerseHISMCreationResult& Result);
}
