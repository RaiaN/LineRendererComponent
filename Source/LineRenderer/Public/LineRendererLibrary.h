// Copyright Peter Leontev

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LineRendererLibrary.generated.h"



UCLASS()
class LINERENDERER_API ULineRendererLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "LineRenderer")
    static void DrawLines();
};
