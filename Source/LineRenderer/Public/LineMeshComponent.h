// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "LineMeshComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class FPrimitiveSceneProxy;
class FLineMeshSceneProxy;
struct FLineMeshSection;


UCLASS(hidecategories = (Object, LOD), meta = (BlueprintSpawnableComponent))
class LINERENDERER_API ULineMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	ULineMeshComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void CreateLine2Points(int32 SectionIndex, const FVector& StartPoint, const FVector& EndPoint, const FLinearColor& Color, float Thickness, int32 NumSegments = 1);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void CreateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color, float Thickness);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void UpdateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void RemoveLine(int32 SectionIndex);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void RemoveAllLines();

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void SetLineVisible(int32 SectionIndex, bool bNewVisibility);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	bool IsLineVisible(int32 SectionIndex) const;

	/** Returns number of sections currently created for this component */
	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	int32 GetNumSections() const;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInterface* Material;

protected:
	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UMeshComponent Interface.

private: 
	UMaterialInterface* CreateOrUpdateMaterial(int32 SectionIndex, const FLinearColor& Color);

	FLinearColor CreateOrUpdateSectionColor(int32 SectionIndex, const FLinearColor& Color);

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();

private:
	UPROPERTY()
    TMap<int32, UMaterialInstanceDynamic*> SectionMaterials;

    UPROPERTY()
    TMap<int32, FLinearColor> SectionColors;

    friend class FLineMeshSceneProxy;
};


