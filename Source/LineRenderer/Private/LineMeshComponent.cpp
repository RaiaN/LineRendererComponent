// Copyright Peter Leontev

#include "LineMeshComponent.h"
#include "LineMeshSceneProxy.h"

/*DECLARE_CYCLE_STAT(TEXT("Create ProcMesh Proxy"), STAT_ProcMesh_CreateSceneProxy, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Create Mesh Section"), STAT_ProcMesh_CreateMeshSection, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection GT"), STAT_ProcMesh_UpdateSectionGT, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection RT"), STAT_ProcMesh_UpdateSectionRT, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Get ProcMesh Elements"), STAT_ProcMesh_GetMeshElements, STATGROUP_ProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Update Collision"), STAT_ProcMesh_UpdateCollision, STATGROUP_ProceduralMesh);*/


DEFINE_LOG_CATEGORY_STATIC(LogLineRendererComponent, Log, All);

/**
/** Line section description */
struct FLineMeshSection
{
public:
    FLineMeshSection()
        : SectionLocalBox(ForceInit)
        , bSectionVisible(true)
        , SectionIndex(-1)
    {}

    /** Reset this section, clear all mesh info. */
    void Reset()
    {
        ProcVertexBuffer.Empty();
        SectionLocalBox.Init();
        bSectionVisible = true;
        SectionIndex = -1;
    }

public:
    TArray<FVector> ProcVertexBuffer;
    TArray<uint32> ProcIndexBuffer;

    FBox SectionLocalBox;
    bool bSectionVisible;
    int32 SectionIndex;
};

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

    UpdateLocalBounds(); // Update overall bounds

	PendingSections.Enqueue(NewSection);
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

    // Enqueue command to send to render thread
    FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    ENQUEUE_RENDER_COMMAND(FLineMeshSectionUpdate)(
        [ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
        {
            ProcMeshSceneProxy->UpdateSection_RenderThread(SectionData);
        }
    );

    UpdateLocalBounds();		 // Update overall bounds
    MarkRenderTransformDirty();  // Need to send new bounds to render thread
}

void ULineMeshComponent::RemoveLine(int32 SectionIndex)
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearMeshSection(SectionIndex);
}

void ULineMeshComponent::RemoveAllLines()
{
	FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
	LineMeshSceneProxy->ClearAllMeshSections();
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
    FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    LineMeshSceneProxy->UpdateLocalBounds();
    
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
	if (OverrideMaterials.Num() == 0)
	{
		if (Material == nullptr)
		{
			return UMaterial::GetDefaultMaterial(MD_Surface);
		}

		return Material;
	}

    return Super::GetMaterial(ElementIndex);
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
}