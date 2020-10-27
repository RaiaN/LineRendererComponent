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
		NewSection->ProcIndexBuffer.Reserve(2 * (Vertices.Num() - 1));
	}

	const int32 NumTris = Vertices.Num() - 1;

	for (int32 TriInd = 0; TriInd < NumTris; ++TriInd)
	{
		NewSection->ProcIndexBuffer[2 * TriInd] = TriInd;
		NewSection->ProcIndexBuffer[2 * TriInd + 1] = TriInd + 1;
	}

	NewSection->SectionIndex = SectionIndex;

    UpdateLocalBounds(); // Update overall bounds

	PendingSections.Enqueue(NewSection);
}

void ULineMeshComponent::UpdateLine(int32 SectionIndex, const TArray<FVector>& Vertices, const FLinearColor& LineColor)
{
    // SCOPE_CYCLE_COUNTER(STAT_ProcMesh_UpdateSectionGT);

    /*if (SectionIndex < ProcMeshSections.Num())
    {
        FLineMeshSection& Section = ProcMeshSections[SectionIndex];
        const int32 NumVerts = Vertices.Num();
        const int32 PreviousNumVerts = Section.ProcVertexBuffer.Num();

        // See if positions are changing
        const bool bSameVertexCount = PreviousNumVerts == NumVerts;

        // Update bounds, if we are getting new position data
        if (bSameVertexCount)
        {
            Section.SectionLocalBox = FBox(Vertices);

            // Iterate through vertex data, copying in new info
            for (int32 VertIdx = 0; VertIdx < NumVerts; VertIdx++)
            {
                FVector& ModifyVert = Section.ProcVertexBuffer[VertIdx];

                // Position data
                if (Vertices.Num() == NumVerts)
                {
                    ModifyVert.Position = Vertices[VertIdx];
                }
            }

            // If we have a valid proxy and it is not pending recreation
            if (SceneProxy && !IsRenderStateDirty())
            {
                // Create data to update section
                FLineMeshSectionUpdateData* SectionData = new FLineMeshSectionUpdateData;
                SectionData->TargetSection = SectionIndex;
                SectionData->NewVertexBuffer = Section.ProcVertexBuffer;

                // Enqueue command to send to render thread
                FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
                ENQUEUE_RENDER_COMMAND(FLineMeshSectionUpdate)(
                    [ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
                    {
                        ProcMeshSceneProxy->UpdateSection_RenderThread(SectionData);
                    }
                );
            }

            UpdateLocalBounds();		 // Update overall bounds
            MarkRenderTransformDirty();  // Need to send new bounds to render thread
        }
        else
        {
            UE_LOG(LogLineRendererComponent, Error, TEXT("Trying to update a procedural mesh component section with a different number of vertices [Previous: %i, New: %i] (clear and recreate mesh section instead)"), PreviousNumVerts, NumVerts);
        }
    }*/
}

void ULineMeshComponent::PostLoad()
{
	Super::PostLoad();
}

void ULineMeshComponent::ClearMeshSection(int32 SectionIndex)
{
	/*if (SectionIndex < ProcMeshSections.Num())
	{
		ProcMeshSections[SectionIndex].Reset();
		UpdateLocalBounds();
		MarkRenderStateDirty();
	}*/
}

void ULineMeshComponent::ClearAllMeshSections()
{
	/*ProcMeshSections.Empty();
	UpdateLocalBounds();
	MarkRenderStateDirty();*/
}

void ULineMeshComponent::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
	/*if(SectionIndex < ProcMeshSections.Num())
	{
		// Set game thread state
		ProcMeshSections[SectionIndex].bSectionVisible = bNewVisibility;

		if (SceneProxy)
		{
			// Enqueue command to modify render thread info
			FLineMeshSceneProxy* ProcMeshSceneProxy = (FLineMeshSceneProxy*)SceneProxy;
			ENQUEUE_RENDER_COMMAND(FLineMeshSectionVisibilityUpdate)(
				[ProcMeshSceneProxy, SectionIndex, bNewVisibility](FRHICommandListImmediate& RHICmdList)
				{
					ProcMeshSceneProxy->SetSectionVisibility_RenderThread(SectionIndex, bNewVisibility);
				});
		}
	}*/
}

bool ULineMeshComponent::IsMeshSectionVisible(int32 SectionIndex) const
{
	return true; //(SectionIndex < ProcMeshSections.Num()) ? ProcMeshSections[SectionIndex].bSectionVisible : false;
}

int32 ULineMeshComponent::GetNumSections() const
{
	return 0; //ProcMeshSections.Num();
}


void ULineMeshComponent::UpdateLocalBounds()
{
	/*FBox LocalBox(ForceInit);

	for (const FLineMeshSection& Section : ProcMeshSections)
	{
		LocalBox += Section.SectionLocalBox;
	}

	LocalBounds = LocalBox.IsValid ? FBoxSphereBounds(LocalBox) : FBoxSphereBounds(FVector(0, 0, 0), FVector(0, 0, 0), 0); // fallback to reset box sphere bounds

	// Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();*/
}

FPrimitiveSceneProxy* ULineMeshComponent::CreateSceneProxy()
{
	// SCOPE_CYCLE_COUNTER(STAT_ProcMesh_CreateSceneProxy);

	return new FLineMeshSceneProxy(this);
}

int32 ULineMeshComponent::GetNumMaterials() const
{
	return 0; //ProcMeshSections.Num();
}

FBoxSphereBounds ULineMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds Ret(LocalBounds.TransformBy(LocalToWorld));

	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;

	return Ret;
}

UMaterialInterface* ULineMeshComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	UMaterialInterface* Result = nullptr;
	/*SectionIndex = 0;

	if (FaceIndex >= 0)
	{
		// Look for element that corresponds to the supplied face
		int32 TotalFaceCount = 0;
		for (int32 SectionIdx = 0; SectionIdx < ProcMeshSections.Num(); SectionIdx++)
		{
			const FLineMeshSection& Section = ProcMeshSections[SectionIdx];
			int32 NumFaces = Section.ProcIndexBuffer.Num() / 3;
			TotalFaceCount += NumFaces;

			if (FaceIndex < TotalFaceCount)
			{
				// Grab the material
				Result = GetMaterial(SectionIdx);
				SectionIndex = SectionIdx;
				break;
			}
		}
	}*/

	return Result;
}