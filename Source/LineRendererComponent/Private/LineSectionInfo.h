// Copyright 2023 Petr Leontev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Math/Color.h"
#include "LineSectionInfo.generated.h"

class UMaterialInterface;

/* Line section description */

USTRUCT()
struct FLineSectionInfo
{
    GENERATED_BODY()

public:
    int32 SectionIndex;
    TArray<FBatchedLine> Lines;
    FLinearColor Color;

    UPROPERTY()
    UMaterialInterface* Material;
};
