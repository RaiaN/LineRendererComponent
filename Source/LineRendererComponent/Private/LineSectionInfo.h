// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Math/Color.h"

class UMaterialInterface;

/* Line section description */
struct FLineSectionInfo
{
public:
    int32 SectionIndex;
    TArray<FBatchedLine> Lines;
    UMaterialInterface* Material;
    FLinearColor Color;
};

/**
 *	Struct used to send update to line section
 */
struct FLineSectionUpdateData
{
public:
    int32 SectionIndex;
    TArray<FVector3f> VertexBuffer;
    FBox3f SectionLocalBox;
    FLinearColor Color;
};
