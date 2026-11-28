#pragma once

#include "Audio/CastleAudioTypes.h"
#include "Engine/DataAsset.h"
#include "CastleAudioConfigDataAsset.generated.h"

class USoundWave;

/** 正式 BGM、音效池和通用语音通过硬引用随总表加载与打包。 */
UCLASS()
class MOVING_CASTLE_API UCastleAudioConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 依 MainMenu、Build、Battle、SkyIsland 顺序存储四条循环曲目。 */
	UPROPERTY(EditAnywhere, Category = "音频|音乐")
	TArray<TObjectPtr<USoundWave>> BackgroundMusicTracks;

	/** 依 ECastleSFXEvent 枚举声明顺序存储每个事件的第一条音效。 */
	UPROPERTY(EditAnywhere, Category = "音频|音效")
	TArray<TObjectPtr<USoundWave>> SoundEffects;

	UPROPERTY(EditAnywhere, Category = "音频|音效")
	TObjectPtr<USoundWave> AlternateMainTowerHit;

	/** 三条不同的旺财短音，由语音通道无连续重复地抽取。 */
	UPROPERTY(EditAnywhere, Category = "音频|语音")
	TArray<TObjectPtr<USoundWave>> WangcaiVoices;

	UPROPERTY(EditAnywhere, Category = "音频|语音")
	TObjectPtr<USoundWave> CryBubbleVoice;

	UPROPERTY(EditAnywhere, Category = "音频|音量")
	float BGMVolume = 0.6f;

	UPROPERTY(EditAnywhere, Category = "音频|音量")
	float SFXVolume = 0.85f;

	UPROPERTY(EditAnywhere, Category = "音频|音量")
	float VoiceVolume = 1.0f;

	/** 高频战斗音效按各自事件计算真实时间间隔，不受战斗倍速影响。 */
	UPROPERTY(EditAnywhere, Category = "音频|音效")
	float HighFreqSFXCooldown = 0.08f;

	/** 编辑器批量导入把音频序列写入对应槽位，并校验整张正式配置。 */
	UFUNCTION(BlueprintCallable, Category = "音频|编辑器")
	bool SetImportedAudioAssets(const TArray<USoundWave*>& InBackgroundMusic,
		const TArray<USoundWave*>& InSoundEffects, USoundWave* InAlternateMainTowerHit,
		const TArray<USoundWave*>& InWangcaiVoices, USoundWave* InCryBubbleVoice);

	/** 校验完整事件覆盖、可播放时长、循环属性及数值范围。 */
	bool ValidateConfiguration(FText& OutReason) const;
};
