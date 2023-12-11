// Copyright Peter Leontev

#include "LineMeshComponent.h"
#include "LineMeshSceneProxy.h"
#include "LineMeshSection.h"
#include "BatchedElements.h"


DEFINE_LOG_CATEGORY_STATIC(LogLineMeshComponent, Log, All);

ULineMeshComponent::ULineMeshComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void ULineMeshComponent::CreateLine2Points(int32 SectionIndex, const FVector& StartPoint, const FVector& EndPoint, const FLinearColor& Color, float Thickness, int32 NumSegments)
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

void ULineMeshComponent::CreateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color, float Thickness)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateMeshSection);

    TSharedPtr<FLineMeshSection> NewSection(MakeShareable(new FLineMeshSection));
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
            Line.Thickness = Thickness;
        }
    }

    NewSection->Material = CreateOrUpdateMaterial(SectionIndex, Color);

    // Enqueue command to send to render thread
    FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    ProcMeshSceneProxy->AddNewSection_GameThread(NewSection);
}

void ULineMeshComponent::UpdateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionGT);
    FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;

    if (SectionIndex >= LineMeshSceneProxy->GetNumSections())
    {
        return;
    }

    // Recreate line if mismatch in number of vertices
    if (Vertices.Num() != LineMeshSceneProxy->GetNumPointsInSection(SectionIndex))
    {
        CreateLine(SectionIndex, Vertices, Color, -1.0);
        return;
    }

    TSharedPtr<FLineMeshSectionUpdateData> SectionData(MakeShareable(new FLineMeshSectionUpdateData));
    SectionData->SectionIndex = SectionIndex;
    SectionData->Color = CreateOrUpdateSectionColor(SectionIndex, Color);
    SectionData->VertexBuffer.Append(Vertices);
    SectionData->SectionLocalBox = FBox3f(SectionData->VertexBuffer);

    // Enqueue command to send to render thread
    FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    ENQUEUE_RENDER_COMMAND(FLineMeshSectionUpdate)(
        [ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
        {
            ProcMeshSceneProxy->UpdateSection_RenderThread(SectionData);
        }
    );
}

void ULineMeshComponent::RemoveLine(int32 SectionIndex)
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearMeshSection(SectionIndex);

    SectionColors.Remove(SectionIndex);
}

void ULineMeshComponent::RemoveAllLines()
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearAllMeshSections();

    SectionColors.Empty();
}

void ULineMeshComponent::SetLineVisible(int32 SectionIndex, bool bNewVisibility)
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->SetMeshSectionVisible(SectionIndex, bNewVisibility);
}

bool ULineMeshComponent::IsLineVisible(int32 SectionIndex) const
{
    FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    return LineMeshSceneProxy->IsMeshSectionVisible(SectionIndex);
}

int32 ULineMeshComponent::GetNumSections() const
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	return LineMeshSceneProxy->GetNumSections();
}

FPrimitiveSceneProxy* ULineMeshComponent::CreateSceneProxy()
{
	// SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateSceneProxy);

	return new FLineMeshSceneProxy(this);
}

UMaterialInterface* ULineMeshComponent::GetMaterial(int32 ElementIndex) const
{
    if (SectionMaterials.Contains(ElementIndex))
    {
        return SectionMaterials[ElementIndex];
    }

    return nullptr;
}

void ULineMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /*= false*/) const
{
    Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);

    OutMaterials.Add(Material);

    for (TTuple<int32, UMaterialInstanceDynamic*> KeyValuePair : SectionMaterials)
    {
        OutMaterials.Add(KeyValuePair.Value);
    }
}

UMaterialInterface* ULineMeshComponent::CreateOrUpdateMaterial(int32 SectionIndex, const FLinearColor& Color)
{
    if (!SectionMaterials.Contains(SectionIndex))
    {
        UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(Material, this);
        SectionMaterials.Add(SectionIndex, MI);
        OverrideMaterials.Add(MI);
    }

    UMaterialInstanceDynamic* MI = SectionMaterials[SectionIndex];
    MI->SetVectorParameterValue(TEXT("LineColor"), Color);

    return MI;
}

FLinearColor ULineMeshComponent::CreateOrUpdateSectionColor(int32 SectionIndex, const FLinearColor& Color)
{
    if (!SectionColors.Contains(SectionIndex))
    {
        SectionColors.Add(SectionIndex, Color);
    }

    return SectionColors[SectionIndex];
}

FBoxSphereBounds ULineMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;

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
