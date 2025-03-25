#include "Spout2BlueprintLibrary.h"
#include "Spout2_DX12.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "RenderingThread.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CoreMinimal.h"

static FTimerHandle SpoutUpdateHandle;
static UWorld* CachedSpoutWorld;

void USpout2BlueprintLibrary::StartSpoutBroadcast(FString SenderName, UTextureRenderTarget2D* RenderTarget, int32 FPS)
{
    if (!RenderTarget || SenderName.IsEmpty()) return;
    FSpout2_DX12Module::Get().SendRenderTarget(RenderTarget, SenderName);

    if (FPS <= 0) return;

    float Interval = 1.0f / FMath::Clamp(FPS, 1, 240);

    UWorld* World = nullptr;

    if (GEngine)
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.World() && (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game))
            {
                World = Context.World();
                break;
            }
        }
    }
    if (!World) return;
    CachedSpoutWorld = World;

    World->GetTimerManager().SetTimer(SpoutUpdateHandle, [RenderTarget, SenderName]()
        {
            if (RenderTarget)
            {
                FSpout2_DX12Module::Get().UpdateTexture(RenderTarget, SenderName);
            } else 
            { 
                UE_LOG(LogTemp, Log, TEXT("Spout Error! No render target."));
                return;
            }
        }, Interval, true);
}

void USpout2BlueprintLibrary::StopSpoutBroadcast()
{
    if (CachedSpoutWorld)
    {
        CachedSpoutWorld->GetTimerManager().ClearTimer(SpoutUpdateHandle);
        CachedSpoutWorld = nullptr;
    }
}
