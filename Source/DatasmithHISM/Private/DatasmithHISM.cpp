#include "DatasmithHISM.h"

#include "ConVerseBatchHISMLibrary.h"
#include "ConVerseDatasmithImportPanel.h"
#include "ConVerseDatasmithToolsPanel.h"
#include "ConVerseHISMLibrary.h"
#include "ConVerseOptimizedReimportHandler.h"
#include "ConVerseStaticMeshConsolidationLibrary.h"
#include "DatasmithHISMStyle.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DatasmithHISM"

static const TCHAR* DatasmithHISMConfigSection = TEXT("DatasmithHISM");
static const TCHAR* DatasmithHISMUseHISMKey = TEXT("bUseHISM");
static const FName DatasmithHISMToolsTabName(TEXT("DatasmithHISM.Tools"));
static const FName DatasmithHISMOptimizedImportTabName(TEXT("DatasmithHISM.OptimizedDatasmithImport"));

FDatasmithHISMModule::~FDatasmithHISMModule() = default;

void FDatasmithHISMModule::StartupModule()
{
	FDatasmithHISMStyle::Initialize();
	OptimizedReimportHandler = MakeUnique<FConVerseOptimizedReimportHandler>();

	const TSharedRef<FGlobalTabmanager> GlobalTabManager = FGlobalTabmanager::Get();
	if (GlobalTabManager->HasTabSpawner(DatasmithHISMToolsTabName))
	{
		GlobalTabManager->UnregisterNomadTabSpawner(DatasmithHISMToolsTabName);
	}

	GlobalTabManager->RegisterNomadTabSpawner(
		DatasmithHISMToolsTabName,
		FOnSpawnTab::CreateRaw(this, &FDatasmithHISMModule::SpawnToolsPanel))
		.SetDisplayName(LOCTEXT("ToolsTabTitle", "DatasmithHISM Tools"))
		.SetTooltipText(LOCTEXT("ToolsTabTooltip", "Open DatasmithHISM selection and import tools."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	if (GlobalTabManager->HasTabSpawner(DatasmithHISMOptimizedImportTabName))
	{
		GlobalTabManager->UnregisterNomadTabSpawner(DatasmithHISMOptimizedImportTabName);
	}

	GlobalTabManager->RegisterNomadTabSpawner(
		DatasmithHISMOptimizedImportTabName,
		FOnSpawnTab::CreateRaw(this, &FDatasmithHISMModule::SpawnOptimizedImportPanel))
		.SetDisplayName(LOCTEXT("OptimizedImportTabTitle", "Optimized Datasmith Import"))
		.SetTooltipText(LOCTEXT(
			"OptimizedImportTabTooltip",
			"Analyze, import, convert, and verify a Datasmith scene as ISM or HISM components."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// Restore the last-used ISM/HISM toggle setting from editor config.
	GConfig->GetBool(DatasmithHISMConfigSection, DatasmithHISMUseHISMKey, bToolbarUseHISM, GEditorPerProjectIni);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDatasmithHISMModule::RegisterMenus));
}

void FDatasmithHISMModule::ShutdownModule()
{
	OptimizedReimportHandler.Reset();

	if (UToolMenus::TryGet() != nullptr)
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	if (FSlateApplication::IsInitialized())
	{
		const TSharedRef<FGlobalTabmanager> GlobalTabManager = FGlobalTabmanager::Get();
		if (const TSharedPtr<SDockTab> ExistingTab = GlobalTabManager->FindExistingLiveTab(DatasmithHISMToolsTabName))
		{
			ExistingTab->RequestCloseTab();
		}

		if (GlobalTabManager->HasTabSpawner(DatasmithHISMToolsTabName))
		{
			GlobalTabManager->UnregisterNomadTabSpawner(DatasmithHISMToolsTabName);
		}

		if (const TSharedPtr<SDockTab> ExistingTab = GlobalTabManager->FindExistingLiveTab(DatasmithHISMOptimizedImportTabName))
		{
			ExistingTab->RequestCloseTab();
		}

		if (GlobalTabManager->HasTabSpawner(DatasmithHISMOptimizedImportTabName))
		{
			GlobalTabManager->UnregisterNomadTabSpawner(DatasmithHISMOptimizedImportTabName);
		}
	}

	FDatasmithHISMStyle::Shutdown();
}

void FDatasmithHISMModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& ToolbarSection = ToolbarMenu->FindOrAddSection("DatasmithHISM");
	ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		"DatasmithHISMOpenTools",
		FToolUIActionChoice(FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::OpenToolsPanel))),
		LOCTEXT("OpenToolsToolbarLabel", "Datasmith Tools"),
		LOCTEXT("OpenToolsToolbarTooltip", "Open the DatasmithHISM tools panel."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ManagedHISMs"),
		EUserInterfaceActionType::Button));

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("DatasmithHISM");
	ToolsSection.AddMenuEntry(
		"DatasmithHISMOptimizedImport",
		LOCTEXT("OptimizedImportMenuLabel", "Optimized Datasmith Import"),
		LOCTEXT(
			"OptimizedImportMenuTooltip",
			"Use when importing a new Datasmith scene, including CAD, Revit, and IFC sources. Optimizes before individual source actors populate the level, with ISM or HISM output and verification."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::OpenOptimizedImportPanel)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMConsolidateSimilarMeshes",
		LOCTEXT("ConsolidateMenuLabel", "Dedupe Meshes In Selection"),
		LOCTEXT("ConsolidateMenuTooltip", "Use when an existing level has separate but duplicate static mesh assets and you want Content Browser cleanup. Does not create ISMs or change actor layout."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ConsolidateMeshes"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunConsolidateSimilarMeshes),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMCreateHISMs",
		LOCTEXT("CreateHISMMenuLabel", "Create Managed ISMs From Selection"),
		LOCTEXT("CreateHISMMenuTooltip", "Use when an existing Datasmith or IFC level needs post-import conversion. Groups repeated geometry by family, mesh, and materials into managed ISM or HISM components."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ManagedHISMs"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunCreateHISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMBatchToHISMs",
		LOCTEXT("BatchHISMMenuLabel", "Batch Selection To ISMs (Unreal)"),
		LOCTEXT("BatchHISMMenuTooltip", "Use when an existing level only needs Unreal's standard same-mesh batching. Faster and simpler than Managed ISMs, but not family-structure-aware."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.BatchHISMs"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunBatchToHISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMToggleUseHISM",
		LOCTEXT("ToggleHISMMenuLabel", "Use HISM For Managed ISMs"),
		LOCTEXT("ToggleHISMMenuTooltip", "When checked, Managed ISMs and Analyze ISMs use UHierarchicalInstancedStaticMeshComponent. Saved per project."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ToggleUseHISM"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::ToggleUseHISM),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FDatasmithHISMModule::IsUseHISMEnabled)),
		EUserInterfaceActionType::ToggleButton);

	ToolsSection.AddMenuEntry(
		"DatasmithHISMAnalyzeISMs",
		LOCTEXT("AnalyzeISMMenuLabel", "Analyze ISM Candidates In Selection"),
		LOCTEXT("AnalyzeISMMenuTooltip", "Dry-run of Managed ISMs. Shows how many ISM groups would be created and how many actors would be converted, without making any changes."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.AnalyzeISMs"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunAnalyzeISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMEnableNanite",
		LOCTEXT("EnableNaniteMenuLabel", "Enable Nanite On Selection"),
		LOCTEXT("EnableNaniteMenuTooltip", "Enables Nanite on all static mesh assets referenced by the current selection. Mesh rebuilds are queued asynchronously."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.EnableNanite"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunEnableNanite),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMExplodeISMs",
		LOCTEXT("ExplodeISMMenuLabel", "Explode ISMs In Selection"),
		LOCTEXT("ExplodeISMMenuTooltip", "Reverse of Managed ISMs. Spawns one static mesh actor per ISM instance, then removes the ISM components. Wrapped in a single undo transaction."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ExplodeISMs"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunExplodeISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMOneClickPipeline",
		LOCTEXT("OneClickMenuLabel", "Dedupe Meshes + Managed ISMs"),
		LOCTEXT("OneClickMenuTooltip", "Runs Dedupe Meshes then Managed ISMs in sequence on the selection."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.OneClickPipeline"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunOneClickPipeline),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));
}

void FDatasmithHISMModule::OpenToolsPanel()
{
	const TSharedRef<FGlobalTabmanager> GlobalTabManager = FGlobalTabmanager::Get();
	if (GlobalTabManager->HasTabSpawner(DatasmithHISMToolsTabName))
	{
		GlobalTabManager->TryInvokeTab(DatasmithHISMToolsTabName);
	}
}

TSharedRef<SDockTab> FDatasmithHISMModule::SpawnToolsPanel(const FSpawnTabArgs&)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ToolsPanelTabLabel", "DatasmithHISM Tools"))
		[
			SNew(SConVerseDatasmithToolsPanel)
			.OnOpenOptimizedImport(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::OpenOptimizedImportPanel))
			.OnDedupeMeshes(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunConsolidateSimilarMeshes))
			.OnCreateManagedISMs(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunCreateHISMs))
			.OnBatchISMs(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunBatchToHISMs))
			.OnAnalyzeISMs(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunAnalyzeISMs))
			.OnEnableNanite(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunEnableNanite))
			.OnExplodeISMs(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunExplodeISMs))
			.OnDedupeAndCreateISMs(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::RunOneClickPipeline))
			.OnToggleUseHISM(FSimpleDelegate::CreateRaw(this, &FDatasmithHISMModule::ToggleUseHISM))
			.CanRunSelectionTools(TAttribute<bool>::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools))
			.UseHISMEnabled(TAttribute<bool>::CreateRaw(this, &FDatasmithHISMModule::IsUseHISMEnabled))
		];
}

