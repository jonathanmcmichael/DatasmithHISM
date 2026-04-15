// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithHISMCommands.h"

#define LOCTEXT_NAMESPACE "FDatasmithHISMModule"

void FDatasmithHISMCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "DatasmithHISM", "Execute DatasmithHISM action", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
