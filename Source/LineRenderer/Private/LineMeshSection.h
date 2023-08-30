// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"

class UMaterialInterface;

/**
/** Line section description */
struct FLineMeshSection
{
public:
    TArray<FBatchedLine> Lines;
    int32 SectionIndex;
};