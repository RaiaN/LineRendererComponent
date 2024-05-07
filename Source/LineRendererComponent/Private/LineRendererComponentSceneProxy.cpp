// Copyright 2023 Petr Leontev. All Rights Reserved.

#include "LineRendererComponentSceneProxy.h"
#include "MaterialShared.h"
#include "Rendering/PositionVertexBuffer.h"
#include "LocalVertexFactory.h"
#include "ShaderCore.h"
#include "ShowFlags.h"
#include "SceneInterface.h"
#include "Runtime/Launch/Resources/Version.h"
#include "LineRendererComponent.h"
#include "LineSectionInfo.h"


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
#if ENGINE_MAJOR_VERSION >= 4 && ENGINE_MINOR_VERSION > 3
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
#else
    virtual void InitRHI() override
#endif
    {
        // create dynamic buffer

        // FRHIResourceCreateInfo CreateInfo(TEXT("ThickLines"), VertexData->GetResourceArray());
        FRHIResourceCreateInfo CreateInfo(TEXT("ThickLines"));

        check (VertexData.IsValid());

        const uint32 SizeInBytes = NumVertices * sizeof(FVector3f) * 8 * 3;

        check (SizeInBytes >= 0);

#if ENGINE_MAJOR_VERSION > 4 && ENGINE_MINOR_VERSION > 3
        VertexBufferRHI = RHICmdList.CreateVertexBuffer(SizeInBytes, EBufferUsageFlags::Dynamic | EBufferUsageFlags::ShaderResource, CreateInfo);
        if (VertexBufferRHI)
        {
            PositionComponentSRV = RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(VertexBufferRHI, PF_R32_FLOAT));
        }
        
#else
        VertexBufferRHI = RHICreateVertexBuffer(SizeInBytes, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
        if (VertexBufferRHI)
        {
            PositionComponentSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(VertexBufferRHI, PF_R32_FLOAT));
        }
#endif
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

    void* GetVertexData() { return VertexData->GetDataPointer(); }

    int32 GetStride() const
    {
        return Stride;
    }

    int32 GetNumVertices() const
    {
        return NumVertices;
    }

private:
    int32 NumVertices;

    int32 Stride = 0;

    /** The vertex data storage type */
    TUniquePtr<class FPositionOnlyVertexData> VertexData;

    FShaderResourceViewRHIRef PositionComponentSRV;
};

class FLineProxySection : public TSharedFromThis<FLineProxySection>
{
public:
    FLineProxySection(ERHIFeatureLevel::Type InFeatureLevel)
        : VertexFactory(InFeatureLevel, "FLineProxySection")
        , bSectionVisible(true)
        , bInitialized(false)
    {}

    virtual ~FLineProxySection()
    {
        PositionVB->ReleaseResource();
        delete PositionVB;
        IndexBuffer.ReleaseResource();

        // Using LocalVertexFactory requires to deinit all buffers
        StaticMeshVertexBuffer.ReleaseResource();
        // ColorVertexBuffer.ReleaseResource();
        
        VertexFactory.ReleaseResource();
    }

public:
    TArray<FBatchedLine> Lines;

    /** Position only vertex buffer */
    FDynamicPositionVertexBuffer* PositionVB;
    /** Index buffer for this section */
    FRawStaticIndexBuffer IndexBuffer;

    /** The buffer containing vertex data (UVs and tangents) */
    FStaticMeshVertexBuffer StaticMeshVertexBuffer;
    /** The buffer containing the vertex color data. */
    // FColorVertexBuffer ColorVertexBuffer;

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
};

FLineRendererComponentSceneProxy::FLineRendererComponentSceneProxy(ULineRendererComponent* InComponent)
: FPrimitiveSceneProxy(InComponent), Component(InComponent), MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
{
    for (const auto& SectionKeyPair : Component->Sections)
    {
        AddNewSection_GameThread(&SectionKeyPair.Value);
    }
}


FLineRendererComponentSceneProxy::~FLineRendererComponentSceneProxy()
{
    Sections_RenderThread.Empty();
}

SIZE_T FLineRendererComponentSceneProxy::GetTypeHash() const
{
    static size_t UniquePointer;
    return reinterpret_cast<size_t>(&UniquePointer);
}

void FLineRendererComponentSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_GetMeshElements);

    const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

    const bool bIsWireframeView = AllowDebugViewmodes() && EngineShowFlags.Wireframe;

    // Iterate over sections

    for (const TTuple<int32, TSharedPtr<FLineProxySection>>& KeyValueIter : Sections_RenderThread)
    {
        TSharedPtr<FLineProxySection> Section = KeyValueIter.Value;

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
                    Mesh.Type = PT_TriangleList;
                    Mesh.DepthPriorityGroup = SDPG_World;
                    Mesh.bCanApplyViewModeOverrides = false;

                    if (AllowDebugViewmodes() && bIsWireframeView)
                    {
                        Mesh.bWireframe = true;
                    }

                    const FMatrix& WorldToClip = View->ViewMatrices.GetViewProjectionMatrix();
                    const FMatrix& ClipToWorld = View->ViewMatrices.GetInvViewProjectionMatrix();
                    const uint32 ViewportSizeX = View->UnscaledViewRect.Width();
                    const uint32 ViewportSizeY = View->UnscaledViewRect.Height();

                    FVector CameraX = ClipToWorld.TransformVector(FVector(1, 0, 0)).GetSafeNormal();
                    FVector CameraY = ClipToWorld.TransformVector(FVector(0, 1, 0)).GetSafeNormal();
                    FVector CameraZ = ClipToWorld.TransformVector(FVector(0, 0, 1)).GetSafeNormal();

                    const float Thickness = Section->Lines[0].Thickness;

                    const float StartW = WorldToClip.TransformFVector4(Section->Lines[0].Start).W;
                    const float EndW = WorldToClip.TransformFVector4(Section->Lines[0].End).W;

                    // Negative thickness means that thickness is calculated in screen space, positive thickness should be used for world space thickness.
                    const float ScalingStart = 1.0f;
                    const float ScalingEnd = 1.0f;

                    const float CurrentOrthoZoomFactor = 1.0f;
                    const float ScreenSpaceScaling = 1.0f;

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

#if ENGINE_MAJOR_VERSION > 4 && ENGINE_MINOR_VERSION > 3
                    FVector3f* ThickVertices = (FVector3f*)Collector.GetRHICommandList().LockBuffer(VertexBufferRHI, 0, VertexBufferRHIBytes, RLM_WriteOnly);
#else
                    FVector3f* ThickVertices = (FVector3f*)RHILockBuffer(VertexBufferRHI, 0, VertexBufferRHIBytes, RLM_WriteOnly);
#endif

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

#if ENGINE_MAJOR_VERSION > 4 && ENGINE_MINOR_VERSION > 3
                    Collector.GetRHICommandList().UnlockBuffer(VertexBufferRHI);
#else
                    RHIUnlockBuffer(VertexBufferRHI);
#endif

                    FMeshBatchElement& BatchElement = Mesh.Elements[0];
                    BatchElement.IndexBuffer = &Section->IndexBuffer;

                    bool bHasPrecomputedVolumetricLightmap;
                    FMatrix PreviousLocalToWorld;
                    int32 SingleCaptureIndex;
                    bool bOutputVelocity;
                    GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

                    FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();

#if ENGINE_MAJOR_VERSION > 4 && ENGINE_MINOR_VERSION > 3
                    DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
#else
                    DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
#endif
                    BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

                    BatchElement.FirstIndex = 0;
                    BatchElement.NumPrimitives = Section->IndexBuffer.GetNumIndices() / 3;
                    BatchElement.MinVertexIndex = 0;
                    BatchElement.MaxVertexIndex = Section->MaxVertexIndex;

#if ENABLE_DRAW_DEBUG
                    BatchElement.VisualizeElementIndex = Section->SectionIndex;
#endif

                    // Draw bounds
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
                    if (bIsWireframeView)
                    {
                        RenderBounds(Collector.GetPDI(ViewIndex), EngineShowFlags, GetBounds(), IsSelected());
                    }
#endif
                   
                    Collector.AddMesh(ViewIndex, Mesh);
                }
            }
        }
    }
}

FPrimitiveViewRelevance FLineRendererComponentSceneProxy::GetViewRelevance(const FSceneView* View) const
{
    FPrimitiveViewRelevance Result;
    Result.bDrawRelevance = IsShown(View);
    Result.bShadowRelevance = IsShadowCast(View);
    Result.bDynamicRelevance = true;
    Result.bRenderInMainPass = ShouldRenderInMainPass();
    Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
    Result.bRenderCustomDepth = ShouldRenderCustomDepth();
    Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
    
    Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

    MaterialRelevance.SetPrimitiveViewRelevance(Result);

    return Result;
}

