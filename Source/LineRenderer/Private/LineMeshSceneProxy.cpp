// Copyright Peter Leontev

#include "LineMeshSceneProxy.h"
#include "MaterialShared.h"
#include "LineMeshComponent.h"
#include "LineMeshSection.h"

/** Class representing a single section of the proc mesh */
class FLineMeshProxySection
{
public:
    virtual ~FLineMeshProxySection()
    {
        check (IsInRenderingThread());

        VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
        VertexBuffers.PositionVertexBuffer.ReleaseResource();
        VertexBuffers.ColorVertexBuffer.ReleaseResource();
        IndexBuffer.ReleaseResource();
        VertexFactory.ReleaseResource();
    }

public:
    /** Vertex buffer for this section */
    FStaticMeshVertexBuffers VertexBuffers;
    /** Index buffer for this section */
    FRawStaticIndexBuffer IndexBuffer;
    /** Vertex factory for this section */
    FLocalVertexFactory VertexFactory;
    /** Whether this section is currently visible */
    bool bSectionVisible;
    /** Section bounding box */
    FBox3f SectionLocalBox;
    /** Whether this section is initialized i.e. render resources created */
    bool bInitialized;
    /** Max vertex index */
    int32 MaxVertexIndex;
    /** Section index */
    int32 SectionIndex;
    /** Section Color */
    FLinearColor Color;

    FLineMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
        : VertexFactory(InFeatureLevel, "FLineMeshProxySection")
        , bSectionVisible(true)
        , bInitialized(false)
    {}
};


FLineMeshSceneProxy::FLineMeshSceneProxy(ULineMeshComponent* InComponent)
: FPrimitiveSceneProxy(InComponent), Component(InComponent), MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
{
    
}

FLineMeshSceneProxy::~FLineMeshSceneProxy()
{
    check (IsInRenderingThread());
    
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

    // Iterate over sections
    for (const TTuple<int32, TSharedPtr<FLineMeshProxySection>>& KeyValueIter : Sections)
    {
        TSharedPtr<FLineMeshProxySection> Section = KeyValueIter.Value;

        if (Section.IsValid() && Section->bInitialized && Section->bSectionVisible)
        {
            // FMaterialRenderProxy* MaterialProxy = Section->Material->GetRenderProxy();

            FMaterialRenderProxy* const MaterialProxy = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial->GetRenderProxy(), Section->Color);
            Collector.RegisterOneFrameMaterialProxy(MaterialProxy);

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
                    Mesh.ReverseCulling = !IsLocalToWorldDeterminantNegative();
                    Mesh.bDisableBackfaceCulling = true;
                    Mesh.Type = PT_TriangleList;
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
                    BatchElement.NumPrimitives = Section->IndexBuffer.GetNumIndices() / 3;
                    BatchElement.MinVertexIndex = 0;
                    BatchElement.MaxVertexIndex = Section->MaxVertexIndex;

#if ENABLE_DRAW_DEBUG
                    BatchElement.VisualizeElementIndex = Section->SectionIndex;
#endif
                   
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

void FLineMeshSceneProxy::AddNewSection_GameThread(TSharedPtr<FLineMeshSection> SrcSection)
{
    check(IsInGameThread());

    const int32 SrcSectionIndex = SrcSection->SectionIndex;

    // Copy data from vertex buffer
    const int32 NumVerts = SrcSection->ProcVertexBuffer.Num();

    TSharedPtr<FLineMeshProxySection> NewSection(MakeShareable(new FLineMeshProxySection(GetScene().GetFeatureLevel())));
    {
        NewSection->VertexBuffers.StaticMeshVertexBuffer.Init(NumVerts, 2, true);

        TArray<FVector3f> InVertexBuffer(MoveTemp(SrcSection->ProcVertexBuffer));
        NewSection->VertexBuffers.PositionVertexBuffer.Init(InVertexBuffer, true);
        NewSection->IndexBuffer.SetIndices(SrcSection->ProcIndexBuffer, EIndexBufferStride::Force16Bit);

        // Enqueue initialization of render resource
        BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);
        BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
        BeginInitResource(&NewSection->IndexBuffer);

        // Copy visibility info
        NewSection->bSectionVisible = SrcSection->bSectionVisible;
        NewSection->SectionLocalBox = SrcSection->SectionLocalBox;

        NewSection->MaxVertexIndex = SrcSection->MaxVertexIndex;
        NewSection->SectionIndex = SrcSectionIndex;

        NewSection->Color = SrcSection->Color;
    }

    ENQUEUE_RENDER_COMMAND(LineMeshVertexBuffersInit)(
        [this, SrcSectionIndex, NewSection](FRHICommandListImmediate& RHICmdList)
        {
            FLocalVertexFactory::FDataType Data;

            NewSection->VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&NewSection->VertexFactory, Data);
            NewSection->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&NewSection->VertexFactory, Data);
            NewSection->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&NewSection->VertexFactory, Data);
            NewSection->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&NewSection->VertexFactory, Data, 1);

            Data.LODLightmapDataIndex = 0;

            NewSection->VertexFactory.SetData(Data);
            NewSection->VertexFactory.InitResource();

#if WITH_EDITOR
            TArray<UMaterialInterface*> UsedMaterials;
            Component->GetUsedMaterials(UsedMaterials);

            SetUsedMaterialForVerification(UsedMaterials);
#endif

            Sections.Add(SrcSectionIndex, NewSection);

            AsyncTask(
                ENamedThreads::GameThread, 
                [this]()
                {
                    Component->UpdateLocalBounds();
                }
            );

            NewSection->bInitialized = true;
        }
    );
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
            void* DstBuffer = RHILockBuffer(SrcBuffer.VertexBufferRHI, 0, SrcBuffer.GetNumVertices() * SrcBuffer.GetStride(), RLM_WriteOnly);
            FMemory::Memcpy(DstBuffer, SrcBuffer.GetVertexData(), SrcBuffer.GetNumVertices() * SrcBuffer.GetStride());
            RHIUnlockBuffer(SrcBuffer.VertexBufferRHI);
        }

        // update index buffer
        {
            FRawStaticIndexBuffer& SrcBuffer = Section->IndexBuffer;
            void* DstBuffer = RHILockBuffer(SrcBuffer.IndexBufferRHI, 0, SrcBuffer.GetIndexDataSize(), RLM_WriteOnly);
            FMemory::Memcpy(DstBuffer, (uint8*)SrcBuffer.AccessStream16(), SrcBuffer.GetIndexDataSize());
            RHIUnlockBuffer(SrcBuffer.IndexBufferRHI);
        }

        Section->SectionLocalBox = SectionData->SectionLocalBox;
    }

    AsyncTask(
        ENamedThreads::GameThread,
        [this]()
        {
            Component->UpdateLocalBounds();
        }
    );
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

            Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
            Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
            Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
            Section->IndexBuffer.ReleaseResource();
            Section->VertexFactory.ReleaseResource();

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
    FBox3f LocalBox(ForceInit);

    for (const TTuple<int32, TSharedPtr<FLineMeshProxySection>>& KeyValueIter : Sections)
    {
        TSharedPtr<FLineMeshProxySection> Section = KeyValueIter.Value;
        LocalBox += Section->SectionLocalBox;
    }

    ensure (LocalBox.IsValid);
    LocalBounds = FBoxSphereBounds3f(LocalBox);
}

FBoxSphereBounds FLineMeshSceneProxy::GetLocalBounds() const
{
    return FBoxSphereBounds(LocalBounds);
}
