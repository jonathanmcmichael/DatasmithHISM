#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

class FConVerseOptimizedReimportHandler;
class SDockTab;
class FSpawnTabArgs;

class FDatasmithHISMModule final : public IModuleInterface
{
public:
	virtual ~FDatasmithHISMModule() override;
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RunBatchToHISMs();
	void RunConsolidateSimilarMeshes();
	void RunCreateHISMs();
	void RunAnalyzeISMs();
	void RunEnableNanite();
	void RunExplodeISMs();
	void RunOneClickPipeline();
	void ToggleUseHISM();
	bool IsUseHISMEnabled() const;
	void OpenToolsPanel();
	void OpenOptimizedImportPanel();
	TSharedRef<SDockTab> SpawnToolsPanel(const FSpawnTabArgs&);
	TSharedRef<SDockTab> SpawnOptimizedImportPanel(const FSpawnTabArgs&);
	void RegisterMenus();
	bool CanRunSelectionTools() const;

	// Persisted toolbar ISM/HISM toggle state. Loaded from GConfig at startup.
	bool bToolbarUseHISM = false;
	TUniquePtr<FConVerseOptimizedReimportHandler> OptimizedReimportHandler;
};
