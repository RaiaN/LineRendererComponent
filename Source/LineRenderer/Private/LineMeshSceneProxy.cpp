// Copyright Peter Leontev

#include "LineMeshSceneProxy.h"
#include "LineMeshComponent.h"

/** Class representing a single section of the proc mesh */
class FLineMeshProxySection
{
public:
    /** Material applied to this section */
    class UMaterialInterface* Material;
    /** Vertex buffer for this section */
    FStaticMeshVertexBuffers VertexBuffers;
    /** Index buffer for this section */
    FRawStaticIndexBuffer IndexBuffer;
    /** Vertex factory for this section */
    FLocalVertexFactory VertexFactory;
    /** Whether this section is currently visible */
    bool bSectionVisible;

    FLineMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
        : Material(NULL)
        , VertexFactory(InFeatureLevel, "FLineMeshProxySection")
        , bSectionVisible(true)
    {}
};

/**
 *	Struct used to send update to mesh data
 *	Arrays may be empty, in which case no update is performed.
 */
class FLineMeshSectionUpdateData
{
public:
    /** Section to update */
    int32 TargetSection;
    /** New vertex information */
    TArray<FVector> NewVertexBuffer;
};


FLineMeshSceneProxy::FLineMeshSceneProxy(ULineMeshComponent* InComponent)
: FPrimitiveSceneProxy(InComponent), Component(InComponent), MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
{

}

FLineMeshSceneProxy::~FLineMeshSceneProxy()
{
    for (TTuple<int32, TSharedPtr<FLineMeshProxySection>> KeyValueIter : Sections)
    {
        TSharedPtr<FLineMeshProxySection> Section = KeyValueIter.Value;

        if (Section != nullptr)
        {
            Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
            Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
            Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
            Section->IndexBuffer.ReleaseResource();
            Section->VertexFactory.ReleaseResource();
        }
    }
}

SIZE_T FLineMeshSceneProxy::GetTypeHash() const
{
    static size_t UniquePointer;
    return reinterpret_cast<size_t>(&UniquePointer);
}

void FLineMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_GetMeshElements);

    TSharedPtr<FLineMeshSection> SrcSection;
    while (Component->PendingSections.Dequeue(SrcSection))
    {
        // Copy data from vertex buffer
        const int32 NumVerts = SrcSection->ProcVertexBuffer.Num();

        TSharedPtr<FLineMeshProxySection> NewSection(MakeShareable(new FLineMeshProxySection(GetScene().GetFeatureLevel())));
        {
            NewSection->VertexBuffers.StaticMeshVertexBuffer.Init(NumVerts, 2, true);
            NewSection->VertexBuffers.PositionVertexBuffer.Init(SrcSection->ProcVertexBuffer, true);
            NewSection->IndexBuffer.SetIndices(SrcSection->ProcIndexBuffer, EIndexBufferStride::Force16Bit);

            // Enqueue initialization of render resource
            BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);
            BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
            BeginInitResource(&NewSection->IndexBuffer);

            // Grab material
            NewSection->Material = Component->GetMaterial(SrcSection->SectionIndex);
            if (NewSection->Material == NULL)
            {
                NewSection->Material = UMaterial::GetDefaultMaterial(MD_Surface);
            }

            // Copy visibility info
            NewSection->bSectionVisible = SrcSection->bSectionVisible;

            Sections.Add(SrcSection->SectionIndex, NewSection);
        }

        ENQUEUE_RENDER_COMMAND(LineMeshVertexBuffersInit)(
            [NewSection](FRHICommandListImmediate& RHICmdList)
            {
                FLocalVertexFactory::FDataType Data;

                NewSection->VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&NewSection->VertexFactory, Data);
                NewSection->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&NewSection->VertexFactory, Data);
                NewSection->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&NewSection->VertexFactory, Data);
                NewSection->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&NewSection->VertexFactory, Data, 1);
                // NewSection->VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(NewSection->VertexFactory, Data);

                Data.LODLightmapDataIndex = 0;

                NewSection->VertexFactory.SetData(Data);
                NewSection->VertexFactory.InitResource();
            }
        );
    }

    // Iterate over sections
    for (TTuple<int32, TSharedPtr<FLineMeshProxySection>> KeyValueIter : Sections)
    {
        TSharedPtr<FLineMeshProxySection> Section = KeyValueIter.Value;

        if (Section != nullptr && Section->bSectionVisible)
        {
            FMaterialRenderProxy* MaterialProxy = Section->Material->GetRenderProxy();

            // For each view..
            for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
            {
                if (VisibilityMap & (1 << ViewIndex))
                {
                    const FSceneView* View = Views[ViewIndex];
                    // Draw the mesh.
                    FMeshBatch& Mesh = Collector.AllocateMesh();
                    FMeshBatchElement& BatchElement = Mesh.Elements[0];
                    BatchElement.IndexBuffer = &Section->IndexBuffer;
                    Mesh.VertexFactory = &Section->VertexFactory;
                    Mesh.MaterialRenderProxy = MaterialProxy;

                    bool bHasPrecomputedVolumetricLightmap;
                    FMatrix PreviousLocalToWorld;
                    int32 SingleCaptureIndex;
                    bool bOutputVelocity;
                    GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

                    FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
                    DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);
                    BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

                    BatchElement.FirstIndex = 0;
                    BatchElement.NumPrimitives = Section->IndexBuffer.GetNumIndices() / 2;
                    BatchElement.MinVertexIndex = 0;
                    BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
                    Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
                    Mesh.Type = PT_LineList;
                    Mesh.DepthPriorityGroup = SDPG_World;
                    Mesh.bCanApplyViewModeOverrides = false;
                    Collector.AddMesh(ViewIndex, Mesh);
                }
            }
        }
    }
}

