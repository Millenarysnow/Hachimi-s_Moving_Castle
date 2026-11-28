#pragma once

#include "Audio/CastleAudioTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CastleAudioSubsystem.generated.h"

class UAudioComponent;
class UCastleAudioConfigDataAsset;
class USoundBase;

/** 每个游戏实例持有一条音乐轨和一条语音通道，玩法只发布语义事件。 */
UCLASS()
class MOVING_CASTLE_API UCastleAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 从玩法世界定位音频实例，编辑器预览世界不拥有游戏实例。 */
	static UCastleAudioSubsystem* GetForWorld(const UWorld* World);
	static void SetBGM(const UWorld* World, ECastleBGMTrack Track);
	static void PlaySFX(const UWorld* World, ECastleSFXEvent Event);

	void SetBGM(ECastleBGMTrack Track);
	void PlaySFX(ECastleSFXEvent Event);
	void PlayDialogueVoice(USoundBase* Voice, bool bUseWangcaiVoice);
	void StopDialogueVoice();
	void PlayTutorialCryBubbleVoice();

	/** 记录已提交的播放请求，可在 UE 禁用混音设备时断言玩法行为。 */
	ECastleBGMTrack GetCurrentBGMTrack() const { return CurrentTrack; }
	ECastleSFXEvent GetLastPlayedSFX() const { return LastPlayedSFX; }
	USoundBase* GetCurrentVoiceSound() const { return CurrentVoice; }
	uint32 GetBGMStartCount() const { return BGMStartCount; }

private:
	bool HasConfiguration() const;
	void StartVoice(USoundBase* Sound);

	UPROPERTY(Transient)
	TObjectPtr<UCastleAudioConfigDataAsset> Configuration;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BGMComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CurrentVoice;

	ECastleBGMTrack CurrentTrack = ECastleBGMTrack::None;
	ECastleSFXEvent LastPlayedSFX = ECastleSFXEvent::Count;
	uint32 BGMStartCount = 0;
	int32 LastWangcaiIndex = INDEX_NONE;
	/** 录音抽样使用自己的随机流，不消费玩法随机数。 */
	FRandomStream AudioRandom;
	TMap<ECastleSFXEvent, double> LastHighFreqStartTimes;
};
