// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "LineMeshComponent.generated.h"


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
	void CreateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void UpdateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color);

	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void ClearMeshSection(int32 SectionIndex);

	/** Clear all mesh sections and reset to empty state */
	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void ClearAllMeshSections();

	/** Control visibility of a particular section */
	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	void SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility);

	/** Returns whether a particular section is currently visible */
	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	bool IsMeshSectionVisible(int32 SectionIndex) const;

	/** Returns number of sections currently created for this component */
	UFUNCTION(BlueprintCallable, Category = "Components|LineRenderer")
	int32 GetNumSections() const;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface.

private:
	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();

private:
    /** Pending line mesh sections */
    TQueue<TSharedPtr<FLineMeshSection>> PendingSections;

	/** Local space bounds of mesh */
    FBoxSphereBounds LocalBounds;

    friend class FLineMeshSceneProxy;
};


