#pragma once

#include "Modules/ModuleInterface.h"

class FDatasmithHISMModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RunBatchToHISMs();
	void RunConsolidateSimilarMeshes();
	void RunCreateHISMs();
	void RegisterMenus();
	bool CanRunSelectionTools() const;
};
