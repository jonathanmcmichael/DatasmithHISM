#include "ConVerseOptimizedImportCommandlet.h"

#include "ConVerseDatasmithImportService.h"
#include "ConVerseNaniteSettings.h"

#include "Misc/Parse.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "DatasmithScene.h"
#include "DatasmithMeshSerialization.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneXmlReader.h"
#include "IDatasmithSceneElements.h"
#include "Hash/xxhash.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogConVerseOptimizedImportCommandlet, Log, All);

UConVerseOptimizedImportCommandlet::UConVerseOptimizedImportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace ConVerseOptimizedImportCommandlet
{
	static const TCHAR* ToString(EConVerseOptimizedImportStatus Status)
	{
		switch (Status)
		{
		case EConVerseOptimizedImportStatus::InvalidOptions: return TEXT("InvalidOptions");
		case EConVerseOptimizedImportStatus::SourceLoadFailed: return TEXT("SourceLoadFailed");
		case EConVerseOptimizedImportStatus::SourceChangedAfterAnalysis: return TEXT("SourceChangedAfterAnalysis");
		case EConVerseOptimizedImportStatus::AnalysisSucceeded: return TEXT("AnalysisSucceeded");
		case EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups: return TEXT("AnalysisNoEligibleGroups");
		case EConVerseOptimizedImportStatus::AlreadyCurrent: return TEXT("AlreadyCurrent");
		case EConVerseOptimizedImportStatus::Verified: return TEXT("Verified");
		case EConVerseOptimizedImportStatus::ImportedWithFailuresRolledBack: return TEXT("ImportedWithFailuresRolledBack");
		case EConVerseOptimizedImportStatus::AwaitingFailedVerificationDecision: return TEXT("AwaitingFailedVerificationDecision");
		case EConVerseOptimizedImportStatus::AcceptedWithFailedVerification: return TEXT("AcceptedWithFailedVerification");
		case EConVerseOptimizedImportStatus::CancelledRolledBack: return TEXT("CancelledRolledBack");
		case EConVerseOptimizedImportStatus::FailedRolledBack: return TEXT("FailedRolledBack");
		case EConVerseOptimizedImportStatus::OptimizedReimportBlocked: return TEXT("OptimizedReimportBlocked");
		case EConVerseOptimizedImportStatus::ManualEditsDetected: return TEXT("ManualEditsDetected");
		case EConVerseOptimizedImportStatus::RollbackFailed: return TEXT("RollbackFailed");
		default: return TEXT("Unknown");
		}
	}

	static const TCHAR* ToString(EConVerseOptimizedSkipReason Reason)
	{
		switch (Reason)
		{
		case EConVerseOptimizedSkipReason::HasChildren: return TEXT("HasChildren");
		case EConVerseOptimizedSkipReason::MissingMeshReference: return TEXT("MissingMeshReference");
		case EConVerseOptimizedSkipReason::UnresolvedMeshReference: return TEXT("UnresolvedMeshReference");
		case EConVerseOptimizedSkipReason::UnresolvedMaterialBinding: return TEXT("UnresolvedMaterialBinding");
		case EConVerseOptimizedSkipReason::NonFiniteTransform: return TEXT("NonFiniteTransform");
		case EConVerseOptimizedSkipReason::UnsupportedNegativeScale: return TEXT("UnsupportedNegativeScale");
		case EConVerseOptimizedSkipReason::BelowMinimumInstanceCount: return TEXT("BelowMinimumInstanceCount");
		case EConVerseOptimizedSkipReason::ExplicitKeepOrdinary: return TEXT("ExplicitKeepOrdinary");
		default: return TEXT("Unknown");
		}
	}

	/**
	 * Only a fully successful outcome is a pass. AlreadyCurrent counts as success for a reimport,
	 * but only when re-verification of the committed output also passed.
	 *
	 * AcceptedWithFailedVerification is deliberately NOT a pass. Accepting a degraded session is an
	 * interactive decision about known-bad output; a headless batch run must still fail so CI does
	 * not go green on an import whose checks did not pass. The commandlet never sets
	 * bDeferRollbackOnVerificationFailure, so it cannot produce that status anyway.
	 */
	static bool IsSuccess(const FConVerseOptimizedImportResult& Result, bool bAnalyzeOnly)
	{
		if (bAnalyzeOnly)
		{
			return Result.Status == EConVerseOptimizedImportStatus::AnalysisSucceeded
				|| Result.Status == EConVerseOptimizedImportStatus::AnalysisNoEligibleGroups;
		}
		if (Result.Status == EConVerseOptimizedImportStatus::Verified)
		{
			return true;
		}
		return Result.Status == EConVerseOptimizedImportStatus::AlreadyCurrent
			&& Result.bVerificationSucceeded;
	}

	/**
	 * Benchmark metrics for one run. Import-side only: draw calls and frame time need a rendered
	 * camera-path run and are deliberately absent, so a passing budget never implies rendered speed.
	 */
	static TSharedRef<FJsonObject> BuildMetrics(const FConVerseOptimizedImportResult& Result, bool bAnalyzeOnly, const FString& Source,
		const FConVerseNaniteApplyResult* Apply = nullptr, const FConVerseNaniteAnalysisResult* Analysis = nullptr)
	{
		TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
		Metrics->SetStringField(TEXT("Source"), Source);
		Metrics->SetStringField(TEXT("Status"), ToString(Result.Status));
		Metrics->SetStringField(TEXT("Operation"), bAnalyzeOnly ? TEXT("Analyze") : TEXT("Import"));
		Metrics->SetNumberField(TEXT("DurationSeconds"), Result.DurationSeconds);
		Metrics->SetNumberField(TEXT("MeshBuildSeconds"), Result.MeshBuildSeconds);
		Metrics->SetNumberField(TEXT("MeshProcessingSeconds"), Result.MeshProcessingSeconds);
		Metrics->SetNumberField(TEXT("PeakPhysicalMB"), double(Result.ProcessPeakPhysicalBytes) / (1024.0 * 1024.0));
		Metrics->SetNumberField(TEXT("SourceMeshActors"), Result.TotalSourceMeshActors);
		Metrics->SetNumberField(TEXT("SourceMeshAssets"), Result.SourceMeshAssetCount);
		Metrics->SetNumberField(TEXT("PlannedGroups"), Result.PlannedGroupCount);
		Metrics->SetNumberField(TEXT("PlannedInstances"), Result.PlannedInstanceCount);
		Metrics->SetNumberField(TEXT("NaniteEnabledMeshes"), Result.NaniteEnabledMeshes);
		Metrics->SetNumberField(TEXT("NaniteRebuiltMeshes"), Result.NaniteRebuiltMeshes);
		Metrics->SetNumberField(TEXT("NaniteBudgetSkippedMeshes"), Result.NaniteBudgetSkippedMeshes);
		Metrics->SetNumberField(TEXT("ProjectedNaniteMeshes"), Result.ProjectedNaniteMeshes);
		Metrics->SetNumberField(TEXT("SourceLights"), Result.SourceLightCount);
		if (Analysis)
		{
			Metrics->SetNumberField(TEXT("AnalysisEligibleMeshes"), Analysis->EligibleMeshes);
			Metrics->SetNumberField(TEXT("AnalysisCandidateMeshes"), Analysis->CandidateMeshes);
			Metrics->SetNumberField(TEXT("AnalysisRecommendedMeshes"), Analysis->RecommendedElements.Num());
			Metrics->SetNumberField(TEXT("AnalysisTotalPlacedTriangles"), double(Analysis->TotalPlacedTriangles));
			Metrics->SetNumberField(TEXT("AnalysisRecommendedPlacedTriangles"), double(Analysis->RecommendedPlacedTriangles));
		}
		if (Apply)
		{
			// The separate Apply Nanite step (Amendment 16); absent when the run did not request it.
			Metrics->SetNumberField(TEXT("ApplyNaniteSeconds"), Apply->DurationSeconds);
			Metrics->SetNumberField(TEXT("ApplyNaniteMeshBuildSeconds"), Apply->MeshBuildSeconds);
			Metrics->SetNumberField(TEXT("ApplyNaniteEnabledMeshes"), Apply->NaniteEnabledMeshes);
			Metrics->SetNumberField(TEXT("ApplyNaniteRebuiltMeshes"), Apply->NaniteRebuiltMeshes);
			Metrics->SetNumberField(TEXT("ApplyNaniteBudgetSkippedMeshes"), Apply->NaniteBudgetSkippedMeshes);
		}

		{
			const FConVerseGroupingBreakdown& Leftovers = Result.GroupingBreakdown;
			TSharedRef<FJsonObject> Breakdown = MakeShared<FJsonObject>();
			Breakdown->SetNumberField(TEXT("EligibleActors"), Result.EligibleSourceActors);
			Breakdown->SetNumberField(TEXT("ParentSplitActors"), Leftovers.ParentSplitActors);
			Breakdown->SetNumberField(TEXT("SettingsSplitActors"), Leftovers.SettingsSplitActors);
			Breakdown->SetNumberField(TEXT("PayloadDuplicateActors"), Leftovers.PayloadDuplicateActors);
			Breakdown->SetNumberField(TEXT("UniqueMeshActors"), Leftovers.UniqueMeshActors);
			Breakdown->SetNumberField(TEXT("NoSingleLeverActors"), Leftovers.NoSingleLeverActors);
			Breakdown->SetNumberField(TEXT("HasChildrenCouldGroup"), Leftovers.HasChildrenCouldGroup);
			Breakdown->SetNumberField(TEXT("NegativeScaleCouldGroup"), Leftovers.NegativeScaleCouldGroup);
			Breakdown->SetNumberField(TEXT("MeshesWithPayloadHash"), Leftovers.MeshesWithPayloadHash);
			TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
			for (const TPair<FString, int32>& Field : Leftovers.SettingsSplitFields) Fields->SetNumberField(Field.Key, Field.Value);
			Breakdown->SetObjectField(TEXT("SettingsSplitFields"), Fields);
			TSharedRef<FJsonObject> Skips = MakeShared<FJsonObject>();
			for (const FConVerseOptimizedSkipCount& Count : Result.SkipCounts) Skips->SetNumberField(ToString(Count.Reason), Count.Count);
			Breakdown->SetObjectField(TEXT("SkipCounts"), Skips);
			Breakdown->SetNumberField(TEXT("SceneSpreadCm"), Leftovers.SceneSpreadCm);
			Breakdown->SetNumberField(TEXT("MedianGroupSpreadCm"), Leftovers.MedianGroupSpreadCm);
			Breakdown->SetNumberField(TEXT("SceneSpanningGroups"), Leftovers.SceneSpanningGroups);
			TArray<TSharedPtr<FJsonValue>> Widest;
			for (const FConVerseGroupingBreakdown::FGroupSpread& Group : Leftovers.WidestGroups)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("GroupId"), Group.GroupId);
				Row->SetStringField(TEXT("MeshElement"), Group.MeshElement);
				Row->SetNumberField(TEXT("Instances"), Group.Instances);
				Row->SetNumberField(TEXT("SpreadCm"), Group.SpreadCm);
				Widest.Add(MakeShared<FJsonValueObject>(Row));
			}
			Breakdown->SetArrayField(TEXT("WidestGroups"), Widest);
			Metrics->SetObjectField(TEXT("GroupingBreakdown"), Breakdown);
		}

		TSharedRef<FJsonObject> Stages = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Stage : Result.StageSeconds)
		{
			Stages->SetNumberField(Stage.Key, Stage.Value);
		}
		Metrics->SetObjectField(TEXT("StageSeconds"), Stages);

		if (!bAnalyzeOnly)
		{
			// Scene cost of the result, after the import: what the renderer and the editor must carry.
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			int32 Actors = 0, Components = 0, IsmComponents = 0;
			int64 Instances = 0;
			if (World)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					++Actors;
					TInlineComponentArray<UPrimitiveComponent*> Primitives;
					It->GetComponents(Primitives);
					Components += Primitives.Num();
					for (UPrimitiveComponent* Primitive : Primitives)
					{
						if (const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(Primitive))
						{
							++IsmComponents;
							Instances += Ism->GetInstanceCount();
						}
					}
				}
			}
			Metrics->SetNumberField(TEXT("ActorCount"), Actors);
			Metrics->SetNumberField(TEXT("PrimitiveComponentCount"), Components);
			Metrics->SetNumberField(TEXT("InstancedComponentCount"), IsmComponents);
			Metrics->SetNumberField(TEXT("InstanceCount"), double(Instances));
		}
		return Metrics;
	}

	/**
	 * Compares metrics to a budget file whose keys are "Max<Metric>". An unknown or non-numeric
	 * budget key fails the run: a typo must not become a silently unchecked budget.
	 */
	static bool CheckBudget(const FJsonObject& Metrics, const FString& BudgetPath)
	{
		FString Text;
		TSharedPtr<FJsonObject> Budget;
		if (!FFileHelper::LoadFileToString(Text, *BudgetPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Budget) || !Budget.IsValid())
		{
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Budget file '%s' could not be read as JSON."), *BudgetPath);
			return false;
		}
		bool bWithinBudget = true;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Budget->Values)
		{
			double Limit = 0.0, Actual = 0.0;
			const FString Metric = Entry.Key.StartsWith(TEXT("Max")) ? Entry.Key.RightChop(3) : FString();
			if (Metric.IsEmpty() || !Entry.Value->TryGetNumber(Limit) || !Metrics.TryGetNumberField(Metric, Actual))
			{
				UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
					TEXT("Budget key '%s' is not Max<NumericMetric> for a metric recorded by this run."), *Entry.Key);
				bWithinBudget = false;
				continue;
			}
			if (Actual > Limit)
			{
				UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
					TEXT("Budget exceeded: %s = %.2f, limit %.2f."), *Metric, Actual, Limit);
				bWithinBudget = false;
			}
		}
		return bWithinBudget;
	}

	// Read-only grouping-plan survey: counts mesh elements whose payloads match once translation is
	// removed (Revit bakes many copies' positions into their geometry). Writes only OutputFile.
	static int32 RunGeometrySurvey(const FString& SourceFile, const FString& OutputFile, int32 MinInstances)
	{
		TSharedRef<IDatasmithScene> Scene = FDatasmithSceneFactory::CreateScene(TEXT("GeometrySurvey"));
		FDatasmithSceneXmlReader Reader;
		if (!Reader.ParseFile(SourceFile, Scene))
		{
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not parse '%s'."), *SourceFile);
			return 1;
		}

		TMap<FString, int32> Placements;
		int32 MeshActors = 0;
		TFunction<void(const TSharedPtr<IDatasmithActorElement>&)> Visit = [&](const TSharedPtr<IDatasmithActorElement>& Actor)
		{
			if (!Actor.IsValid()) return;
			if (Actor->IsA(EDatasmithElementType::StaticMeshActor))
			{
				++MeshActors;
				++Placements.FindOrAdd(StaticCastSharedPtr<IDatasmithMeshActorElement>(Actor)->GetStaticMeshPathName());
			}
			for (int32 Child = 0; Child < Actor->GetChildrenCount(); ++Child) Visit(Actor->GetChild(Child));
		};
		for (int32 Index = 0; Index < Scene->GetActorsCount(); ++Index) Visit(Scene->GetActor(Index));

		// Exact: payload as stored. Shape: sorted vertex positions relative to the centroid.
		// ShapeAndUV: ordered centroid-relative positions, triangle corners, normals and UV0. All include material slots.
		enum EKind { Exact, Shape, ShapeAndUV, KindCount };
		const TCHAR* KindNames[KindCount] = { TEXT("Exact"), TEXT("Shape"), TEXT("ShapeAndUV") };
		struct FClass { int32 Meshes = 0; int32 Actors = 0; FString Label; };
		TMap<uint64, FClass> Classes[KindCount];
		TMap<FString, uint64> MeshSignature[KindCount];
		int32 Unreadable = 0;

		for (int32 Index = 0; Index < Scene->GetMeshesCount(); ++Index)
		{
			const TSharedPtr<IDatasmithMeshElement> Mesh = Scene->GetMesh(Index);
			const int32* Uses = Mesh.IsValid() ? Placements.Find(Mesh->GetName()) : nullptr;
			if (!Uses) continue;
			FString File = Mesh->GetFile();
			if (FPaths::IsRelative(File)) File = FPaths::GetPath(SourceFile) / File;
			const FDatasmithPackedMeshes Packed = GetDatasmithMeshFromFile(File);
			const FDatasmithMeshModels* Model = Packed.Meshes.FindByPredicate(
				[](const FDatasmithMeshModels& Candidate) { return !Candidate.bIsCollisionMesh && !Candidate.SourceModels.IsEmpty(); });
			if (!Model) { ++Unreadable; continue; }
			const FMeshDescription& Description = Model->SourceModels[0];
			const FStaticMeshConstAttributes Attributes(Description);
			const auto Positions = Attributes.GetVertexPositions();
			const auto UVs = Attributes.GetVertexInstanceUVs();
			const auto Normals = Attributes.GetVertexInstanceNormals();

			TArray<FString> Slots;
			for (int32 Slot = 0; Slot < Mesh->GetMaterialSlotCount(); ++Slot)
			{
				const TSharedPtr<const IDatasmithMaterialIDElement> Material = Mesh->GetMaterialSlotAt(Slot);
				if (Material.IsValid()) Slots.Add(FString::Printf(TEXT("%d:%s"), Material->GetId(), Material->GetName()));
			}
			Slots.Sort();
			const FString SlotText = FString::Join(Slots, TEXT(";"));

			FVector3d Centroid = FVector3d::ZeroVector;
			for (FVertexID Vertex : Description.Vertices().GetElementIDs()) Centroid += FVector3d(Positions[Vertex]);
			Centroid /= FMath::Max(1, Description.Vertices().Num());

			// 1 mm position and 1e-3 UV/normal quanta: coarse enough to absorb float noise at kilometre offsets.
			auto Quantize = [](double Value, double Scale) { return FMath::RoundToInt64(Value * Scale); };
			TArray<int64> Streams[KindCount];
			TArray<FIntVector3> Sorted;
			TMap<FVertexID, int32> Ordinal;
			for (FVertexID Vertex : Description.Vertices().GetElementIDs())
			{
				const FVector3d P(Positions[Vertex]);
				const FVector3d C = P - Centroid;
				Ordinal.Add(Vertex, Ordinal.Num());
				Streams[Exact].Append({ Quantize(P.X, 10.0), Quantize(P.Y, 10.0), Quantize(P.Z, 10.0) });
				Streams[ShapeAndUV].Append({ Quantize(C.X, 10.0), Quantize(C.Y, 10.0), Quantize(C.Z, 10.0) });
				Sorted.Add(FIntVector3(int32(Quantize(C.X, 10.0)), int32(Quantize(C.Y, 10.0)), int32(Quantize(C.Z, 10.0))));
			}
			Sorted.Sort([](const FIntVector3& A, const FIntVector3& B)
			{
				return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z;
			});
			for (const FIntVector3& P : Sorted) Streams[Shape].Append({ P.X, P.Y, P.Z });
			Streams[Shape].Add(Description.Triangles().Num());
			for (FTriangleID Triangle : Description.Triangles().GetElementIDs())
			{
				const auto Corners = Description.GetTriangleVertexInstances(Triangle);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const int32 VertexOrdinal = Ordinal[Description.GetVertexInstanceVertex(Corners[Corner])];
					const FVector2f UV = UVs.GetNumChannels() > 0 ? UVs.Get(Corners[Corner], 0) : FVector2f::ZeroVector;
					const FVector3f N = Normals[Corners[Corner]];
					for (EKind Kind : { Exact, ShapeAndUV })
					{
						Streams[Kind].Append({ VertexOrdinal, Quantize(UV.X, 1000.0), Quantize(UV.Y, 1000.0),
							Quantize(N.X, 1000.0), Quantize(N.Y, 1000.0), Quantize(N.Z, 1000.0) });
					}
				}
			}
			for (int32 Kind = 0; Kind < KindCount; ++Kind)
			{
				const uint64 Signature = FXxHash64::HashBuffer(Streams[Kind].GetData(), Streams[Kind].Num() * sizeof(int64)).Hash
					^ GetTypeHash(SlotText);
				MeshSignature[Kind].Add(Mesh->GetName(), Signature);
				FClass& Class = Classes[Kind].FindOrAdd(Signature);
				++Class.Meshes;
				Class.Actors += *Uses;
				if (Class.Label.IsEmpty()) Class.Label = Mesh->GetLabel();
			}
		}

		int32 Baseline = 0, PlacedOnce = 0;
		for (const TPair<FString, int32>& Use : Placements)
		{
			Baseline += Use.Value >= MinInstances ? Use.Value : 0;
			PlacedOnce += Use.Value == 1 ? 1 : 0;
		}
		const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("Source"), SourceFile);
		Report->SetNumberField(TEXT("MinInstances"), MinInstances);
		Report->SetNumberField(TEXT("MeshActors"), MeshActors);
		Report->SetNumberField(TEXT("PlacedMeshElements"), Placements.Num());
		Report->SetNumberField(TEXT("PlacedOnceMeshElements"), PlacedOnce);
		Report->SetNumberField(TEXT("UnreadablePayloads"), Unreadable);
		Report->SetNumberField(TEXT("ActorsGroupableByMeshElement"), Baseline);
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display,
			TEXT("Geometry survey: %d mesh actors, %d placed mesh elements (%d placed once), %d unreadable; %d actors groupable by mesh element."),
			MeshActors, Placements.Num(), PlacedOnce, Unreadable, Baseline);
		for (int32 Kind = 0; Kind < KindCount; ++Kind)
		{
			int32 Groupable = 0, PlacedOnceRescued = 0;
			for (const TPair<FString, uint64>& Entry : MeshSignature[Kind])
			{
				if (Placements[Entry.Key] == 1 && Classes[Kind][Entry.Value].Actors >= MinInstances) ++PlacedOnceRescued;
			}
			TArray<FClass> Ranked;
			for (const TPair<uint64, FClass>& Entry : Classes[Kind])
			{
				Groupable += Entry.Value.Actors >= MinInstances ? Entry.Value.Actors : 0;
				Ranked.Add(Entry.Value);
			}
			Ranked.Sort([](const FClass& A, const FClass& B) { return A.Actors > B.Actors; });
			const TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
			Section->SetNumberField(TEXT("DistinctSignatures"), Classes[Kind].Num());
			Section->SetNumberField(TEXT("ActorsGroupable"), Groupable);
			Section->SetNumberField(TEXT("PlacedOnceMeshesRescued"), PlacedOnceRescued);
			TArray<TSharedPtr<FJsonValue>> Top;
			for (int32 Rank = 0; Rank < FMath::Min(10, Ranked.Num()); ++Rank)
			{
				const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("Label"), Ranked[Rank].Label);
				Row->SetNumberField(TEXT("Meshes"), Ranked[Rank].Meshes);
				Row->SetNumberField(TEXT("Actors"), Ranked[Rank].Actors);
				Top.Add(MakeShared<FJsonValueObject>(Row));
			}
			Section->SetArrayField(TEXT("Largest"), Top);
			Report->SetObjectField(KindNames[Kind], Section);
			UE_LOG(LogConVerseOptimizedImportCommandlet, Display,
				TEXT("  %s: %d distinct signatures, %d actors groupable, %d placed-once meshes rescued."),
				KindNames[Kind], Classes[Kind].Num(), Groupable, PlacedOnceRescued);
		}

		FString Json;
		FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		return FFileHelper::SaveStringToFile(Json, *OutputFile) ? 0 : 1;
	}
}

