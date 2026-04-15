// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithHISM.h"
#include "DatasmithHISMStyle.h"
#include "DatasmithHISMCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

static const FName DatasmithHISMTabName("DatasmithHISM");

#define LOCTEXT_NAMESPACE "FDatasmithHISMModule"

void FDatasmithHISMModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FDatasmithHISMStyle::Initialize();
	FDatasmithHISMStyle::ReloadTextures();

	FDatasmithHISMCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FDatasmithHISMCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FDatasmithHISMModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDatasmithHISMModule::RegisterMenus));
}

void FDatasmithHISMModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FDatasmithHISMStyle::Shutdown();

	FDatasmithHISMCommands::Unregister();
}

void FDatasmithHISMModule::PluginButtonClicked()
{
	// Put your "OnButtonClicked" stuff here
	FText DialogText = FText::Format(
							LOCTEXT("PluginButtonDialogText", "Add code to {0} in {1} to override this button's actions"),
							FText::FromString(TEXT("FDatasmithHISMModule::PluginButtonClicked()")),
							FText::FromString(TEXT("DatasmithHISM.cpp"))
					   );
	FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}

void FDatasmithHISMModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FDatasmithHISMCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FDatasmithHISMCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FDatasmithHISMModule, DatasmithHISM)