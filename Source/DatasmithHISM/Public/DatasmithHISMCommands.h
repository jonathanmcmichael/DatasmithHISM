// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "DatasmithHISMStyle.h"

class FDatasmithHISMCommands : public TCommands<FDatasmithHISMCommands>
{
public:

	FDatasmithHISMCommands()
		: TCommands<FDatasmithHISMCommands>(TEXT("DatasmithHISM"), NSLOCTEXT("Contexts", "DatasmithHISM", "DatasmithHISM Plugin"), NAME_None, FDatasmithHISMStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
