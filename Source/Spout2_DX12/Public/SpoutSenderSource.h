#pragma once

#include "CoreMinimal.h"
#include "SpoutSenderSource.generated.h"

UENUM(BlueprintType)
enum class ESpoutSenderSourceType : uint8
{
    RenderTarget UMETA(DisplayName = "Render Target"),
    GameViewport UMETA(DisplayName = "Game Viewport"),
    EditorViewport UMETA(DisplayName = "Editor Viewport"),
};
