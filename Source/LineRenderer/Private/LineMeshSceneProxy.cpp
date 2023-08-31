// Copyright Peter Leontev

#include "LineMeshSceneProxy.h"
#include "MaterialShared.h"
#include "Rendering/PositionVertexBuffer.h"
#include "LocalVertexFactory.h"
#include "LineMeshComponent.h"
#include "LineMeshSection.h"
#include "RHICommandList.h"

PRAGMA_DISABLE_OPTIMIZATION

class FPositionOnlyVertexData :
    public TStaticMeshVertexData<FPositionVertex>
{
public:
    FPositionOnlyVertexData()
        : TStaticMeshVertexData<FPositionVertex>(false)
    {}
};

/** A vertex buffer for lines. */
class FDynamicPositionVertexBuffer : public FVertexBuffer
{
public:

    /** Default constructor. */
    FDynamicPositionVertexBuffer()
        : NumVertices(0)
    {}

    FDynamicPositionVertexBuffer(int32 InNumVertices)
        : NumVertices(InNumVertices)
    {
        VertexData = new FPositionOnlyVertexData();
        VertexData->ResizeBuffer(NumVertices);

        Stride = VertexData->GetStride();
    }

    /** Destructor. */
    ~FDynamicPositionVertexBuffer()
    {
        if (VertexData)
        {
            delete VertexData;
            VertexData = nullptr;
        }
    }

    // FRenderResource interface.
    virtual void InitRHI() override
    {
        const int32 VertexBufferRHIBytes = sizeof(FVector3f) * NumVertices;

        // create dynamic buffer

        FRHIResourceCreateInfo CreateInfo(TEXT("ThickLines"));
        CreateInfo.ResourceArray = VertexData->GetResourceArray();

        VertexBufferRHI = CreateRHIBuffer<true>(VertexData, NumVertices, BUF_Dynamic | BUF_ShaderResource, TEXT("ThickLines"));
        if (VertexBufferRHI)
        {
            PositionComponentSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(VertexBufferRHI, PF_R32_FLOAT));
        }
    }

    virtual void ReleaseRHI() override
    {
        PositionComponentSRV.SafeRelease();
        FVertexBuffer::ReleaseRHI();
    }

    void BindPositionVertexBuffer(const class FVertexFactory* VertexFactory, struct FStaticMeshDataType& StaticMeshData) const
    {
        StaticMeshData.PositionComponent = FVertexStreamComponent(
            this,
            STRUCT_OFFSET(FPositionVertex, Position),
            GetStride(),
            VET_Float3
        );
        StaticMeshData.PositionComponentSRV = PositionComponentSRV;
    }

    int32 GetStride() const
    {
        return Stride;
    }

