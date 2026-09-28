#include "ConVerseSourceMetadata.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogConVerseRuntimeValidation, Log, All);

#if !UE_BUILD_SHIPPING
// Explicit acceptance command for cooked Development builds. It never runs during ordinary gameplay.
static FAutoConsoleCommandWithWorldAndArgs ValidateImportedScene(
	TEXT("ConVerse.ValidateImportedScene"),
	TEXT("Validate cooked source lookup and mesh/material references. Arguments: expected source count; optional Exit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		int32 Sources = 0, Components = 0, Lights = 0, IESProfiles = 0, QueryCollision = 0, CollisionHits = 0, Errors = 0;
		if (!World || Args.IsEmpty())
		{
			UE_LOG(LogConVerseRuntimeValidation, Error, TEXT("A world and expected source count are required."));
			return;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<ULightComponent*> ActorLights(*It);
			Lights += ActorLights.Num();
			for (const auto* Light : ActorLights) IESProfiles += Light->IESTexture ? 1 : 0;
			const auto* Data = It->FindComponentByClass<UConVerseSourceMetadata>();
			if (!Data) continue;
			TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
			Components += Meshes.Num();
			for (UStaticMeshComponent* Mesh : Meshes)
			{
				Errors += Mesh->GetStaticMesh() == nullptr ? 1 : 0;
				QueryCollision += Mesh->IsQueryCollisionEnabled() ? 1 : 0;
				if (Mesh->IsQueryCollisionEnabled() && Mesh->GetStaticMesh())
				{
					FTransform Placement = Mesh->GetComponentTransform();
					if (const auto* Instances = Cast<UInstancedStaticMeshComponent>(Mesh); Instances && Instances->GetInstanceCount()) Instances->GetInstanceTransform(0, Placement, true);
					const FBox Box = Mesh->GetStaticMesh()->GetBoundingBox();
					FCollisionQueryParams Query(SCENE_QUERY_STAT(ConVerseCookedValidation), true);
					bool bHit = false;
					for (int32 X = 0; X < 5 && !bHit; ++X)
						for (int32 Y = 0; Y < 5 && !bHit; ++Y)
						{
							const FVector Point(FMath::Lerp(Box.Min.X, Box.Max.X, 0.01 + X * 0.245), FMath::Lerp(Box.Min.Y, Box.Max.Y, 0.01 + Y * 0.245), Box.GetCenter().Z);
							FHitResult Hit;
							bHit = Mesh->LineTraceComponent(Hit, Placement.TransformPosition(Point + FVector(0, 0, Box.GetExtent().Z + 100)),
								Placement.TransformPosition(Point - FVector(0, 0, Box.GetExtent().Z + 100)), Query);
						}
					CollisionHits += bHit ? 1 : 0;
				}

				for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) Errors += Mesh->GetMaterial(Slot) == nullptr ? 1 : 0;
			}
			TInlineComponentArray<UActorComponent*> AllComponents(*It);
			for (const auto& Record : Data->Records)
			{
				++Sources;
				UActorComponent* const* Found = AllComponents.FindByPredicate([&](const UActorComponent* Component) { return Component->GetFName() == Record.ComponentName; });
				FConVerseSourceRecord Lookup;
				if (!Found || !UConVerseSourceMetadata::FindSource(*Found, Record.InstanceIndex, Lookup)
					|| Lookup.SourceElement != Record.SourceElement || Lookup.SourceElement.IsEmpty() || Lookup.SourceDocument.IsEmpty()) ++Errors;
				if (Found)
				{
					const auto* Instances = Cast<UInstancedStaticMeshComponent>(*Found);
					if (Instances ? Record.InstanceIndex < 0 || Record.InstanceIndex >= Instances->GetInstanceCount() : Record.InstanceIndex != INDEX_NONE) ++Errors;
				}
			}
		}
		Errors += Sources == FCString::Atoi(*Args[0]) && Sources > 0 ? 0 : 1;
		if (Args.Contains(TEXT("RequireCollision"))) Errors += QueryCollision > 0 && CollisionHits == QueryCollision ? 0 : 1;
		if (Args.Contains(TEXT("RequireIES"))) Errors += Lights > 0 && IESProfiles > 0 ? 0 : 1;
		UE_LOG(LogConVerseRuntimeValidation, Display, TEXT("%s cooked import: sources=%d meshComponents=%d lights=%d queryCollisionComponents=%d collisionTraceHits=%d IESProfiles=%d errors=%d. Rendered fidelity requires separate acceptance."),
			Errors ? TEXT("FAIL") : TEXT("PASS"), Sources, Components, Lights, QueryCollision, CollisionHits, IESProfiles, Errors);
		if (Args.Contains(TEXT("Exit"))) FPlatformMisc::RequestExitWithStatus(false, Errors ? 1 : 0);
	}));
#endif