int32 UConVerseOptimizedImportCommandlet::Main(const FString& Params)
{
	using namespace ConVerseOptimizedImportCommandlet;

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Arguments;
	ParseCommandLine(*Params, Tokens, Switches, Arguments);

	if (const FString* SurveySource = Arguments.Find(TEXT("GeometrySurvey")))
	{
		const FString* SurveyFile = Arguments.Find(TEXT("SurveyFile"));
		const FString* MinText = Arguments.Find(TEXT("MinInstances"));
		if (!SurveyFile) return 1;
		return RunGeometrySurvey(*SurveySource, *SurveyFile, MinText ? FMath::Max(2, FCString::Atoi(**MinText)) : 2);
	}

	if (const FString* MeshFile = Arguments.Find(TEXT("InspectMesh")))
	{
		const FString* Directory = Arguments.Find(TEXT("GeometryDirectory"));
		if (!Directory) return 1;
		const FDatasmithPackedMeshes Packed = GetDatasmithMeshFromFile(*MeshFile);
		if (Packed.Meshes.IsEmpty()) return 1;
		IFileManager::Get().MakeDirectory(**Directory, true);
		for (int32 Model = 0; Model < Packed.Meshes.Num(); ++Model)
		{
			for (int32 LOD = 0; LOD < Packed.Meshes[Model].SourceModels.Num(); ++LOD)
			{
				const FMeshDescription& Description = Packed.Meshes[Model].SourceModels[LOD];
				const auto Positions = FStaticMeshConstAttributes(Description).GetVertexPositions();
				TMap<FVertexID, int32> Indices;
				FString Obj;
				int32 Index = 1;
				for (FVertexID Vertex : Description.Vertices().GetElementIDs())
				{
					Indices.Add(Vertex, Index++);
					const FVector3f P = Positions[Vertex];
					Obj += FString::Printf(TEXT("v %.9g %.9g %.9g\n"), P.X, P.Y, P.Z);
				}
				for (FTriangleID Triangle : Description.Triangles().GetElementIDs())
				{
					const auto Vertices = Description.GetTriangleVertices(Triangle);
					Obj += FString::Printf(TEXT("f %d %d %d\n"), Indices[Vertices[0]], Indices[Vertices[1]], Indices[Vertices[2]]);
				}
				UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("Exported payload model=%d LOD=%d vertices=%d triangles=%d collision=%d"),
					Model, LOD, Description.Vertices().Num(), Description.Triangles().Num(), Packed.Meshes[Model].bIsCollisionMesh);
				if (!FFileHelper::SaveStringToFile(Obj, *(*Directory / FString::Printf(TEXT("Payload_%d_LOD%d.obj"), Model, LOD)))) return 1;
			}
		}
		return 0;
	}

	const FString* Source = Arguments.Find(TEXT("Source"));
	if (Source == nullptr || Source->IsEmpty())
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
			TEXT("Missing -Source=<path>. Usage: -run=ConVerseOptimizedImport -Source=\"C:/Path/Scene.udatasmith\" ")
			TEXT("[-Destination=/Game/DatasmithOptimized] [-InstanceType=ISM|HISM] [-MinInstances=2] [-AnalyzeOnly]")
			TEXT(" [-ChordTolerance=0.2] [-MaxEdgeLength=0] [-NormalTolerance=20] [-Stitching=None|Sewing|Healing] [-AllowMissingTextures] [-TextureSearchFolders=\"C:/A;C:/B\"]"));
		return 1;
	}

	if (Arguments.Contains(TEXT("Budget")) && !Arguments.Contains(TEXT("MetricsFile")))
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("-Budget requires -MetricsFile."));
		return 1;
	}

	FConVerseOptimizedImportOptions Options;
	if (const FString* Preset = Arguments.Find(TEXT("Preset")))
	{
		FString Error;
		if (!FConVerseDatasmithImportService::LoadPreset(*Preset, Options, Error)) { UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("%s"), *Error); return 1; }
	}
	Options.FilePath = *Source;
	Options.bRebuildFromSource = FParse::Param(*Params, TEXT("Rebuild"));
	Options.bReplaceManualEdits = FParse::Param(*Params, TEXT("ReplaceManualEdits"));
	// Headless equivalent of answering Yes to the panel's missing-texture prompt. Missing mesh files still fail.
	Options.bAllowMissingTextures = FParse::Param(*Params, TEXT("AllowMissingTextures"));
	// Semicolon-separated; an empty value disables the default Autodesk library search.
	if (const FString* Folders = Arguments.Find(TEXT("TextureSearchFolders")))
	{
		Options.TextureSearchFolders.Reset();
		Folders->ParseIntoArray(Options.TextureSearchFolders, TEXT(";"), true);
	}
	if (const FString* Policy = Arguments.Find(TEXT("Nanite")))
	{
		if (*Policy == TEXT("All")) Options.Processing.NanitePolicy = EConVerseNanitePolicy::AllSupportedMeshes;
		else if (*Policy == TEXT("ISM")) Options.Processing.NanitePolicy = EConVerseNanitePolicy::ConvertedISMOnly;
		else if (*Policy == TEXT("Preserve")) Options.Processing.NanitePolicy = EConVerseNanitePolicy::PreserveImported;
		else { UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Nanite must be All, ISM (covers converted ISM and HISM group output), or Preserve.")); return 1; }
	}
	// 0 = unlimited. Omitted keeps the recipe/preset value (default 16384).
	if (const FString* NaniteBudget = Arguments.Find(TEXT("NaniteBudget"))) Options.Processing.MaxNaniteMeshes = FMath::Max(0, FCString::Atoi(**NaniteBudget));
	if (const FString* Threshold = Arguments.Find(TEXT("ManyLightThreshold"))) Options.Processing.ManyLightThreshold = FCString::Atoi(**Threshold);
	if (const FString* Names = Arguments.Find(TEXT("KeepOrdinary"))) Names->ParseIntoArray(Options.Processing.KeepOrdinaryMeshElements, TEXT(","), true);
	if (const FString* Names = Arguments.Find(TEXT("DisableNanite"))) Names->ParseIntoArray(Options.Processing.DisableNaniteMeshElements, TEXT(","), true);
	// Never prompt in a headless run. This is the documented automation seam.
	Options.bAutomated = true;

	if (const FString* Destination = Arguments.Find(TEXT("Destination")))
	{
		Options.DestinationPath = *Destination;
	}
	if (const FString* InstanceType = Arguments.Find(TEXT("InstanceType")))
	{
		if (InstanceType->Equals(TEXT("HISM"), ESearchCase::IgnoreCase))
		{
			Options.InstanceType = EConVerseOptimizedInstanceType::HISM;
		}
		else if (InstanceType->Equals(TEXT("ISM"), ESearchCase::IgnoreCase))
		{
			Options.InstanceType = EConVerseOptimizedInstanceType::ISM;
		}
		else
		{
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
				TEXT("Unrecognized -InstanceType=%s. Expected ISM or HISM."), **InstanceType);
			return 1;
		}
	}
	if (const FString* MinInstances = Arguments.Find(TEXT("MinInstances")))
	{
		Options.MinimumInstanceCount = FCString::Atoi(**MinInstances);
	}

	// Tessellation switches. Values are clamped by the service, so CI cannot drive the
	// translator outside the engine's supported ranges.
	if (const FString* ChordTolerance = Arguments.Find(TEXT("ChordTolerance")))
	{
		Options.Tessellation.ChordTolerance = FCString::Atof(**ChordTolerance);
	}
	if (const FString* MaxEdgeLength = Arguments.Find(TEXT("MaxEdgeLength")))
	{
		Options.Tessellation.MaxEdgeLength = FCString::Atof(**MaxEdgeLength);
	}
	if (const FString* NormalTolerance = Arguments.Find(TEXT("NormalTolerance")))
	{
		Options.Tessellation.NormalTolerance = FCString::Atof(**NormalTolerance);
	}
	if (const FString* Stitching = Arguments.Find(TEXT("Stitching")))
	{
		if (Stitching->Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Options.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::None;
		}
		else if (Stitching->Equals(TEXT("Sewing"), ESearchCase::IgnoreCase))
		{
			Options.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::Sewing;
		}
		else if (Stitching->Equals(TEXT("Healing"), ESearchCase::IgnoreCase))
		{
			Options.Tessellation.StitchingTechnique = EConVerseStitchingTechnique::Healing;
		}
		else
		{
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
				TEXT("Unrecognized -Stitching=%s. Expected None, Sewing, or Healing."), **Stitching);
			return 1;
		}
	}

	if (const FString* Map = Arguments.Find(TEXT("NewMap")))
	{
		if (!Map->StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(*Map) || FPackageName::DoesPackageExist(*Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("NewMap must be a new /Game map path; existing maps are never overwritten.")); return 1; }
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (!World || !UEditorLoadingAndSavingUtils::SaveMap(World, *Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not create validation map.")); return 1; }
	}
	if (const FString* Map = Arguments.Find(TEXT("LoadMap")))
	{
		if (!UEditorLoadingAndSavingUtils::LoadMap(*Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not load map.")); return 1; }
	}

	const bool bAnalyzeOnly = Switches.ContainsByPredicate(
		[](const FString& Switch) { return Switch.Equals(TEXT("AnalyzeOnly"), ESearchCase::IgnoreCase); });
	if (const FString* Map = Arguments.Find(TEXT("SaveAsMap")); Map && !bAnalyzeOnly)
	{
		if (!Map->StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(*Map) || FPackageName::DoesPackageExist(*Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("SaveAsMap requires a new writable /Game map path.")); return 1; }
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World && FPackageName::DoesPackageExist(World->GetOutermost()->GetName()))
		{
			// UE duplicates an already-saved world, including a new actor identity domain, while
			// leaving this world loaded. Saving its manifest would falsely bless the new copy.
			UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
				TEXT("Named-level Save As is blocked before import or copying: ownership cannot be proven for a duplicated map. ")
				TEXT("Continue with -LoadMap=%s -Save. For an independent copy, import into a new map and a new destination."),
				*World->GetOutermost()->GetName());
			return 1;
		}
	}

	UE_LOG(LogConVerseOptimizedImportCommandlet, Display,
		TEXT("ConVerse optimized %s: source '%s', destination '%s', %s, minimum %d instances."),
		bAnalyzeOnly ? TEXT("analysis") : TEXT("import"),
		*Options.FilePath,
		*Options.DestinationPath,
		Options.InstanceType == EConVerseOptimizedInstanceType::HISM ? TEXT("HISM") : TEXT("ISM"),
		Options.MinimumInstanceCount);

	const FConVerseOptimizedImportResult Result = bAnalyzeOnly
		? FConVerseDatasmithImportService::Analyze(Options)
		: FConVerseDatasmithImportService::ImportAndVerify(Options);

	const bool bSucceeded = IsSuccess(Result, bAnalyzeOnly);
	UE_LOG(LogConVerseOptimizedImportCommandlet, Display,
		TEXT("Status=%s PlanId=%s Groups=%d/%d Instances=%d/%d. %s"),
		ToString(Result.Status),
		*Result.PlanId,
		Result.VerifiedGroupCount, Result.PlannedGroupCount,
		Result.VerifiedInstanceCount, Result.PlannedInstanceCount,
		*Result.Summary);

	if (!Result.SavedReportPath.IsEmpty())
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("Report: %s"), *Result.SavedReportPath);
	}

	if (Result.bSidecarChanged)
	{
		// Logged as a warning, not an error: the run itself succeeded and nothing was mutated.
		// Escalating to a non-zero exit would break CI on a condition the user chose to be advisory.
		UE_LOG(LogConVerseOptimizedImportCommandlet, Warning,
			TEXT("Sidecar assets changed while the primary source file did not (%d files now, %d recorded). ")
			TEXT("The committed geometry may be stale. Force a reimport to pick up the new assets."),
			Result.SidecarFileCount,
			Result.SidecarFileCountAtCommit);
	}

	// Amendment 17: optional read-only Nanite analysis, run before any apply so it describes the import as
	// made. -AnalyzeNanite logs it, -NaniteAnalysisFile=<csv> also writes every row, and
	// -ApplyNanite=Recommended applies exactly its recommendation. -NaniteCoverage=<percent> and
	// -NaniteMinTriangles=<n> set its heuristic thresholds (defaults 95 and 1000).
	const FString* ApplyPolicy = Arguments.Find(TEXT("ApplyNanite"));
	const FString* AnalysisFile = Arguments.Find(TEXT("NaniteAnalysisFile"));
	const bool bApplyRecommended = ApplyPolicy && *ApplyPolicy == TEXT("Recommended");
	FConVerseNaniteAnalysisResult Analysis;
	bool bAnalysisRan = false;
	if ((FParse::Param(*Params, TEXT("AnalyzeNanite")) || AnalysisFile || bApplyRecommended) && !bAnalyzeOnly && bSucceeded)
	{
		FConVerseNaniteAnalysisOptions Request;
		Request.ManifestPath = Result.ManifestAssetPath;
		Request.DisableNaniteMeshElements = Options.Processing.DisableNaniteMeshElements;
		if (const FString* Coverage = Arguments.Find(TEXT("NaniteCoverage"))) Request.CoverageTarget = FMath::Clamp(FCString::Atod(**Coverage), 1.0, 100.0) / 100.0;
		if (const FString* MinTriangles = Arguments.Find(TEXT("NaniteMinTriangles"))) Request.MinTriangles = FMath::Max(0, FCString::Atoi(**MinTriangles));
		Analysis = FConVerseDatasmithImportService::AnalyzeNanite(Request);
		bAnalysisRan = Analysis.bSucceeded;
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("%s"), *Analysis.Summary);
		if (AnalysisFile && Analysis.bSucceeded)
		{
			FString Csv = TEXT("MeshElement,Triangles,Placements,PlacedTriangles,Eligible,Recommended,CumulativeShare,NaniteEnabled,Note\n");
			for (const FConVerseNaniteAnalysisRow& Row : Analysis.Rows)
				Csv += FString::Printf(TEXT("%s,%d,%d,%lld,%d,%d,%.6f,%d,%s\n"), *Row.MeshElement, Row.Triangles, Row.Placements,
					Row.PlacedTriangles, Row.bEligible, Row.bRecommended, Row.CumulativeShare, Row.bNaniteEnabled, *Row.Note);
			if (FFileHelper::SaveStringToFile(Csv, **AnalysisFile))
			{
				UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("Nanite analysis: %s"), **AnalysisFile);
			}
			else
			{
				UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not write the Nanite analysis file '%s'."), **AnalysisFile);
			}
		}
	}

	// Amendment 16: Nanite is a separate step on the committed import. Run it here so the benchmark
	// metrics can report it next to the import it follows.
	FConVerseNaniteApplyResult ApplyResult;
	bool bApplyRan = false;
	bool bApplyOk = true;
	if (ApplyPolicy)
	{
		FConVerseNaniteApplyOptions Apply;
		if (*ApplyPolicy == TEXT("All")) Apply.Policy = EConVerseNanitePolicy::AllSupportedMeshes;
		else if (*ApplyPolicy == TEXT("ISM")) Apply.Policy = EConVerseNanitePolicy::ConvertedISMOnly;
		else if (bApplyRecommended) Apply.Policy = EConVerseNanitePolicy::AllSupportedMeshes;
		else { UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("ApplyNanite must be All, ISM (converted ISM and HISM group output) or Recommended.")); return 1; }
		if (bAnalyzeOnly)
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("-ApplyNanite needs an import; it cannot follow -AnalyzeOnly.")); return 1; }
		if (bSucceeded)
		{
			Apply.ManifestPath = Result.ManifestAssetPath;
			Apply.bAutomated = true;
			Apply.MaxNaniteMeshes = Options.Processing.MaxNaniteMeshes;
			Apply.DisableNaniteMeshElements = Options.Processing.DisableNaniteMeshElements;
			if (bApplyRecommended)
			{
				// An empty selection means "every mesh" to the service, so refuse it here.
				if (!bAnalysisRan || Analysis.RecommendedElements.IsEmpty())
				{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("-ApplyNanite=Recommended found nothing to recommend; lower -NaniteMinTriangles or check the analysis.")); return 1; }
				Apply.OnlyMeshElements = Analysis.RecommendedElements;
			}
			ApplyResult = FConVerseDatasmithImportService::ApplyNanite(Apply);
			bApplyRan = true;
			bApplyOk = ApplyResult.bSucceeded;
			UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("%s"), *ApplyResult.Summary);
			if (!bApplyOk) UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Apply Nanite did not succeed."));
		}
	}

	// Written for failed runs too: a failed benchmark still shows where time and memory went.
	bool bBudgetMet = true;
	if (const FString* MetricsFile = Arguments.Find(TEXT("MetricsFile")))
	{
		const TSharedRef<FJsonObject> Metrics = BuildMetrics(Result, bAnalyzeOnly, Options.FilePath, bApplyRan ? &ApplyResult : nullptr, bAnalysisRan ? &Analysis : nullptr);
		FString Json;
		FJsonSerializer::Serialize(Metrics, TJsonWriterFactory<>::Create(&Json));
		if (!FFileHelper::SaveStringToFile(Json, **MetricsFile))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("Could not write metrics file '%s'."), **MetricsFile); return 1; }
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("Metrics: %s"), **MetricsFile);
		if (const FString* Budget = Arguments.Find(TEXT("Budget")))
		{
			bBudgetMet = CheckBudget(*Metrics, *Budget);
		}
	}

	if (!bSucceeded)
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error,
			TEXT("ConVerse optimized import did not succeed (status %s). See the report and log above."),
			ToString(Result.Status));
		return 1;
	}
	if (!bApplyOk)
	{
		return 1;
	}
	if (!bBudgetMet)
	{
		UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("ConVerse optimized import exceeded its benchmark budget."));
		return 1;
	}

	if (const FString* Directory = Arguments.Find(TEXT("GeometryDirectory")))
	{
		if (auto* Scene = Cast<UDatasmithScene>(Result.ImportAssetPath.ResolveObject()))
		{
			IFileManager::Get().MakeDirectory(**Directory, true);
			FString Inventory = TEXT("Element,Vertices,Triangles,NaniteEnabled,NaniteData\n");
			for (const auto& Pair : Scene->StaticMeshes)
			{
				UStaticMesh* Mesh = Pair.Value.LoadSynchronous();
				const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
				if (!Description) continue;
				FStaticMeshConstAttributes Attributes(*Description);
				const auto Positions = Attributes.GetVertexPositions();
				FString Obj;
				TMap<FVertexID, int32> Indices;
				int32 Index = 1;
				for (FVertexID Vertex : Description->Vertices().GetElementIDs())
				{
					Indices.Add(Vertex, Index++);
					const FVector3f P = Positions[Vertex];
					Obj += FString::Printf(TEXT("v %.9g %.9g %.9g\n"), P.X, P.Y, P.Z);
				}
				for (FTriangleID Triangle : Description->Triangles().GetElementIDs())
				{
					const auto Vertices = Description->GetTriangleVertices(Triangle);
					Obj += FString::Printf(TEXT("f %d %d %d\n"), Indices[Vertices[0]], Indices[Vertices[1]], Indices[Vertices[2]]);
				}
				Inventory += FString::Printf(TEXT("%s,%d,%d,%d,%d\n"), *Pair.Key.ToString(), Description->Vertices().Num(), Description->Triangles().Num(), ConVerseGetNaniteSettings(*Mesh).bEnabled, Mesh->HasValidNaniteData());
				if (!FFileHelper::SaveStringToFile(Obj, *(*Directory / (Pair.Key.ToString() + TEXT(".obj"))))) return 1;
			}
			if (!FFileHelper::SaveStringToFile(Inventory, *(*Directory / TEXT("meshes.csv")))) return 1;
		}
	}
	if (const FString* Map = Arguments.Find(TEXT("SaveAsMap")); Map && !bAnalyzeOnly)
	{
		if (!Map->StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(*Map) || FPackageName::DoesPackageExist(*Map)
			|| !UEditorLoadingAndSavingUtils::SaveMap(GEditor->GetEditorWorldContext().World(), *Map))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("SaveAsMap requires a new writable /Game map path.")); return 1; }
	}
	if (FParse::Param(*Params, TEXT("Save")) && !bAnalyzeOnly)
	{
		FString Message;
		if (!FConVerseDatasmithImportService::SaveImportedResult(Result.ManifestAssetPath, Message))
		{ UE_LOG(LogConVerseOptimizedImportCommandlet, Error, TEXT("%s"), *Message); return 1; }
		UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("%s"), *Message);
	}
	UE_LOG(LogConVerseOptimizedImportCommandlet, Display, TEXT("ConVerse optimized import succeeded."));
	return 0;
}
