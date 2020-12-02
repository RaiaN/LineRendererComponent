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

    UMaterialInterface* Material;
};