FPrimitiveViewRelevance FLineMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
{
    FPrimitiveViewRelevance Result;
    Result.bDrawRelevance = IsShown(View);
    Result.bShadowRelevance = IsShadowCast(View);
    Result.bDynamicRelevance = true;
    Result.bRenderInMainPass = ShouldRenderInMainPass();
    Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
    Result.bRenderCustomDepth = ShouldRenderCustomDepth();
    Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
    MaterialRelevance.SetPrimitiveViewRelevance(Result);
    Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;
    return Result;
}

void FLineMeshSceneProxy::UpdateSection_RenderThread(FLineMeshSectionUpdateData* SectionData)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionRT);

    check(IsInRenderingThread());

    // Check we have data 
    /*if(	SectionData != nullptr)
    {
        // Check it references a valid section
        if (SectionData->TargetSection < Sections.Num() &&
            Sections[SectionData->TargetSection] != nullptr)
        {
            FLineMeshProxySection* Section = Sections[SectionData->TargetSection];

            // Lock vertex buffer
            const int32 NumVerts = SectionData->NewVertexBuffer.Num();

            // Iterate through vertex data, copying in new info
            for(int32 i=0; i<NumVerts; i++)
            {
                const FVector& ProcVert = SectionData->NewVertexBuffer[i];
                FDynamicMeshVertex Vertex;
                ConvertProcMeshToDynMeshVertex(Vertex, ProcVert);

                Section->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
                Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector());
                Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TextureCoordinate[0]);
                Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 1, Vertex.TextureCoordinate[1]);
                Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 2, Vertex.TextureCoordinate[2]);
                Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 3, Vertex.TextureCoordinate[3]);
                Section->VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
            }

            {
                auto& VertexBuffer = Section->VertexBuffers.PositionVertexBuffer;
                void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
                FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
                RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
            }

            {
                auto& VertexBuffer = Section->VertexBuffers.ColorVertexBuffer;
                void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
                FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
                RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
            }

            {
                auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
                void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTangentSize(), RLM_WriteOnly);
                FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), VertexBuffer.GetTangentSize());
                RHIUnlockVertexBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
            }

            {
                auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
                void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTexCoordSize(), RLM_WriteOnly);
                FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), VertexBuffer.GetTexCoordSize());
                RHIUnlockVertexBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
            }
        }

        // Free data sent from game thread
        delete SectionData;
    }*/
}

void FLineMeshSceneProxy::SetSectionVisibility_RenderThread(int32 SectionIndex, bool bNewVisibility)
{
    /*check(IsInRenderingThread());

    if(	SectionIndex < Sections.Num() &&
        Sections[SectionIndex] != nullptr)
    {
        Sections[SectionIndex]->bSectionVisible = bNewVisibility;
    }*/
}

bool FLineMeshSceneProxy::CanBeOccluded() const
{
    return !MaterialRelevance.bDisableDepthTest;
}

uint32 FLineMeshSceneProxy::GetMemoryFootprint() const
{
    return sizeof(*this) + GetAllocatedSize();
}

uint32 FLineMeshSceneProxy::GetAllocatedSize() const
{
    return FPrimitiveSceneProxy::GetAllocatedSize();
}