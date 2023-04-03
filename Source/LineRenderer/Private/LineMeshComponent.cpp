// Copyright Peter Leontev

#include "LineMeshComponent.h"
#include "LineMeshSceneProxy.h"
#include "LineMeshSection.h"


DEFINE_LOG_CATEGORY_STATIC(LogLineMeshComponent, Log, All);

ULineMeshComponent::ULineMeshComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void ULineMeshComponent::CreateLine(int32 SectionIndex, const TArray<FVector>& InVertices, const FLinearColor& Color, float Thickness)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateMeshSection);

    TArray<FVector3f> Vertices(InVertices);

    int32 IndexOffset = 0;

    TSharedPtr<FLineMeshSection> NewSection(MakeShareable(new FLineMeshSection));
    {
        const int32 NumVerts = (Vertices.Num() - 1) * 6;

        NewSection->ProcVertexBuffer.Reset();
        NewSection->ProcVertexBuffer.Reserve(NumVerts);
        NewSection->ProcIndexBuffer.Reset();
        NewSection->ProcIndexBuffer.Reserve(NumVerts * 3);
    }

    for (int32 Ind = 0; Ind < Vertices.Num() - 1; ++Ind)
    {
        const FVector3f& StartPoint = Vertices[Ind];
        const FVector3f& EndPoint = Vertices[Ind+1];

        FVector3f Direction = (EndPoint - StartPoint).GetSafeNormal();
        float Length = (EndPoint - StartPoint).Size();

        FVector3f Tangent = FVector3f::CrossProduct(Direction, FVector3f::UpVector).GetSafeNormal();
        FVector3f Normal = FVector3f::CrossProduct(Direction, Tangent).GetSafeNormal();

        FVector3f StartVertex = StartPoint - Normal * Thickness / 2;
        FVector3f EndVertex = EndPoint + Normal * Thickness / 2;

        FVector3f Corner1 = StartVertex + Tangent * Thickness / 2;
        FVector3f Corner2 = StartVertex - Tangent * Thickness / 2;
        FVector3f Corner3 = EndVertex + Tangent * Thickness / 2;
        FVector3f Corner4 = EndVertex - Tangent * Thickness / 2;

        TArray<FVector3f> SegmentVertices{
            Corner1, StartVertex, Corner2, // Corner vertices
            Corner3, EndVertex, Corner4    // Start and end vertices
        };

        // Define the index buffer for the line with thickness
        TArray<int32> SegmentIndices{
            IndexOffset, IndexOffset + 3, IndexOffset + 1,
            IndexOffset + 1, IndexOffset + 3, IndexOffset + 4,
            IndexOffset + 1, IndexOffset + 4, IndexOffset + 2,
            IndexOffset + 2, IndexOffset + 4, IndexOffset + 5,
        };

        IndexOffset += 3;

        NewSection->ProcVertexBuffer.Append(SegmentVertices);
        NewSection->ProcIndexBuffer.Append(SegmentIndices);
    }

    NewSection->MaxVertexIndex = Vertices.Num() * 3 - 1;

	NewSection->SectionIndex = SectionIndex;
    NewSection->SectionLocalBox = FBox3f(Vertices);
    NewSection->SectionLocalBox = NewSection->SectionLocalBox.ExpandBy(Thickness);
    NewSection->Color = CreateOrUpdateSectionColor(SectionIndex, Color);

    // Enqueue command to send to render thread
    FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
    ProcMeshSceneProxy->AddNewSection_GameThread(NewSection);
}

void ULineMeshComponent::UpdateLine(int32 SectionIndex, const TArray<FVector>& InVertices, const FLinearColor& Color)
{
    TArray<FVector3f> Vertices(InVertices);

    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionGT);
    FLineMeshSceneProxy* LineMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;

    if (SectionIndex >= LineMeshSceneProxy->GetNumSections())
    {
        return;
    }

    // Recreate line if mismatch in number of vertices
    if (Vertices.Num() != LineMeshSceneProxy->GetNumPointsInSection(SectionIndex))
    {
        CreateLine(SectionIndex, InVertices, Color, 15.0);
        return;
    }

    TSharedPtr<FLineMeshSectionUpdateData> SectionData(MakeShareable(new FLineMeshSectionUpdateData));
    SectionData->SectionIndex = SectionIndex;
    SectionData->SectionLocalBox = FBox3f(Vertices);
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

    SectionData->Color = CreateOrUpdateSectionColor(SectionIndex, Color);

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