void FDatasmithHISMModule::OpenOptimizedImportPanel()
{
	const TSharedRef<FGlobalTabmanager> GlobalTabManager = FGlobalTabmanager::Get();
	if (GlobalTabManager->HasTabSpawner(DatasmithHISMOptimizedImportTabName))
	{
		GlobalTabManager->TryInvokeTab(DatasmithHISMOptimizedImportTabName);
	}
}

TSharedRef<SDockTab> FDatasmithHISMModule::SpawnOptimizedImportPanel(const FSpawnTabArgs&)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("OptimizedImportPanelTabLabel", "Optimized Datasmith Import"))
		[
			SNew(SConVerseDatasmithImportPanel)
		];
}

void FDatasmithHISMModule::RunConsolidateSimilarMeshes()
{
	const FConVerseStaticMeshConsolidationResult Result =
		UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshesInSelection(true);

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunCreateHISMs()
{
	const FConVerseHISMCreationResult Result = UConVerseHISMLibrary::CreateISMsFromSelection(TEXT("ISM"), bToolbarUseHISM);

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunBatchToHISMs()
{
	const FConVerseBatchHISMResult Result = UConVerseBatchHISMLibrary::BatchSelectionToHISMs();

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunAnalyzeISMs()
{
	const FConVerseHISMAnalysisResult Result = UConVerseHISMLibrary::AnalyzeISMCandidatesInSelection(TEXT("ISM"), bToolbarUseHISM);

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunEnableNanite()
{
	const FConVerseEnableNaniteResult Result = UConVerseHISMLibrary::EnableNaniteOnSelection();

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::ToggleUseHISM()
{
	bToolbarUseHISM = !bToolbarUseHISM;
	GConfig->SetBool(DatasmithHISMConfigSection, DatasmithHISMUseHISMKey, bToolbarUseHISM, GEditorPerProjectIni);
}

bool FDatasmithHISMModule::IsUseHISMEnabled() const
{
	return bToolbarUseHISM;
}

void FDatasmithHISMModule::RunExplodeISMs()
{
	const FConVerseISMExplodeResult Result = UConVerseHISMLibrary::ExplodeISMsFromSelection();

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunOneClickPipeline()
{
	// Phase 1: Dedupe. The next phase must not run after a declined or failed destructive operation.
	const FConVerseStaticMeshConsolidationResult DedupeResult =
		UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshesInSelection(true);

	if (DedupeResult.Outcome != EConVerseStaticMeshConsolidationOutcome::Completed)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
			FString::Printf(TEXT("Dedupe: %s\n\nManaged ISMs were not run."), *DedupeResult.Summary)));
		return;
	}

	// Phase 2: Managed ISMs (respects the toolbar HISM toggle).
	const FConVerseHISMCreationResult ISMResult = UConVerseHISMLibrary::CreateISMsFromSelection(TEXT("ISM"), bToolbarUseHISM);

	const FString CombinedSummary = FString::Printf(
		TEXT("Dedupe: %s\n\nManaged ISMs: %s"),
		*DedupeResult.Summary,
		*ISMResult.Summary);

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CombinedSummary));
}

bool FDatasmithHISMModule::CanRunSelectionTools() const
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDatasmithHISMModule, DatasmithHISM)
