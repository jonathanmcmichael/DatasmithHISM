// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithHISMStyle.h"
#include "DatasmithHISM.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FDatasmithHISMStyle::StyleInstance = nullptr;

void FDatasmithHISMStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FDatasmithHISMStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FDatasmithHISMStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("DatasmithHISMStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FDatasmithHISMStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("DatasmithHISMStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("DatasmithHISM")->GetBaseDir() / TEXT("Resources"));

	Style->Set("DatasmithHISM.PluginAction", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));
	return Style;
}

void FDatasmithHISMStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FDatasmithHISMStyle::Get()
{
	return *StyleInstance;
}
