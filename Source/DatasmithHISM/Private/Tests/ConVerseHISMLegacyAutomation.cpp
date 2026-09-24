#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ConVerseHISMLibrary.h"
#include "ConVerseHISMUtils.h"

#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

/**
 * Coverage for the legacy in-place conversion path in ConVerseHISMUtils.cpp.
 *
 * This path destroys source actors, so these tests focus on the safety properties rather than
 * on grouping breadth: an actor must never be deleted unless its geometry is genuinely
 * represented by an instance, actors carrying behavior must be left alone, and per-component
 * material overrides must survive conversion.
 */
namespace ConVerseHISMLegacyTestHelpers
{
	static UWorld* GetTestWorld()
	{
		return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static UStaticMesh* LoadSharedMesh()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	static UMaterialInterface* LoadOverrideMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	static AStaticMeshActor* SpawnMeshActor(
		UWorld& World,
		UStaticMesh* Mesh,
		const FVector& Location,
		const FString& Label)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transactional;
		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), FTransform(Location), SpawnParameters);
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->SetActorLabel(Label);
		if (UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent())
		{
			MeshComponent->SetMobility(EComponentMobility::Movable);
			MeshComponent->SetStaticMesh(Mesh);
		}
		return Actor;
	}

	/** Destroys everything the test created, including any family actors the build produced. */
	static void Cleanup(UWorld& World, TArray<AActor*>& Actors, const ConVerseHISM::FBuildOutput& Output)
	{
		for (UObject* Object : Output.ObjectsToDelete)
		{
			if (AActor* Actor = Cast<AActor>(Object); IsValid(Actor))
			{
				World.DestroyActor(Actor);
			}
		}
		for (AActor* Actor : Actors)
		{
			if (IsValid(Actor))
			{
				// The build may have reparented geometry under a generated family actor.
				if (AActor* Parent = Actor->GetAttachParentActor(); IsValid(Parent) && !Actors.Contains(Parent))
				{
					World.DestroyActor(Parent);
				}
				World.DestroyActor(Actor);
			}
		}
		Actors.Reset();
	}

	/** Collects every managed component currently in the world. */
	static void CollectManagedComponents(UWorld& World, TSet<UInstancedStaticMeshComponent*>& OutComponents)
	{
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			TInlineComponentArray<UInstancedStaticMeshComponent*> Components;
			It->GetComponents(Components);
			for (UInstancedStaticMeshComponent* Component : Components)
			{
				if (IsValid(Component) && Component->ComponentTags.Contains(FName(TEXT("ConVerseManagedHISM"))))
				{
					OutComponents.Add(Component);
				}
			}
		}
	}

	/**
	 * Finds the managed component this build created, ignoring any that already existed.
	 * Searching the whole world unconditionally would pick up leftovers from earlier tests.
	 */
	static UInstancedStaticMeshComponent* FindNewManagedComponent(
		UWorld& World,
		const TSet<UInstancedStaticMeshComponent*>& PreExisting)
	{
		TSet<UInstancedStaticMeshComponent*> Current;
		CollectManagedComponents(World, Current);
		for (UInstancedStaticMeshComponent* Component : Current)
		{
			if (!PreExisting.Contains(Component))
			{
				return Component;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseHISMBehaviorPayloadTest,
	"DatasmithHISM.LegacyConversion.BehaviorPayloadIsNotDestroyed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConVerseHISMBehaviorPayloadTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseHISMLegacyTestHelpers;

	UWorld* World = GetTestWorld();
	if (!TestNotNull(TEXT("editor world is available"), World))
	{
		return false;
	}

	UStaticMesh* Mesh = LoadSharedMesh();
	if (!TestNotNull(TEXT("shared cube mesh loads"), Mesh))
	{
		return false;
	}

	TArray<AActor*> Created;
	AStaticMeshActor* PlainA = SpawnMeshActor(*World, Mesh, FVector(0.0, 0.0, 0.0), TEXT("Legacy_Plain_A"));
	AStaticMeshActor* PlainB = SpawnMeshActor(*World, Mesh, FVector(200.0, 0.0, 0.0), TEXT("Legacy_Plain_B"));
	AStaticMeshActor* WithAudio = SpawnMeshActor(*World, Mesh, FVector(400.0, 0.0, 0.0), TEXT("Legacy_With_Audio"));
	if (!TestNotNull(TEXT("plain actor A spawned"), PlainA)
		|| !TestNotNull(TEXT("plain actor B spawned"), PlainB)
		|| !TestNotNull(TEXT("payload actor spawned"), WithAudio))
	{
		return false;
	}
	Created.Append({ PlainA, PlainB, WithAudio });

	// Give the third actor behavior beyond its mesh. Converting would delete the actor and
	// silently take this component with it.
	UAudioComponent* AudioComponent = NewObject<UAudioComponent>(WithAudio, TEXT("BehaviorAudio"), RF_Transactional);
	AudioComponent->SetupAttachment(WithAudio->GetRootComponent());
	AudioComponent->RegisterComponent();
	WithAudio->AddInstanceComponent(AudioComponent);

	ConVerseHISM::FBuildOutput Output = ConVerseHISM::BuildManagedHISMs(
		Created, TEXT("ISM"), false, 2, false, EConVerseGroupingMode::MaximumOptimization);

	TestEqual(TEXT("the behavior-bearing actor is reported as skipped"),
		Output.Result.ActorsWithBehaviorPayload, 1);
	TestEqual(TEXT("only the two plain actors convert"),
		Output.Result.SourceActorsConverted, 2);
	TestFalse(TEXT("the behavior-bearing actor is never queued for deletion"),
		Output.ObjectsToDelete.Contains(WithAudio));
	TestTrue(TEXT("the behavior-bearing actor survives with its component intact"),
		IsValid(WithAudio) && IsValid(AudioComponent));

	Cleanup(*World, Created, Output);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseHISMMaterialOverrideTest,
	"DatasmithHISM.LegacyConversion.MaterialOverrideSurvivesConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConVerseHISMMaterialOverrideTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseHISMLegacyTestHelpers;

	UWorld* World = GetTestWorld();
	if (!TestNotNull(TEXT("editor world is available"), World))
	{
		return false;
	}

	UStaticMesh* Mesh = LoadSharedMesh();
	UMaterialInterface* OverrideMaterial = LoadOverrideMaterial();
	if (!TestNotNull(TEXT("shared cube mesh loads"), Mesh)
		|| !TestNotNull(TEXT("override material loads"), OverrideMaterial))
	{
		return false;
	}

	TSet<UInstancedStaticMeshComponent*> PreExistingManaged;
	CollectManagedComponents(*World, PreExistingManaged);

	TArray<AActor*> Created;
	AStaticMeshActor* OverrideA = SpawnMeshActor(*World, Mesh, FVector(0.0, 300.0, 0.0), TEXT("Legacy_Override_A"));
	AStaticMeshActor* OverrideB = SpawnMeshActor(*World, Mesh, FVector(200.0, 300.0, 0.0), TEXT("Legacy_Override_B"));
	if (!TestNotNull(TEXT("override actor A spawned"), OverrideA)
		|| !TestNotNull(TEXT("override actor B spawned"), OverrideB))
	{
		return false;
	}
	Created.Append({ OverrideA, OverrideB });

	// Both actors share a mesh but override slot 0, so they group together and the created
	// component must carry the override rather than falling back to the mesh default.
	OverrideA->GetStaticMeshComponent()->SetMaterial(0, OverrideMaterial);
	OverrideB->GetStaticMeshComponent()->SetMaterial(0, OverrideMaterial);

	ConVerseHISM::FBuildOutput Output = ConVerseHISM::BuildManagedHISMs(
		Created, TEXT("ISM"), false, 2, false, EConVerseGroupingMode::MaximumOptimization);

	TestEqual(TEXT("both overridden actors convert"), Output.Result.SourceActorsConverted, 2);

	UInstancedStaticMeshComponent* Managed = FindNewManagedComponent(*World, PreExistingManaged);
	if (TestNotNull(TEXT("a managed component was created"), Managed))
	{
		TestEqual(TEXT("the managed component holds both instances"),
			Managed->GetInstanceCount(), 2);
		TestEqual(TEXT("the per-component material override survives conversion"),
			Managed->GetMaterial(0), OverrideMaterial);
	}

	Cleanup(*World, Created, Output);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConVerseHISMBelowThresholdTest,
	"DatasmithHISM.LegacyConversion.BelowThresholdLeavesNoEmptyActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConVerseHISMBelowThresholdTest::RunTest(const FString& Parameters)
{
	using namespace ConVerseHISMLegacyTestHelpers;

	UWorld* World = GetTestWorld();
	if (!TestNotNull(TEXT("editor world is available"), World))
	{
		return false;
	}

	UStaticMesh* Mesh = LoadSharedMesh();
	if (!TestNotNull(TEXT("shared cube mesh loads"), Mesh))
	{
		return false;
	}

	int32 ActorCountBefore = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++ActorCountBefore;
	}

	TArray<AActor*> Created;
	AStaticMeshActor* Lonely = SpawnMeshActor(*World, Mesh, FVector(0.0, 600.0, 0.0), TEXT("Legacy_Lonely"));
	if (!TestNotNull(TEXT("single actor spawned"), Lonely))
	{
		return false;
	}
	Created.Add(Lonely);

	// A single actor cannot meet the minimum instance count, so nothing should be converted
	// and no generated family actor should be left behind.
	ConVerseHISM::FBuildOutput Output = ConVerseHISM::BuildManagedHISMs(
		Created, TEXT("ISM"), false, 2, false, EConVerseGroupingMode::MaximumOptimization);

	TestEqual(TEXT("nothing converts below the threshold"), Output.Result.SourceActorsConverted, 0);
	TestEqual(TEXT("the actor is reported as left in place"), Output.Result.ActorsInSingleActorGroups, 1);
	TestTrue(TEXT("nothing is queued for deletion"), Output.ObjectsToDelete.IsEmpty());
	TestTrue(TEXT("the source actor survives"), IsValid(Lonely));

	Cleanup(*World, Created, Output);

	int32 ActorCountAfter = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++ActorCountAfter;
	}
	TestEqual(TEXT("no empty family actor is left behind"), ActorCountAfter, ActorCountBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
