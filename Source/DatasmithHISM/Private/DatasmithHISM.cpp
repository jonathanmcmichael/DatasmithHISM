#include "DatasmithHISM.h"

#include "ConVerseBatchHISMLibrary.h"
#include "ConVerseHISMLibrary.h"
#include "ConVerseStaticMeshConsolidationLibrary.h"
#include "DatasmithHISMStyle.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DatasmithHISM"

void FDatasmithHISMModule::StartupModule()
{
	FDatasmithHISMStyle::Initialize();
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDatasmithHISMModule::RegisterMenus));
}

void FDatasmithHISMModule::ShutdownModule()
{
	if (UToolMenus::TryGet() != nullptr)
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	FDatasmithHISMStyle::Shutdown();
}

void FDatasmithHISMModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& ToolbarSection = ToolbarMenu->FindOrAddSection("DatasmithHISM");
	ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		"DatasmithHISMConsolidateSimilarMeshes",
		FToolUIActionChoice(FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunConsolidateSimilarMeshes),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools))),
		LOCTEXT("ConsolidateToolbarLabel", "Dedupe Meshes"),
		LOCTEXT("ConsolidateToolbarTooltip", "Repoints the selected actors to shared duplicate mesh assets and deletes the extra mesh assets. This does not create HISMs or change actor layout."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ConsolidateMeshes"),
		EUserInterfaceActionType::Button));

	ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		"DatasmithHISMCreateHISMs",
		FToolUIActionChoice(FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunCreateHISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools))),
		LOCTEXT("CreateHISMToolbarLabel", "Managed ISMs"),
		LOCTEXT("CreateHISMToolbarTooltip", "ConVerse managed conversion. Groups repeated Datasmith geometry by family label, mesh, and materials, creates organized Family Type actors with ISM components, and removes converted source actors where safe. Nanite-compatible."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ManagedHISMs"),
		EUserInterfaceActionType::Button));

	ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		"DatasmithHISMBatchToHISMs",
		FToolUIActionChoice(FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunBatchToHISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools))),
		LOCTEXT("BatchHISMToolbarLabel", "Batch ISMs"),
		LOCTEXT("BatchHISMToolbarTooltip", "Unreal built-in batch instancing. Replaces repeated selected actors with ISM components based on matching meshes and materials. Faster and simpler, but less family-structure-aware than Managed ISMs. Nanite-compatible."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.BatchHISMs"),
		EUserInterfaceActionType::Button));

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("DatasmithHISM");
	ToolsSection.AddMenuEntry(
		"DatasmithHISMConsolidateSimilarMeshes",
		LOCTEXT("ConsolidateMenuLabel", "Dedupe Meshes In Selection"),
		LOCTEXT("ConsolidateMenuTooltip", "Repoints the selected actors to shared duplicate mesh assets and deletes the extra mesh assets. This does not create HISMs or change actor layout."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ConsolidateMeshes"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunConsolidateSimilarMeshes),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMCreateHISMs",
		LOCTEXT("CreateHISMMenuLabel", "Create Managed ISMs From Selection"),
		LOCTEXT("CreateHISMMenuTooltip", "ConVerse managed conversion. Groups repeated Datasmith geometry by family label, mesh, and materials, creates organized Family Type actors with ISM components, and removes converted source actors where safe. Nanite-compatible."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.ManagedHISMs"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunCreateHISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));

	ToolsSection.AddMenuEntry(
		"DatasmithHISMBatchToHISMs",
		LOCTEXT("BatchHISMMenuLabel", "Batch Selection To ISMs (Unreal)"),
		LOCTEXT("BatchHISMMenuTooltip", "Unreal built-in batch instancing. Replaces repeated selected actors with ISM components based on matching meshes and materials. Faster and simpler, but less family-structure-aware than Managed ISMs. Nanite-compatible."),
		FSlateIcon(FDatasmithHISMStyle::GetStyleSetName(), "DatasmithHISM.BatchHISMs"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::RunBatchToHISMs),
			FCanExecuteAction::CreateRaw(this, &FDatasmithHISMModule::CanRunSelectionTools)));
}

void FDatasmithHISMModule::RunConsolidateSimilarMeshes()
{
	const FConVerseStaticMeshConsolidationResult Result =
		UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshesInSelection(true);

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunCreateHISMs()
{
	const FConVerseHISMCreationResult Result = UConVerseHISMLibrary::CreateHISMsFromSelection(TEXT("HISM"));

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
}

void FDatasmithHISMModule::RunBatchToHISMs()
{
	const FConVerseBatchHISMResult Result = UConVerseBatchHISMLibrary::BatchSelectionToHISMs();

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Summary));
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
