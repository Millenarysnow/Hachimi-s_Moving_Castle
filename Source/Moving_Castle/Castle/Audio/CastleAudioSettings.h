#pragma once

#include "Engine/DeveloperSettings.h"
#include "CastleAudioSettings.generated.h"

class UCastleAudioConfigDataAsset;

/** 项目配置提供唯一正式音频总表路径。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Castle Audio"))
class MOVING_CASTLE_API UCastleAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "音频")
	TSoftObjectPtr<UCastleAudioConfigDataAsset> AudioConfig;
};