void FLineRendererComponentSceneProxy::AddNewSection_GameThread(const FLineSectionInfo* SrcSection)
{
    check(IsInGameThread());

    const int32 NumVerts = SrcSection->Lines.Num() * 8 * 3;

    const int32 SrcSectionIndex = SrcSection->SectionIndex;

    TSharedPtr<FLineProxySection> NewSection(MakeShareable(new FLineProxySection(GetScene().GetFeatureLevel())));
    {
        NewSection->Lines = SrcSection->Lines;
        NewSection->MaxVertexIndex = NumVerts - 1;
        NewSection->SectionIndex = SrcSectionIndex;
        NewSection->Material = SrcSection->Material;
        NewSection->Color = SrcSection->Color;
        
        NewSection->SectionLocalBox = FBox3f(EForceInit::ForceInitToZero);

        NewSection->PositionVB = new FDynamicPositionVertexBuffer(NumVerts);

        // Using LocalVertexFactory requires to init all buffers
        NewSection->StaticMeshVertexBuffer.Init(NumVerts, 1, true);
        // NewSection->ColorVertexBuffer.Init(NumVerts, true);

        for (const FBatchedLine& Line : NewSection->Lines)
        {
            NewSection->SectionLocalBox += FVector3f(Line.Start);
            NewSection->SectionLocalBox += FVector3f(Line.End);

            // NO UV support just yet
            FStaticMeshVertexBuffer& StaticMeshVB = NewSection->StaticMeshVertexBuffer;
            
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

            for (int32 Index = 0; Index < 24; ++Index)
            {
                StaticMeshVB.SetVertexTangents(Index, FVector3f::UpVector, FVector3f::RightVector, FVector3f::ForwardVector);
            }
            //*/
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
        BeginInitResource(NewSection->PositionVB);
        BeginInitResource(&NewSection->IndexBuffer);

        // Using LocalVertexFactory requires to init all buffers
        BeginInitResource(&NewSection->StaticMeshVertexBuffer);
        // BeginInitResource(&NewSection->ColorVertexBuffer);
    }

    TSharedRef<FLineProxySection> SectionRef = StaticCastSharedRef<FLineProxySection>(NewSection.ToSharedRef());

    ENQUEUE_RENDER_COMMAND(LineVertexBuffersInit)(
        [this, SrcSectionIndex, SectionRef](FRHICommandListImmediate& RHICmdList)
        {
            FLocalVertexFactory::FDataType Data;

            SectionRef->PositionVB->BindPositionVertexBuffer(&SectionRef->VertexFactory, Data);

            // Using LocalVertexFactory requires to init all buffers
            SectionRef->StaticMeshVertexBuffer.BindTangentVertexBuffer(&SectionRef->VertexFactory, Data);
            SectionRef->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&SectionRef->VertexFactory, Data);
            SectionRef->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&SectionRef->VertexFactory, Data, 1);

            Data.LODLightmapDataIndex = 0;

#if ENGINE_MAJOR_VERSION > 4 && ENGINE_MINOR_VERSION > 3
            SectionRef->VertexFactory.SetData(RHICmdList, Data);
            SectionRef->VertexFactory.InitResource(RHICmdList);

#else
            SectionRef->VertexFactory.SetData(Data);
            SectionRef->VertexFactory.InitResource();
#endif
            

#if WITH_EDITOR
            TArray<UMaterialInterface*> UsedMaterials;
            Component->GetUsedMaterials(UsedMaterials);

            SetUsedMaterialForVerification(UsedMaterials);
#endif


            Sections_RenderThread.Add(SrcSectionIndex, SectionRef);

            SectionRef->bInitialized = true;
        }
    );
}

bool FLineRendererComponentSceneProxy::CanBeOccluded() const
{
    return !MaterialRelevance.bDisableDepthTest;
}

uint32 FLineRendererComponentSceneProxy::GetMemoryFootprint() const
{
    return sizeof(*this) + GetAllocatedSize();
}

uint32 FLineRendererComponentSceneProxy::GetAllocatedSize() const
{
    return FPrimitiveSceneProxy::GetAllocatedSize();
}

int32 FLineRendererComponentSceneProxy::GetNumSections() const
{
    return Sections_RenderThread.Num();
}

int32 FLineRendererComponentSceneProxy::GetNumPointsInSection(int32 SectionIndex) const
{
    const TSharedPtr<FLineProxySection>& SectionRef = Sections_RenderThread.FindRef(SectionIndex);
    if (SectionRef->PositionVB != nullptr)
    {
        return SectionRef->PositionVB->GetNumVertices();
    }

    return 0;
}

void FLineRendererComponentSceneProxy::ClearMeshSection(int32 SectionIndex)
{
    ENQUEUE_RENDER_COMMAND(ReleaseSectionResources)(
        [this, SectionIndex](FRHICommandListImmediate&)
        {
            Sections_RenderThread.Remove(SectionIndex);
        }
    );
}

void FLineRendererComponentSceneProxy::ClearAllMeshSections()
{
    TArray<int32> SectionIndices;
    Sections_RenderThread.GetKeys(SectionIndices);

    for (int32 SectionIndex : SectionIndices)
    {
        ClearMeshSection(SectionIndex);
    }
}

void FLineRendererComponentSceneProxy::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
    ENQUEUE_RENDER_COMMAND(SetMeshSectionVisibility)(
        [this, SectionIndex, bNewVisibility](FRHICommandListImmediate&)
        {
            if (Sections_RenderThread.Contains(SectionIndex))
            {
                Sections_RenderThread[SectionIndex]->bSectionVisible = bNewVisibility;
            }
        }
    );
}

bool FLineRendererComponentSceneProxy::IsMeshSectionVisible(int32 SectionIndex) const
{
    return Sections_RenderThread.FindRef(SectionIndex)->bSectionVisible;
}
