// Copyright Peter Leontev

#include "LineMeshComponent.h"
#include "LineMeshSceneProxy.h"
#include "LineMeshSection.h"

/*DECLARE_CYCLE_STAT(TEXT("Create ProcMesh Proxy"), STAT_ProcMesh_CreateSceneProxy, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Create Mesh Section"), STAT_ProcMesh_CreateMeshSection, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection GT"), STAT_ProcMesh_UpdateSectionGT, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection RT"), STAT_ProcMesh_UpdateSectionRT, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Get ProcMesh Elements"), STAT_ProcMesh_GetMeshElements, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Update Collision"), STAT_ProcMesh_UpdateCollision, STATGROUP_ProceduralMesh);*/


DEFINE_LOG_CATEGORY_STATIC(LogLineRendererComponent, Log, All);

ULineMeshComponent::ULineMeshComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void ULineMeshComponent::CreateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& Color)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateMeshSection);

    TSharedPtr<FLineMeshSection> NewSection(MakeShareable(new FLineMeshSection));

    // Copy data to vertex buffer
    const int32 NumVerts = Vertices.Num();
    NewSection->ProcVertexBuffer.Reset();
    NewSection->ProcVertexBuffer.Reserve(NumVerts);

    for (int32 VertIdx = 0; VertIdx < NumVerts; VertIdx++)
    {
		NewSection->ProcVertexBuffer.Add(Vertices[VertIdx]);
		NewSection->SectionLocalBox += Vertices[VertIdx];
    }

	if (Vertices.Num() - 1 > 0)
	{
		NewSection->ProcIndexBuffer.Reset();
		NewSection->ProcIndexBuffer.SetNumZeroed(2 * (Vertices.Num() - 1) + 1);
	}

	const int32 NumTris = Vertices.Num() - 1;

	for (int32 TriInd = 0; TriInd < NumTris; ++TriInd)
	{
		NewSection->ProcIndexBuffer[2 * TriInd] = TriInd;
		NewSection->ProcIndexBuffer[2 * TriInd + 1] = TriInd + 1;
	}

	NewSection->SectionIndex = SectionIndex;
    NewSection->SectionLocalBox = FBox(Vertices);
    NewSection->Material = CreateOrUpdateMaterial(SectionIndex, Color);

    // Enqueue command to send to render thread
    FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    ProcMeshSceneProxy->AddNewSection_GameThread(NewSection);
    
    UpdateLocalBounds();
    MarkRenderTransformDirty();
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
        CreateLine(SectionIndex, Vertices, Color);
        return;
    }

    TSharedPtr<FLineMeshSectionUpdateData> SectionData(MakeShareable(new FLineMeshSectionUpdateData));
    SectionData->SectionIndex = SectionIndex;
    SectionData->SectionLocalBox = FBox(Vertices);
    SectionData->VertexBuffer = Vertices;

    if (Vertices.Num() - 1 > 0)
    {
        SectionData->IndexBuffer.Reset();
        SectionData->IndexBuffer.SetNumZeroed(2 * (Vertices.Num() - 1) + 1);
    }

    const int32 NumTris = Vertices.Num() - 1;
    for (int32 TriInd = 0; TriInd < NumTris; ++TriInd)
    {
        SectionData->IndexBuffer[2 * TriInd] = TriInd;
        SectionData->IndexBuffer[2 * TriInd + 1] = TriInd + 1;
    }

    SectionData->Material = CreateOrUpdateMaterial(SectionIndex, Color);

    // Enqueue command to send to render thread
    FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    ENQUEUE_RENDER_COMMAND(FLineMeshSectionUpdate)(
        [ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
        {
            ProcMeshSceneProxy->UpdateSection_RenderThread(SectionData);
        }
    );

    UpdateLocalBounds();
    MarkRenderTransformDirty();
}

void ULineMeshComponent::RemoveLine(int32 SectionIndex)
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearMeshSection(SectionIndex);

    SectionMaterials.Remove(SectionIndex);
}

void ULineMeshComponent::RemoveAllLines()
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearAllMeshSections();

    SectionMaterials.Empty();
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

int32 ULineMeshComponent::GetNumLines() const
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	return LineMeshSceneProxy->GetNumSections();
}

void ULineMeshComponent::UpdateLocalBounds()
{
    // FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    // LineMeshSceneProxy->UpdateLocalBounds();
    
    // Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();
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

UMaterialInterface* ULineMeshComponent::CreateOrUpdateMaterial(int32 SectionIndex, const FLinearColor& Color)
{
    if (!SectionMaterials.Contains(SectionIndex))
    {
        UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(Material, Material);
        SectionMaterials.Add(SectionIndex, MI);
        OverrideMaterials.Add(MI);
    }

    UMaterialInstanceDynamic* MI = SectionMaterials[SectionIndex];
    MI->SetVectorParameterValue(TEXT("LineColor"), Color);

    return MI;
}

FBoxSphereBounds ULineMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;

    FBoxSphereBounds LocalBounds = FBoxSphereBounds(FVector(0, 0, 0), FVector(0, 0, 0), 0);
    if (LineMeshSceneProxy != nullptr)
    {
        LocalBounds = LineMeshSceneProxy->GetLocalBounds();
    }

    FBoxSphereBounds Ret(LocalBounds.TransformBy(LocalToWorld));

    Ret.BoxExtent *= BoundsScale;
    Ret.SphereRadius *= BoundsScale;

    return Ret;
}

void ULineMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials /*= false*/) const
{
    Super::GetUsedMaterials(OutMaterials, false);

	OutMaterials.Add(Material);

    for (TTuple<int32, UMaterialInstanceDynamic*> KeyValuePair : SectionMaterials)
    {
        OutMaterials.Add(KeyValuePair.Value);
    }
}