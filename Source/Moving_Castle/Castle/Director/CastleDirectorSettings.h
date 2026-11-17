// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CastleDirectorSettings.generated.h"

class UCastleDirectorConfigDataAsset;

/** 导演系统全局入口设置；只有列出的地图会由导演接管流程。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Castle Director"))
class MOVING_CASTLE_API UCastleDirectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 当前整局所用配置；启动时必须加载并完整校验。 */
	UPROPERTY(Config, EditAnywhere, Category = "导演")
	TSoftObjectPtr<UCastleDirectorConfigDataAsset> DirectorConfig { FSoftObjectPath(TEXT("/Game/MovingCastle/Data/Director/DA_DirectorConfig")) };

	/** 导演自动启动地图白名单，资产路径应为完整地图路径。 */
	UPROPERTY(Config, EditAnywhere, Category = "导演")
	TArray<FSoftObjectPath> AutoStartMaps = { FSoftObjectPath(TEXT("/Game/MovingCastle/Maps/MainMap")) };
};
