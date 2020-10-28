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
    /** Section bounding box */
    FBox SectionLocalBox;

    FLineMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
        : Material(NULL)
        , VertexFactory(InFeatureLevel, "FLineMeshProxySection")
        , bSectionVisible(true)
    {}
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
        if (Section.IsValid())
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
        // release resources when overriding existing section 
        if (Sections.Contains(SrcSection->SectionIndex))
        {
            const_cast<FLineMeshSceneProxy*>(this)->ClearMeshSection(SrcSection->SectionIndex);
        }

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

            // Copy visibility info
            NewSection->bSectionVisible = SrcSection->bSectionVisible;
            NewSection->SectionLocalBox = SrcSection->SectionLocalBox;

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

#if WITH_EDITOR
    TArray<UMaterialInterface*> UsedMaterials;
    for (TTuple<int32, UMaterialInstanceDynamic*> KeyValueIter : Component->SectionMaterials)
    {
        UsedMaterials.Add(KeyValueIter.Value);
    }

    const_cast<FLineMeshSceneProxy*>(this)->SetUsedMaterialForVerification(UsedMaterials);
#endif

    // Iterate over sections
    for (TTuple<int32, TSharedPtr<FLineMeshProxySection>> KeyValueIter : Sections)
    {
        TSharedPtr<FLineMeshProxySection> Section = KeyValueIter.Value;

        if (Section.IsValid() && Section->bSectionVisible)
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
                    Mesh.VertexFactory = &Section->VertexFactory;
                    Mesh.MaterialRenderProxy = MaterialProxy;
                    Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
                    Mesh.Type = PT_LineList;
                    Mesh.DepthPriorityGroup = SDPG_World;
                    Mesh.bCanApplyViewModeOverrides = false;

                    FMeshBatchElement& BatchElement = Mesh.Elements[0];
                    BatchElement.IndexBuffer = &Section->IndexBuffer;

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

void FLineMeshSceneProxy::UpdateSection_RenderThread(TSharedPtr<FLineMeshSectionUpdateData> SectionData)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionRT);

    check (IsInRenderingThread());
    check (SectionData.IsValid());

    if (!Sections.Contains(SectionData->SectionIndex))
    {
        return;
    }

    // Check it references a valid section
    if (Sections.Contains(SectionData->SectionIndex))
    {
        TSharedPtr<FLineMeshProxySection> Section = Sections[SectionData->SectionIndex];

        // Lock vertex buffer
        const int32 NumVerts = SectionData->VertexBuffer.Num();

        // Iterate through vertex data, copying in new info
        for (int32 i = 0; i < NumVerts; i++)
        {
            Section->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = SectionData->VertexBuffer[i];
        }

        Section->IndexBuffer.SetIndices(SectionData->IndexBuffer, EIndexBufferStride::Force16Bit);

        // update vertex buffer
        {
            FPositionVertexBuffer& SrcBuffer = Section->VertexBuffers.PositionVertexBuffer;
            void* DstBuffer = RHILockVertexBuffer(SrcBuffer.VertexBufferRHI, 0, SrcBuffer.GetNumVertices() * SrcBuffer.GetStride(), RLM_WriteOnly);
            FMemory::Memcpy(DstBuffer, SrcBuffer.GetVertexData(), SrcBuffer.GetNumVertices() * SrcBuffer.GetStride());
            RHIUnlockVertexBuffer(SrcBuffer.VertexBufferRHI);
        }

        // update index buffer
        {
            FRawStaticIndexBuffer& SrcBuffer = Section->IndexBuffer;
            void* DstBuffer = RHILockIndexBuffer(SrcBuffer.IndexBufferRHI, 0, SrcBuffer.GetIndexDataSize(), RLM_WriteOnly);
            FMemory::Memcpy(DstBuffer, (uint8*)SrcBuffer.AccessStream16(), SrcBuffer.GetIndexDataSize());
            RHIUnlockIndexBuffer(SrcBuffer.IndexBufferRHI);
        }

        Section->SectionLocalBox = SectionData->SectionLocalBox;
    }
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

int32 FLineMeshSceneProxy::GetNumSections() const
{
    return Sections.Num();
}

int32 FLineMeshSceneProxy::GetNumPointsInSection(int32 SectionIndex) const
{
    if (Sections.Contains(SectionIndex))
    {
        return Sections[SectionIndex]->VertexBuffers.PositionVertexBuffer.GetNumVertices();
    }

    return 0;
}

void FLineMeshSceneProxy::ClearMeshSection(int32 SectionIndex)
{
    ENQUEUE_RENDER_COMMAND(ReleaseSectionResources)(
        [this, SectionIndex](FRHICommandListImmediate&)
        {
            TSharedPtr<FLineMeshProxySection> Section = Sections[SectionIndex];

            BeginReleaseResource(&Section->VertexBuffers.StaticMeshVertexBuffer);
            BeginReleaseResource(&Section->VertexBuffers.PositionVertexBuffer);
            BeginReleaseResource(&Section->VertexBuffers.ColorVertexBuffer);
            BeginReleaseResource(&Section->IndexBuffer);
            BeginReleaseResource(&Section->VertexFactory);

            Sections.Remove(SectionIndex);
        }
    );
    
}

void FLineMeshSceneProxy::ClearAllMeshSections()
{
    TArray<int32> SectionIndices;
    Sections.GetKeys(SectionIndices);

    for (int32 SectionIndex : SectionIndices)
    {
        ClearMeshSection(SectionIndex);
    }
}

void FLineMeshSceneProxy::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
    ENQUEUE_RENDER_COMMAND(SetMeshSectionVisibility)(
        [this, SectionIndex, bNewVisibility](FRHICommandListImmediate&)
        {
            if (Sections.Contains(SectionIndex))
            {
                Sections[SectionIndex]->bSectionVisible = bNewVisibility;
            }
        }
    );
}

bool FLineMeshSceneProxy::IsMeshSectionVisible(int32 SectionIndex) const
{
    return Sections.Contains(SectionIndex) && Sections[SectionIndex]->bSectionVisible;
}

void FLineMeshSceneProxy::UpdateLocalBounds()
{
    FBox LocalBox(ForceInit);

    for (TTuple<int32, TSharedPtr<FLineMeshProxySection>> KeyValueIter : Sections)
    {
        TSharedPtr<FLineMeshProxySection> Section = KeyValueIter.Value;
        LocalBox += Section->SectionLocalBox;
    }

    LocalBounds = LocalBox.IsValid ? FBoxSphereBounds(LocalBox) : FBoxSphereBounds(FVector(0, 0, 0), FVector(0, 0, 0), 0); // fallback to reset box sphere bounds
}

FBoxSphereBounds FLineMeshSceneProxy::GetLocalBounds() const
{
    return LocalBounds;
}
