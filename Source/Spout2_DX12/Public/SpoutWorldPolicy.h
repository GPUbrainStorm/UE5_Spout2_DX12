#pragma once

#include "CoreMinimal.h"
#include "SpoutWorldPolicy.generated.h"

UENUM(BlueprintType)
enum class ESpoutWorldBootstrapPolicy : uint8
{
    EditorAndGame UMETA(DisplayName = "Editor | Game"),
    GameOnly UMETA(DisplayName = "Game Only"),
    EditorGameAndSinglePreview UMETA(DisplayName = "Editor | Game | Single Preview"),
};
