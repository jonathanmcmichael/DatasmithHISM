#include "ConVersePowdercoatMaterialLibrary.h"

#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstantBiasScale.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

namespace ConVersePowdercoatMaterialLibrary
{
	template <typename ExpressionType>
	ExpressionType* CreateExpression(UMaterial* Material, const int32 X, const int32 Y)
	{
		return CastChecked<ExpressionType>(UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionType::StaticClass(), X, Y));
	}

	template <typename ParameterType>
	ParameterType* ConfigureParameter(ParameterType* Parameter, const FName Name, const FName Group, const int32 SortPriority)
	{
		Parameter->ParameterName = Name;
		Parameter->Group = Group;
		Parameter->SortPriority = SortPriority;
		return Parameter;
	}

	FString NormalizeAssetPath(const FString& AssetPath)
	{
		FString NormalizedPath = AssetPath;
		NormalizedPath.TrimStartAndEndInline();

		const int32 ObjectPathSeparator = NormalizedPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (ObjectPathSeparator != INDEX_NONE)
		{
			NormalizedPath = NormalizedPath.Left(ObjectPathSeparator);
		}

		return NormalizedPath;
	}
}

UMaterial* UConVersePowdercoatMaterialLibrary::CreatePowdercoatSubstrateMaterial(
	const FString& AssetPath,
	const FConVersePowdercoatMaterialSettings& Settings)
{
	const FString NormalizedAssetPath = ConVersePowdercoatMaterialLibrary::NormalizeAssetPath(AssetPath);
	const FString PackagePath = FPackageName::GetLongPackagePath(NormalizedAssetPath);
	const FString AssetName = FPackageName::GetLongPackageAssetName(NormalizedAssetPath);

	if (PackagePath.IsEmpty() || AssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackagePath))
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePowdercoatSubstrateMaterial: '%s' is not a valid asset path. Use a long package path such as /Game/Materials/M_Powdercoat."), *AssetPath);
		return nullptr;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), MaterialFactory));
	if (Material == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePowdercoatSubstrateMaterial: failed to create material asset '%s'."), *NormalizedAssetPath);
		return nullptr;
	}

	Material->Modify();
	Material->BlendMode = BLEND_Opaque;
	Material->bSubstrateRoughnessTracking = true;
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

	const FName PowdercoatGroup(TEXT("Powdercoat"));
	const FName CoatGroup(TEXT("Clearcoat"));

	UMaterialExpressionVectorParameter* BaseColor = ConVersePowdercoatMaterialLibrary::ConfigureParameter(
		ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionVectorParameter>(Material, -1800, -200),
		TEXT("PowdercoatColor"),
		PowdercoatGroup,
		0);
	BaseColor->DefaultValue = Settings.Color;

	UMaterialExpressionScalarParameter* OrangePeelAmount = ConVersePowdercoatMaterialLibrary::ConfigureParameter(
		ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionScalarParameter>(Material, -1800, 0),
		TEXT("OrangePeelAmount"),
		PowdercoatGroup,
		1);
	OrangePeelAmount->DefaultValue = Settings.OrangePeelAmount;
	OrangePeelAmount->SliderMin = 0.0f;
	OrangePeelAmount->SliderMax = 0.25f;

	UMaterialExpressionScalarParameter* OrangePeelScale = ConVersePowdercoatMaterialLibrary::ConfigureParameter(
		ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionScalarParameter>(Material, -1800, 180),
		TEXT("OrangePeelScale"),
		PowdercoatGroup,
		2);
	OrangePeelScale->DefaultValue = Settings.OrangePeelScale;
	OrangePeelScale->SliderMin = 0.1f;
	OrangePeelScale->SliderMax = 20.0f;

	UMaterialExpressionScalarParameter* Clearcoat = ConVersePowdercoatMaterialLibrary::ConfigureParameter(
		ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionScalarParameter>(Material, -1800, 560),
		TEXT("Clearcoat"),
		CoatGroup,
		0);
	Clearcoat->DefaultValue = Settings.Clearcoat;
	Clearcoat->SliderMin = 0.0f;
	Clearcoat->SliderMax = 1.0f;

	UMaterialExpressionScalarParameter* Thickness = ConVersePowdercoatMaterialLibrary::ConfigureParameter(
		ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionScalarParameter>(Material, -1800, 740),
		TEXT("ClearcoatThicknessCm"),
		CoatGroup,
		1);
	Thickness->DefaultValue = Settings.ThicknessCm;
	Thickness->SliderMin = 0.001f;
	Thickness->SliderMax = 0.05f;

	UMaterialExpressionWorldPosition* WorldPosition = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionWorldPosition>(Material, -1550, 120);
	WorldPosition->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;

	UMaterialExpressionMultiply* ScaledWorldPosition = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionMultiply>(Material, -1320, 120);
	UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPosition, TEXT(""), ScaledWorldPosition, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelScale, TEXT(""), ScaledWorldPosition, TEXT("B"));

	UMaterialExpressionNoise* NoiseX = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionNoise>(Material, -1090, 20);
	NoiseX->NoiseFunction = NOISEFUNCTION_GradientALU;
	NoiseX->Quality = 2;
	NoiseX->Levels = 3;
	NoiseX->LevelScale = 2.0f;
	NoiseX->bTurbulence = false;
	NoiseX->OutputMin = 0.0f;
	NoiseX->OutputMax = 1.0f;
	NoiseX->Scale = 1.0f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(ScaledWorldPosition, TEXT(""), NoiseX, TEXT("Position"));

	UMaterialExpressionConstant3Vector* NoiseOffset = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant3Vector>(Material, -1320, 360);
	NoiseOffset->Constant = FLinearColor(19.31f, 7.73f, 3.11f);

	UMaterialExpressionAdd* OffsetWorldPosition = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionAdd>(Material, -1090, 300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(ScaledWorldPosition, TEXT(""), OffsetWorldPosition, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(NoiseOffset, TEXT(""), OffsetWorldPosition, TEXT("B"));

	UMaterialExpressionNoise* NoiseY = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionNoise>(Material, -860, 300);
	NoiseY->NoiseFunction = NOISEFUNCTION_GradientALU;
	NoiseY->Quality = 2;
	NoiseY->Levels = 3;
	NoiseY->LevelScale = 2.0f;
	NoiseY->bTurbulence = false;
	NoiseY->OutputMin = 0.0f;
	NoiseY->OutputMax = 1.0f;
	NoiseY->Scale = 1.0f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(OffsetWorldPosition, TEXT(""), NoiseY, TEXT("Position"));

	UMaterialExpressionConstantBiasScale* CenterNoiseX = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstantBiasScale>(Material, -840, 20);
	CenterNoiseX->Bias = -0.5f;
	CenterNoiseX->Scale = 1.0f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(NoiseX, TEXT(""), CenterNoiseX, TEXT("Input"));

	UMaterialExpressionConstantBiasScale* CenterNoiseY = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstantBiasScale>(Material, -610, 300);
	CenterNoiseY->Bias = -0.5f;
	CenterNoiseY->Scale = 1.0f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(NoiseY, TEXT(""), CenterNoiseY, TEXT("Input"));

	UMaterialExpressionMultiply* OrangePeelSlopeX = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionMultiply>(Material, -610, 20);
	UMaterialEditingLibrary::ConnectMaterialExpressions(CenterNoiseX, TEXT(""), OrangePeelSlopeX, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelAmount, TEXT(""), OrangePeelSlopeX, TEXT("B"));

	UMaterialExpressionMultiply* OrangePeelSlopeY = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionMultiply>(Material, -380, 300);
	UMaterialEditingLibrary::ConnectMaterialExpressions(CenterNoiseY, TEXT(""), OrangePeelSlopeY, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelAmount, TEXT(""), OrangePeelSlopeY, TEXT("B"));

	UMaterialExpressionAppendVector* OrangePeelXY = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionAppendVector>(Material, -170, 140);
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelSlopeX, TEXT(""), OrangePeelXY, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelSlopeY, TEXT(""), OrangePeelXY, TEXT("B"));

	UMaterialExpressionConstant* UnitZ = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant>(Material, -160, 320);
	UnitZ->R = 1.0f;

	UMaterialExpressionAppendVector* OrangePeelNormalVector = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionAppendVector>(Material, 60, 180);
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelXY, TEXT(""), OrangePeelNormalVector, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(UnitZ, TEXT(""), OrangePeelNormalVector, TEXT("B"));

	UMaterialExpressionNormalize* OrangePeelNormal = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionNormalize>(Material, 280, 180);
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelNormalVector, TEXT(""), OrangePeelNormal, TEXT("VectorInput"));

	UMaterialExpressionConstant3Vector* DielectricF0 = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant3Vector>(Material, 360, -300);
	DielectricF0->Constant = FLinearColor(0.04f, 0.04f, 0.04f);

	UMaterialExpressionConstant3Vector* F90 = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant3Vector>(Material, 360, -120);
	F90->Constant = FLinearColor(1.0f, 1.0f, 1.0f);

	UMaterialExpressionConstant* BaseRoughness = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant>(Material, 360, 40);
	BaseRoughness->R = 0.42f;

	UMaterialExpressionMultiply* ClearcoatRoughness = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionMultiply>(Material, 360, 640);
	UMaterialExpressionConstant* ClearcoatRoughnessScale = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant>(Material, 120, 760);
	ClearcoatRoughnessScale->R = 0.12f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelAmount, TEXT(""), ClearcoatRoughness, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatRoughnessScale, TEXT(""), ClearcoatRoughness, TEXT("B"));

	UMaterialExpressionAdd* ClearcoatRoughnessBias = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionAdd>(Material, 590, 640);
	UMaterialExpressionConstant* ClearcoatRoughnessMin = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant>(Material, 360, 820);
	ClearcoatRoughnessMin->R = 0.015f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatRoughness, TEXT(""), ClearcoatRoughnessBias, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatRoughnessMin, TEXT(""), ClearcoatRoughnessBias, TEXT("B"));

	UMaterialExpressionSubstrateHazinessToSecondaryRoughness* Haziness = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionSubstrateHazinessToSecondaryRoughness>(Material, 600, -40);
	UMaterialEditingLibrary::ConnectMaterialExpressions(BaseRoughness, TEXT(""), Haziness, TEXT("BaseRoughness"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelAmount, TEXT(""), Haziness, TEXT("Haziness"));

	UMaterialExpressionConstant* SecondLobeWeight = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant>(Material, 840, 100);
	SecondLobeWeight->R = 0.35f;

	UMaterialExpressionSubstrateSlabBSDF* BaseSlab = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionSubstrateSlabBSDF>(Material, 1080, -120);
	UMaterialEditingLibrary::ConnectMaterialExpressions(BaseColor, TEXT(""), BaseSlab, TEXT("DiffuseAlbedo"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DielectricF0, TEXT(""), BaseSlab, TEXT("F0"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(F90, TEXT(""), BaseSlab, TEXT("F90"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(BaseRoughness, TEXT(""), BaseSlab, TEXT("Roughness"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Haziness, TEXT(""), BaseSlab, TEXT("SecondRoughness"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SecondLobeWeight, TEXT(""), BaseSlab, TEXT("SecondRoughnessWeight"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelNormal, TEXT(""), BaseSlab, TEXT("Normal"));

	UMaterialExpressionConstant3Vector* ClearcoatDiffuse = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant3Vector>(Material, 1080, 520);
	ClearcoatDiffuse->Constant = FLinearColor::Black;

	UMaterialExpressionConstant3Vector* ClearcoatTransmittance = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionConstant3Vector>(Material, 1080, 700);
	ClearcoatTransmittance->Constant = FLinearColor(0.985f, 0.985f, 0.985f);

	UMaterialExpressionSubstrateTransmittanceToMFP* ClearcoatMfp = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionSubstrateTransmittanceToMFP>(Material, 1310, 700);
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatTransmittance, TEXT(""), ClearcoatMfp, TEXT("TransmittanceColor"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Thickness, TEXT(""), ClearcoatMfp, TEXT("Thickness"));

	UMaterialExpressionSubstrateSlabBSDF* ClearcoatSlab = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionSubstrateSlabBSDF>(Material, 1550, 520);
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatDiffuse, TEXT(""), ClearcoatSlab, TEXT("DiffuseAlbedo"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(DielectricF0, TEXT(""), ClearcoatSlab, TEXT("F0"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(F90, TEXT(""), ClearcoatSlab, TEXT("F90"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatRoughnessBias, TEXT(""), ClearcoatSlab, TEXT("Roughness"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatMfp, TEXT(""), ClearcoatSlab, TEXT("SSSMFP"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(OrangePeelNormal, TEXT(""), ClearcoatSlab, TEXT("Normal"));

	UMaterialExpressionSubstrateWeight* ClearcoatWeight = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionSubstrateWeight>(Material, 1790, 520);
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatSlab, TEXT(""), ClearcoatWeight, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Clearcoat, TEXT(""), ClearcoatWeight, TEXT("Weight"));

	UMaterialExpressionSubstrateVerticalLayering* PowdercoatMaterial = ConVersePowdercoatMaterialLibrary::CreateExpression<UMaterialExpressionSubstrateVerticalLayering>(Material, 2040, 220);
	PowdercoatMaterial->bUseParameterBlending = true;
	UMaterialEditingLibrary::ConnectMaterialExpressions(ClearcoatWeight, TEXT(""), PowdercoatMaterial, TEXT("Top"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(BaseSlab, TEXT(""), PowdercoatMaterial, TEXT("Base"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Thickness, TEXT(""), PowdercoatMaterial, TEXT("Thickness"));

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	EditorOnlyData->FrontMaterial.Expression = PowdercoatMaterial;
	EditorOnlyData->FrontMaterial.OutputIndex = 0;
	EditorOnlyData->FrontMaterial.InputName = NAME_None;
	EditorOnlyData->FrontMaterial.Mask = 0;
	EditorOnlyData->FrontMaterial.MaskR = 0;
	EditorOnlyData->FrontMaterial.MaskG = 0;
	EditorOnlyData->FrontMaterial.MaskB = 0;
	EditorOnlyData->FrontMaterial.MaskA = 0;

	UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	return Material;
}
