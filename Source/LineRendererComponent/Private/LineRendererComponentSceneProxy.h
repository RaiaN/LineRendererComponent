// Copyright 2023 Unreal Solutions Ltd. All Rights Reserved.

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
#include "Components/LineBatchComponent.h"


struct FLineSectionInfo;
struct FLineSectionUpdateData;
class ULineRendererComponent;
class FLineProxySection;

/** Procedural mesh scene proxy */
class FLineRendererComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FLineRendererComponentSceneProxy(ULineRendererComponent* InComponent);
	virtual ~FLineRendererComponentSceneProxy();

	SIZE_T GetTypeHash() const override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const;

	virtual bool CanBeOccluded() const;
	virtual uint32 GetMemoryFootprint() const;
	uint32 GetAllocatedSize() const;

public: 
    // Accessors for ULineRendererComponent
	int32 GetNumSections() const;
	int32 GetNumPointsInSection(int32 SectionIndex) const;
    void ClearMeshSection(int32 SectionIndex);
    void ClearAllMeshSections();
    void SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility);
    bool IsMeshSectionVisible(int32 SectionIndex) const;
	FBoxSphereBounds CalculateBounds() const;

private:
	void AddNewSection_GameThread(const FLineSectionInfo* NewSection);

private:
	ULineRendererComponent* Component;
	FMaterialRelevance MaterialRelevance;

	TMap<int32, TSharedPtr<FLineProxySection>> Sections_RenderThread;
};