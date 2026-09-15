#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ConVersePowdercoatMaterialLibrary.generated.h"

class UMaterial;

USTRUCT(BlueprintType)
struct DATASMITHHISM_API FConVersePowdercoatMaterialSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Powdercoat")
	FLinearColor Color = FLinearColor(0.62f, 0.08f, 0.06f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Powdercoat", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.25"))
	float OrangePeelAmount = 0.045f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Powdercoat", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "20.0"))
	float OrangePeelScale = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Powdercoat", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Clearcoat = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Powdercoat", meta = (ClampMin = "0.001", UIMin = "0.001", UIMax = "0.05"))
	float ThicknessCm = 0.008f;
};

UCLASS()
class DATASMITHHISM_API UConVersePowdercoatMaterialLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DatasmithHISM|Materials", meta = (DisplayName = "Create Powdercoat Substrate Material"))
	static UMaterial* CreatePowdercoatSubstrateMaterial(const FString& AssetPath, const FConVersePowdercoatMaterialSettings& Settings);
};
