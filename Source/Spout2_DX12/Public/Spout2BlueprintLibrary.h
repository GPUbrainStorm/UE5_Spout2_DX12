#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Spout2BlueprintLibrary.generated.h"

UCLASS()
class USpout2BlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Spout")
    static void StartSpoutBroadcast(FString SenderName, UTextureRenderTarget2D* RenderTarget, int32 FPS = 60);

    UFUNCTION(BlueprintCallable, Category = "Spout")
    static void StopSpoutBroadcast();
};
