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

    ~FPositionOnlyVertexData()
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
        VertexData = MakeUnique<FPositionOnlyVertexData>();
        VertexData->ResizeBuffer(NumVertices);

        Stride = VertexData->GetStride();
    }

    /** Destructor. */
    ~FDynamicPositionVertexBuffer()
    {
        if (VertexData)
        {
            VertexData.Reset();
        }
    }

    // FRenderResource interface.
    virtual void InitRHI() override
    {
        // create dynamic buffer

        FRHIResourceCreateInfo CreateInfo(TEXT("ThickLines"), VertexData->GetResourceArray());

        check (VertexData.IsValid());

        const uint32 SizeInBytes = sizeof(FVector3f) * 8 * 3;

        VertexBufferRHI = RHICreateVertexBuffer(SizeInBytes, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
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

    // Vertex data accessors.
    FORCEINLINE FVector3f& VertexPosition(int32 VertexIndex)
    {
        check(VertexIndex < NumVertices);
        return ((FPositionVertex*)(VertexData->GetDataPointer() + VertexIndex * Stride))->Position;
    }

    // Vertex data accessors.
    FORCEINLINE const FVector3f& VertexPosition(int32 VertexIndex) const 
    {
        check(VertexIndex < NumVertices);
        return ((FPositionVertex*)(VertexData->GetDataPointer() + VertexIndex * Stride))->Position;
    }

    int32 GetStride() const
    {
        return Stride;
    }

private:
    int32 NumVertices;

    int32 Stride = 0;

    /** The vertex data storage type */
    TUniquePtr<class FPositionOnlyVertexData> VertexData;

    FShaderResourceViewRHIRef PositionComponentSRV;
};

/** Class representing a single section of the proc mesh */
class FLineMeshProxySection : public TSharedFromThis<FLineMeshProxySection>
{
public:
    virtual ~FLineMeshProxySection()
    {
        check (IsInRenderingThread());

        PositionVB->ReleaseResource();
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

    // Customization
    /** Material applied to this section */
    class UMaterialInterface* Material;
    /** Color applied to this section */
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

                    const float StartW = WorldToClip.TransformFVector4(Section->Lines[0].Start).W;
                    const float EndW = WorldToClip.TransformFVector4(Section->Lines[0].End).W;

                    // Negative thickness means that thickness is calculated in screen space, positive thickness should be used for world space thickness.
                    const float ScalingStart = StartW / ViewportSizeX;
                    const float ScalingEnd = EndW / ViewportSizeX;

                    const float CurrentOrthoZoomFactor = 1.0f;

                    const float StartThickness = Thickness * ScreenSpaceScaling * CurrentOrthoZoomFactor * ScalingStart;
                    const float EndThickness = Thickness * ScreenSpaceScaling * CurrentOrthoZoomFactor * ScalingEnd;

                    const FVector WorldPointXS = CameraX * StartThickness * 0.5f;
                    const FVector WorldPointYS = CameraY * StartThickness * 0.5f;

                    const FVector WorldPointXE = CameraX * EndThickness * 0.5f;
                    const FVector WorldPointYE = CameraY * EndThickness * 0.5f;

                    // Generate vertices for the point such that the post-transform point size is constant.
                    const FVector WorldPointX = CameraX * Thickness * StartW / ViewportSizeX;
                    const FVector WorldPointY = CameraY * Thickness * StartW / ViewportSizeX;

                    const int32 VertexBufferRHIBytes = Section->PositionVB->VertexBufferRHI->GetSize();

                    FBufferRHIRef VertexBufferRHI = Section->PositionVB->VertexBufferRHI;
                    FVector3f* ThickVertices = (FVector3f*)RHILockBuffer(VertexBufferRHI, 0, VertexBufferRHIBytes, RLM_WriteOnly);

                    check(ThickVertices);

                    for (const FBatchedLine& Line : Section->Lines)
                    {
                        // Begin point
                        ThickVertices[0] = FVector3f(Line.Start + WorldPointXS - WorldPointYS); // 0S
                        ThickVertices[1] = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[2] = FVector3f(Line.Start - WorldPointXS - WorldPointYS); // 2S

                        ThickVertices[3] = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[4] = FVector3f(Line.Start - WorldPointXS - WorldPointYS); // 2S
                        ThickVertices[5] = FVector3f(Line.Start - WorldPointXS + WorldPointYS); // 3S

                        // Ending point
                        ThickVertices[0 + 6] = FVector3f(Line.End + WorldPointXE - WorldPointYE); // 0E
                        ThickVertices[1 + 6] = FVector3f(Line.End + WorldPointXE + WorldPointYE); // 1E
                        ThickVertices[2 + 6] = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E

                        ThickVertices[3 + 6] = FVector3f(Line.End + WorldPointXE + WorldPointYE); // 1E
                        ThickVertices[4 + 6] = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E
                        ThickVertices[5 + 6] = FVector3f(Line.End - WorldPointXE + WorldPointYE); // 3E

                        // First part of line
                        ThickVertices[0 + 12] = FVector3f(Line.Start - WorldPointXS - WorldPointYS); // 2S
                        ThickVertices[1 + 12] = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[2 + 12] = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E

                        ThickVertices[3 + 12] = FVector3f(Line.Start + WorldPointXS + WorldPointYS); // 1S
                        ThickVertices[4 + 12] = FVector3f(Line.End + WorldPointXE + WorldPointYE); // 1E
                        ThickVertices[5 + 12] = FVector3f(Line.End - WorldPointXE - WorldPointYE); // 2E

                        // Second part of line
                        ThickVertices[0 + 18] = FVector3f(Line.Start - WorldPointXS + WorldPointYS); // 3S
                        ThickVertices[1 + 18] = FVector3f(Line.Start + WorldPointXS - WorldPointYS); // 0S
                        ThickVertices[2 + 18] = FVector3f(Line.End - WorldPointXE + WorldPointYE); // 3E

                        ThickVertices[3 + 18] = FVector3f(Line.Start + WorldPointXS - WorldPointYS); // 0S
                        ThickVertices[4 + 18] = FVector3f(Line.End + WorldPointXE - WorldPointYE); // 0E
                        ThickVertices[5 + 18] = FVector3f(Line.End - WorldPointXE + WorldPointYE); // 3E

                        ThickVertices += 24;
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
        NewSection->Material = SrcSection->Material;
        NewSection->Color = SrcSection->Color;
        
        NewSection->SectionLocalBox = FBox3f(EForceInit::ForceInitToZero);

        NewSection->VertexBuffers.StaticMeshVertexBuffer.Init(NumVerts, 1, true);
        NewSection->PositionVB = new FDynamicPositionVertexBuffer(NumVerts);

        for (const FBatchedLine& Line : NewSection->Lines)
        {
            NewSection->SectionLocalBox += FVector3f(Line.Start);
            NewSection->SectionLocalBox += FVector3f(Line.End);

            /*
            FStaticMeshVertexBuffer& StaticMeshVB = NewSection->VertexBuffers.StaticMeshVertexBuffer;
            
            // Begin point
            StaticMeshVB.SetVertexUV(0, 0, FVector2f(1, 0));
            StaticMeshVB.SetVertexUV(1, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(2, 0, FVector2f(0, 0));

            StaticMeshVB.SetVertexUV(3, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(4, 0, FVector2f(0, 0));
            StaticMeshVB.SetVertexUV(5, 0, FVector2f(0, 1));

            // Ending point
            StaticMeshVB.SetVertexUV(6, 0, FVector2f(1, 0));
            StaticMeshVB.SetVertexUV(7, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(8, 0, FVector2f(0, 0));

            StaticMeshVB.SetVertexUV(9, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(10, 0, FVector2f(0, 0));
            StaticMeshVB.SetVertexUV(11, 0, FVector2f(0, 1));
            
            // First part of line
            StaticMeshVB.SetVertexUV(12, 0, FVector2f(0, 0));
            StaticMeshVB.SetVertexUV(13, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(14, 0, FVector2f(0, 0));

            StaticMeshVB.SetVertexUV(15, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(16, 0, FVector2f(1, 1));
            StaticMeshVB.SetVertexUV(17, 0, FVector2f(0, 0));

            // Second part of line
            StaticMeshVB.SetVertexUV(18, 0, FVector2f(0, 1));
            StaticMeshVB.SetVertexUV(19, 0, FVector2f(1, 0));
            StaticMeshVB.SetVertexUV(20, 0, FVector2f(0, 1));

            StaticMeshVB.SetVertexUV(21, 0, FVector2f(1, 0));
            StaticMeshVB.SetVertexUV(22, 0, FVector2f(1, 0));
            StaticMeshVB.SetVertexUV(23, 0, FVector2f(0, 1));
            */
        }

        TArray<uint32> IndexBuffer;

        int32 IndexOffset = 0;
        for (int32 LineIndex = 0; LineIndex < NewSection->Lines.Num(); ++LineIndex)
        {
            // First rectangle
            IndexBuffer.Add(IndexOffset + 0);
            IndexBuffer.Add(IndexOffset + 1);
            IndexBuffer.Add(IndexOffset + 2);

            IndexBuffer.Add(IndexOffset + 3);
            IndexBuffer.Add(IndexOffset + 4);
            IndexBuffer.Add(IndexOffset + 5);

            // Second rectangle
            IndexBuffer.Add(IndexOffset + 6);
            IndexBuffer.Add(IndexOffset + 7);
            IndexBuffer.Add(IndexOffset + 8);

            IndexBuffer.Add(IndexOffset + 9);
            IndexBuffer.Add(IndexOffset + 10);
            IndexBuffer.Add(IndexOffset + 11);

            // Third rectangle
            IndexBuffer.Add(IndexOffset + 12);
            IndexBuffer.Add(IndexOffset + 13);
            IndexBuffer.Add(IndexOffset + 14);

            IndexBuffer.Add(IndexOffset + 15);
            IndexBuffer.Add(IndexOffset + 16);
            IndexBuffer.Add(IndexOffset + 17);

            // Fourth rectangle
            IndexBuffer.Add(IndexOffset + 18);
            IndexBuffer.Add(IndexOffset + 19);
            IndexBuffer.Add(IndexOffset + 20);

            IndexBuffer.Add(IndexOffset + 21);
            IndexBuffer.Add(IndexOffset + 22);
            IndexBuffer.Add(IndexOffset + 23);

            // Increment the IndexOffset for the next line
            IndexOffset += 24;
        }

        NewSection->IndexBuffer.SetIndices(IndexBuffer, EIndexBufferStride::Force16Bit);

        // Enqueue initialization of render resource
        BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
        BeginInitResource(NewSection->PositionVB);
        BeginInitResource(&NewSection->IndexBuffer);
    }

    TSharedRef<FLineMeshProxySection> SectionRef = StaticCastSharedRef<FLineMeshProxySection>(NewSection.ToSharedRef());

    ENQUEUE_RENDER_COMMAND(LineMeshVertexBuffersInit)(
        [this, SrcSectionIndex, SectionRef](FRHICommandListImmediate& RHICmdList)
        {
            FLocalVertexFactory::FDataType Data;

            SectionRef->PositionVB->BindPositionVertexBuffer(&SectionRef->VertexFactory, Data);

            SectionRef->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&SectionRef->VertexFactory, Data);
            SectionRef->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&SectionRef->VertexFactory, Data);
            SectionRef->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&SectionRef->VertexFactory, Data, 1);

            Data.LODLightmapDataIndex = 0;

            SectionRef->VertexFactory.SetData(Data);
            SectionRef->VertexFactory.InitResource();

#if WITH_EDITOR
            TArray<UMaterialInterface*> UsedMaterials;
            Component->GetUsedMaterials(UsedMaterials);

            SetUsedMaterialForVerification(UsedMaterials);
#endif

            Sections.Add(SrcSectionIndex, SectionRef);

            AsyncTask(
                ENamedThreads::GameThread, 
                [this]()
                {
                    Component->UpdateLocalBounds();
                }
            );

            SectionRef->bInitialized = true;
        }
    );
}

void FLineMeshSceneProxy::UpdateSection_RenderThread(TSharedPtr<FLineMeshSectionUpdateData> SectionData)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionRT);

    // FIXME:

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