// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
/** Line section description */
struct FLineMeshSection
{
public:
    FLineMeshSection()
        : SectionLocalBox(ForceInit)
        , bSectionVisible(true)
        , SectionIndex(-1)
        , MaxVertexIndex(-1)
        , Color(FColor::Red)
    {}

    /** Reset this section, clear all mesh info. */
    void Reset()
    {
        ProcVertexBuffer.Empty();
        SectionLocalBox.Init();
        bSectionVisible = true;
        SectionIndex = -1;
        MaxVertexIndex = -1;
        Color = FColor::Red;
    }

public:
    TArray<FVector3f> ProcVertexBuffer;
    TArray<uint32> ProcIndexBuffer;

    FBox3f SectionLocalBox;
    bool bSectionVisible;
    int32 SectionIndex;
    int32 MaxVertexIndex;

    FLinearColor Color;
};