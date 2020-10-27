// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"

#include "StaticMeshResources.h"
#include "RawIndexBuffer.h"
#include "LocalVertexFactory.h"
#include "RHIDefinitions.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Materials/MaterialRelevance.h"


class FLineMeshProxySection;
class ULineMeshComponent;
class FLineMeshSectionUpdateData;


/** Procedural mesh scene proxy */
class FLineMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FLineMeshSceneProxy(ULineMeshComponent* InComponent);
	virtual ~FLineMeshSceneProxy();

	SIZE_T GetTypeHash() const override;

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FLineMeshSectionUpdateData* SectionData);

	void SetSectionVisibility_RenderThread(int32 SectionIndex, bool bNewVisibility);

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const;

	virtual bool CanBeOccluded() const;
	virtual uint32 GetMemoryFootprint() const;
	uint32 GetAllocatedSize() const;

private:
	ULineMeshComponent* Component;
	FMaterialRelevance MaterialRelevance;

	mutable TMap<int32, TSharedPtr<FLineMeshProxySection>> Sections;
};