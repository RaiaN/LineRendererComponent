// Copyright 2023 Unreal Solutions Ltd. All Rights Reserved.

#include "LineRendererComponent.h"
#include "BatchedElements.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "LineRendererComponentSceneProxy.h"
#include "LineSectionInfo.h"


DEFINE_LOG_CATEGORY_STATIC(LogLineMeshComponent, Log, All);

ULineRendererComponent::ULineRendererComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void ULineRendererComponent::CreateLine2Points(int32 SectionIndex, const FVector& StartPoint, const FVector& EndPoint, const FLinearColor& Color, float Thickness, int32 NumSegments)
{
    if (NumSegments == 1)
    {
        CreateLine(SectionIndex, { StartPoint, EndPoint }, Color, Thickness);
        return;
    }

    const FVector Step = (EndPoint - StartPoint) / FMath::Max(NumSegments, 1);

    TArray<FVector> Vertices;

    for (int32 SegmentInd = 0; SegmentInd <= NumSegments; ++SegmentInd)
    {
        const FVector& CurrPoint = StartPoint + Step * SegmentInd;
        Vertices.Add(CurrPoint);
    }

    CreateLine(SectionIndex, Vertices, Color, Thickness);
}

void ULineRendererComponent::CreateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color, float Thickness)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateMeshSection);

    TSharedPtr<FLineSectionInfo> NewSection(MakeShareable(new FLineSectionInfo));
    NewSection->SectionIndex = SectionIndex;
    NewSection->Color = Color;

    for (int32 Ind = 0; Ind < Vertices.Num() - 1; ++Ind)
    {
        const FVector& StartPoint = Vertices[Ind];
        const FVector& EndPoint = Vertices[Ind + 1];

        FBatchedLine& Line = NewSection->Lines.AddDefaulted_GetRef();
        {
            Line.Start = StartPoint;
            Line.End = EndPoint;
            Line.Color = Color;
            Line.Thickness = Thickness > 0.0f ? Thickness : 1.0f;
        }
    }

    NewSection->Material = CreateOrUpdateMaterial(SectionIndex, Color);

    // Enqueue command to send to render thread
    FLineRendererComponentSceneProxy* ProcMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
    ProcMeshSceneProxy->AddNewSection_GameThread(NewSection);
}

void ULineRendererComponent::UpdateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color, float Thickness)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionGT);
    FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;

    if (SectionIndex >= LineMeshSceneProxy->GetNumSections())
    {
        return;
    }

    // Recreate line if mismatch in number of vertices
    if (Vertices.Num() != LineMeshSceneProxy->GetNumPointsInSection(SectionIndex))
    {
        CreateLine(SectionIndex, Vertices, Color, Thickness);
        return;
    }

    TSharedPtr<FLineSectionUpdateData> SectionData(MakeShareable(new FLineSectionUpdateData));
    SectionData->SectionIndex = SectionIndex;
    SectionData->Color = CreateOrUpdateSectionColor(SectionIndex, Color);
    SectionData->VertexBuffer.Append(Vertices);
    SectionData->SectionLocalBox = FBox3f(SectionData->VertexBuffer);
    SectionData->Thickness = Thickness;

    // Enqueue command to send to render thread
    FLineRendererComponentSceneProxy* ProcMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
    ENQUEUE_RENDER_COMMAND(FLineMeshSectionUpdate)(
        [ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
        {
            ProcMeshSceneProxy->UpdateSection_RenderThread(SectionData);
        }
    );
}

void ULineRendererComponent::RemoveLine(int32 SectionIndex)
{
	FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearMeshSection(SectionIndex);

    SectionColors.Remove(SectionIndex);
}

void ULineRendererComponent::RemoveAllLines()
{
	FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearAllMeshSections();

    SectionColors.Empty();
}

void ULineRendererComponent::SetLineVisible(int32 SectionIndex, bool bNewVisibility)
{
	FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
	LineMeshSceneProxy->SetMeshSectionVisible(SectionIndex, bNewVisibility);
}

bool ULineRendererComponent::IsLineVisible(int32 SectionIndex) const
{
    FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
    return LineMeshSceneProxy->IsMeshSectionVisible(SectionIndex);
}

int32 ULineRendererComponent::GetNumSections() const
{
	FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;
	return LineMeshSceneProxy->GetNumSections();
}

FPrimitiveSceneProxy* ULineRendererComponent::CreateSceneProxy()
{
	// SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateSceneProxy);

	return new FLineRendererComponentSceneProxy(this);
}

UMaterialInterface* ULineRendererComponent::GetMaterial(int32 ElementIndex) const
{
    if (SectionMaterials.Contains(ElementIndex))
    {
        return SectionMaterials[ElementIndex];
    }

    return nullptr;
}

void ULineRendererComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /*= false*/) const
{
    Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);

    OutMaterials.Add(Material);

    for (TTuple<int32, UMaterialInstanceDynamic*> KeyValuePair : SectionMaterials)
    {
        OutMaterials.Add(KeyValuePair.Value);
    }
}

UMaterialInterface* ULineRendererComponent::CreateOrUpdateMaterial(int32 SectionIndex, const FLinearColor& Color)
{
    if (!SectionMaterials.Contains(SectionIndex))
    {
        UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(Material, this);
        SectionMaterials.Add(SectionIndex, MI);
        // OverrideMaterials.Add(MI);
    }

    UMaterialInstanceDynamic* MI = SectionMaterials[SectionIndex];
    MI->SetVectorParameterValue(TEXT("LineColor"), Color);

    return MI;
}

FLinearColor ULineRendererComponent::CreateOrUpdateSectionColor(int32 SectionIndex, const FLinearColor& Color)
{
    if (!SectionColors.Contains(SectionIndex))
    {
        SectionColors.Add(SectionIndex, Color);
    }

    return SectionColors[SectionIndex];
}

FBoxSphereBounds ULineRendererComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    FLineRendererComponentSceneProxy* LineMeshSceneProxy = (FLineRendererComponentSceneProxy*)SceneProxy;

    FBoxSphereBounds LocalBounds(FBoxSphereBounds3f(FVector3f(0, 0, 0), FVector3f(0, 0, 0), 0));
    if (LineMeshSceneProxy != nullptr)
    {
        LocalBounds = LineMeshSceneProxy->GetLocalBounds();
    }

    FBoxSphereBounds Ret(FBoxSphereBounds(LocalBounds).TransformBy(LocalToWorld));

    Ret.BoxExtent *= BoundsScale;
    Ret.SphereRadius *= BoundsScale;

    return Ret;
}