private:
    int32 NumVertices;

    int32 Stride = 0;

    /** The vertex data storage type */
    TMemoryImagePtr<class FPositionOnlyVertexData> VertexData;

    FShaderResourceViewRHIRef PositionComponentSRV;
};

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
    TArray<FBatchedLine> Lines;

    /** Vertex buffer for this section */
    FStaticMeshVertexBuffers VertexBuffers;

    /** Position only vertex buffer */
    FDynamicPositionVertexBuffer* PositionVB;

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
    /** Section thickness */
    float SectionThickness;

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

            // FMaterialRenderProxy* const MaterialProxy = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial->GetRenderProxy(), Section->Color);
            // Collector.RegisterOneFrameMaterialProxy(MaterialProxy);

            // For each view..
            for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
            {
                if (VisibilityMap & (1 << ViewIndex))
                {
                    const FSceneView* View = Views[ViewIndex];
                    // Draw the mesh.
                    FMeshBatch& Mesh = Collector.AllocateMesh();
                    Mesh.VertexFactory = &Section->VertexFactory;
                    Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
                    Mesh.ReverseCulling = !IsLocalToWorldDeterminantNegative();
                    Mesh.bDisableBackfaceCulling = true;
                    Mesh.Type = PT_TriangleList;
                    Mesh.DepthPriorityGroup = SDPG_World;
                    Mesh.bCanApplyViewModeOverrides = false;

                    const FMatrix& WorldToClip = View->ViewMatrices.GetViewProjectionMatrix();
                    const FMatrix& ClipToWorld = View->ViewMatrices.GetInvViewProjectionMatrix();
                    const uint32 ViewportSizeX = View->UnscaledViewRect.Width();
                    const uint32 ViewportSizeY = View->UnscaledViewRect.Height();

                    FVector CameraX = ClipToWorld.TransformVector(FVector(1, 0, 0)).GetSafeNormal();
                    FVector CameraY = ClipToWorld.TransformVector(FVector(0, 1, 0)).GetSafeNormal();
                    FVector CameraZ = ClipToWorld.TransformVector(FVector(0, 0, 1)).GetSafeNormal();

                    // TODO: Add this option to section!
                    const float ScreenSpaceScaling = 2.0f; // Line.bScreenSpace ? 2.0f : 1.0f;
                    const float Thickness = Section->Lines[0].Thickness;

                    float OrthoZoomFactor = 1.0f;
                    const float StartThickness = Thickness * ScreenSpaceScaling * OrthoZoomFactor;
                    const float EndThickness = Thickness * ScreenSpaceScaling * OrthoZoomFactor;

                    const FVector WorldPointXS = CameraX * StartThickness * 0.5f;
                    const FVector WorldPointYS = CameraY * StartThickness * 0.5f;

                    const FVector WorldPointXE = CameraX * EndThickness * 0.5f;
                    const FVector WorldPointYE = CameraY * EndThickness * 0.5f;

                    const int32 VertexBufferRHIBytes = sizeof(FPositionVertex) * 8 * 3 * Section->Lines.Num();

                    FRHIResourceCreateInfo CreateInfo(TEXT("ThickLines"));
                    // TODO: Make sure correct structs are used
                    // FBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(VertexBufferRHIBytes, BUF_Volatile, CreateInfo);
                    // void* ThickVertexData = RHILockBuffer(VertexBufferRHI, 0, VertexBufferRHIBytes, RLM_WriteOnly);
                    // FSimpleElementVertex* ThickVertices = (FSimpleElementVertex*)ThickVertexData;

                    FBufferRHIRef VertexBufferRHI = Section->PositionVB->VertexBufferRHI;
                    FPositionVertex* ThickVertices = (FPositionVertex*)RHILockBuffer(VertexBufferRHI, 0, VertexBufferRHIBytes, RLM_WriteOnly);

                    check(ThickVertices);

                    for (const FBatchedLine& Line : Section->Lines)
                    {
                        // Begin point
                        ThickVertices[0].Position = FVector3f(Line.Start + WorldPointXS - WorldPointYS); // 0S
                        ThickVertices[1].Position = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[2].Position = FVector3f(Line.Start - WorldPointXS - WorldPointYS); // 2S

                        ThickVertices[3].Position = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[4].Position = FVector3f(Line.Start - WorldPointXS - WorldPointYS); // 2S
                        ThickVertices[5].Position = FVector3f(Line.Start - WorldPointXS + WorldPointYS); // 3S

                        // Ending point
                        ThickVertices[0 + 6].Position = FVector3f(Line.End + WorldPointXE - WorldPointYE); // 0E
                        ThickVertices[1 + 6].Position = FVector3f(Line.End + WorldPointXE + WorldPointYE); // 1E
                        ThickVertices[2 + 6].Position = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E

                        ThickVertices[3 + 6].Position = FVector3f(Line.End + WorldPointXE + WorldPointYE); // 1E
                        ThickVertices[4 + 6].Position = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E
                        ThickVertices[5 + 6].Position = FVector3f(Line.End - WorldPointXE + WorldPointYE); // 3E

                        // First part of line
                        ThickVertices[0 + 12].Position = FVector3f(Line.Start - WorldPointXS - WorldPointYS); // 2S
                        ThickVertices[1 + 12].Position = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[2 + 12].Position = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E

                        ThickVertices[3 + 12].Position = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[4 + 12].Position = FVector3f(Line.End + WorldPointXE + WorldPointYE); // 1E
                        ThickVertices[5 + 12].Position = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E

                        // Second part of line
                        ThickVertices[0 + 18].Position = FVector3f(Line.Start - WorldPointXS + WorldPointYS); // 3S
                        ThickVertices[1 + 18].Position = FVector3f(Line.Start + WorldPointXS - WorldPointYS); // 0S
                        ThickVertices[2 + 18].Position = FVector3f(Line.End - WorldPointXE + WorldPointYE); // 3E

                        ThickVertices[3 + 18].Position = FVector3f(Line.Start + WorldPointXS - WorldPointYS); // 0S
                        ThickVertices[4 + 18].Position = FVector3f(Line.End + WorldPointXE - WorldPointYE); // 0E
                        ThickVertices[5 + 18].Position = FVector3f(Line.End - WorldPointXE + WorldPointYE); // 3E

                        ThickVertices += 24;
                        
                        /* Commented out as we only need vertices position, nothing else
                        // Begin point
                        ThickVertices[0] = FSimpleElementVertex(Line.Start + WorldPointXS - WorldPointYS, FVector2D(1, 0), Line.Color, FHitProxyId()); // 0S
                        ThickVertices[1] = FSimpleElementVertex(Line.Start + WorldPointXS + WorldPointYS, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1S
                        ThickVertices[2] = FSimpleElementVertex(Line.Start - WorldPointXS - WorldPointYS, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2S

                        ThickVertices[3] = FSimpleElementVertex(Line.Start + WorldPointXS + WorldPointYS, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1S
                        ThickVertices[4] = FSimpleElementVertex(Line.Start - WorldPointXS - WorldPointYS, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2S
                        ThickVertices[5] = FSimpleElementVertex(Line.Start - WorldPointXS + WorldPointYS, FVector2D(0, 1), Line.Color, FHitProxyId()); // 3S

                        // Ending point
                        ThickVertices[0 + 6] = FSimpleElementVertex(Line.End + WorldPointXE - WorldPointYE, FVector2D(1, 0), Line.Color, FHitProxyId()); // 0E
                        ThickVertices[1 + 6] = FSimpleElementVertex(Line.End + WorldPointXE + WorldPointYE, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1E
                        ThickVertices[2 + 6] = FSimpleElementVertex(Line.End - WorldPointXE - WorldPointYE, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2E

                        ThickVertices[3 + 6] = FSimpleElementVertex(Line.End + WorldPointXE + WorldPointYE, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1E
                        ThickVertices[4 + 6] = FSimpleElementVertex(Line.End - WorldPointXE - WorldPointYE, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2E
                        ThickVertices[5 + 6] = FSimpleElementVertex(Line.End - WorldPointXE + WorldPointYE, FVector2D(0, 1), Line.Color, FHitProxyId()); // 3E

                        // First part of line
                        ThickVertices[0 + 12] = FSimpleElementVertex(Line.Start - WorldPointXS - WorldPointYS, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2S
                        ThickVertices[1 + 12] = FSimpleElementVertex(Line.Start + WorldPointXS + WorldPointYS, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1S
                        ThickVertices[2 + 12] = FSimpleElementVertex(Line.End - WorldPointXE - WorldPointYE, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2E

                        ThickVertices[3 + 12] = FSimpleElementVertex(Line.Start + WorldPointXS + WorldPointYS, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1S
                        ThickVertices[4 + 12] = FSimpleElementVertex(Line.End + WorldPointXE + WorldPointYE, FVector2D(1, 1), Line.Color, FHitProxyId()); // 1E
                        ThickVertices[5 + 12] = FSimpleElementVertex(Line.End - WorldPointXE - WorldPointYE, FVector2D(0, 0), Line.Color, FHitProxyId()); // 2E

                        // Second part of line
                        ThickVertices[0 + 18] = FSimpleElementVertex(Line.Start - WorldPointXS + WorldPointYS, FVector2D(0, 1), Line.Color, FHitProxyId()); // 3S
                        ThickVertices[1 + 18] = FSimpleElementVertex(Line.Start + WorldPointXS - WorldPointYS, FVector2D(1, 0), Line.Color, FHitProxyId()); // 0S
                        ThickVertices[2 + 18] = FSimpleElementVertex(Line.End - WorldPointXE + WorldPointYE, FVector2D(0, 1), Line.Color, FHitProxyId()); // 3E

                        ThickVertices[3 + 18] = FSimpleElementVertex(Line.Start + WorldPointXS - WorldPointYS, FVector2D(1, 0), Line.Color, FHitProxyId()); // 0S
                        ThickVertices[4 + 18] = FSimpleElementVertex(Line.End + WorldPointXE - WorldPointYE, FVector2D(1, 0), Line.Color, FHitProxyId()); // 0E
                        ThickVertices[5 + 18] = FSimpleElementVertex(Line.End - WorldPointXE + WorldPointYE, FVector2D(0, 1), Line.Color, FHitProxyId()); // 3E

                        ThickVertices += 24;
                        */
                    }

                    RHIUnlockBuffer(VertexBufferRHI);
                    

                    FMeshBatchElement& BatchElement = Mesh.Elements[0];
                    BatchElement.IndexBuffer = &Section->IndexBuffer;

                    bool bHasPrecomputedVolumetricLightmap;
                    FMatrix PreviousLocalToWorld;
                    int32 SingleCaptureIndex;
                    bool bOutputVelocity;
                    GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

                    FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
                    DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
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

    const int32 NumVerts = SrcSection->Lines.Num() * 24;

    const int32 SrcSectionIndex = SrcSection->SectionIndex;

    TSharedPtr<FLineMeshProxySection> NewSection(MakeShareable(new FLineMeshProxySection(GetScene().GetFeatureLevel())));
    {
        NewSection->Lines = MoveTemp(SrcSection->Lines);
        NewSection->MaxVertexIndex = NumVerts - 1;
        NewSection->SectionIndex = SrcSectionIndex;

        NewSection->VertexBuffers.StaticMeshVertexBuffer.Init(NumVerts, 2, true);
        NewSection->VertexBuffers.PositionVertexBuffer.Init(NumVerts, true);
        NewSection->PositionVB = new FDynamicPositionVertexBuffer(NumVerts);

        TArray<uint32> IndexBuffer;

        int32 VertexOffset = 0;
        for (int32 LineIndex = 0; LineIndex < NewSection->Lines.Num(); ++LineIndex)
        {
            // Calculate the index offset for this line
            int32 IndexOffset = LineIndex * 24; // Each line has 24 vertices

            // First rectangle
            IndexBuffer.Add(VertexOffset + 0 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 1 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 2 + IndexOffset);

            IndexBuffer.Add(VertexOffset + 1 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 4 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 2 + IndexOffset);

            // Second rectangle
            IndexBuffer.Add(VertexOffset + 6 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 7 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 8 + IndexOffset);

            IndexBuffer.Add(VertexOffset + 7 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 10 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 8 + IndexOffset);

            // Third rectangle
            IndexBuffer.Add(VertexOffset + 12 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 13 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 14 + IndexOffset);

            IndexBuffer.Add(VertexOffset + 13 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 16 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 14 + IndexOffset);

            // Fourth rectangle
            IndexBuffer.Add(VertexOffset + 18 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 19 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 20 + IndexOffset);

            IndexBuffer.Add(VertexOffset + 19 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 22 + IndexOffset);
            IndexBuffer.Add(VertexOffset + 20 + IndexOffset);

            // Increment the vertex offset for the next line
            VertexOffset += 24;
        }

        NewSection->IndexBuffer.SetIndices(IndexBuffer, EIndexBufferStride::Force16Bit);

        // Enqueue initialization of render resource
        BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);
        BeginInitResource(NewSection->PositionVB);
        BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
        BeginInitResource(&NewSection->IndexBuffer);
    }

    ENQUEUE_RENDER_COMMAND(LineMeshVertexBuffersInit)(
        [this, SrcSectionIndex, NewSection](FRHICommandListImmediate& RHICmdList)
        {
            FLocalVertexFactory::FDataType Data;

            // NewSection->VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&NewSection->VertexFactory, Data);
            NewSection->PositionVB->BindPositionVertexBuffer(&NewSection->VertexFactory, Data);

            NewSection->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&NewSection->VertexFactory, Data);
            NewSection->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&NewSection->VertexFactory, Data);
            NewSection->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&NewSection->VertexFactory, Data, 1);

            Data.LODLightmapDataIndex = 0;

            NewSection->VertexFactory.SetData(Data);
            NewSection->VertexFactory.InitResource();

/*#if WITH_EDITOR
            TArray<UMaterialInterface*> UsedMaterials;
            Component->GetUsedMaterials(UsedMaterials);

            SetUsedMaterialForVerification(UsedMaterials);
#endif*/

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

PRAGMA_ENABLE_OPTIMIZATION