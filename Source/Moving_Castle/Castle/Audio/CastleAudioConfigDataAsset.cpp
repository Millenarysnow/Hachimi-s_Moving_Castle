#include "Audio/CastleAudioConfigDataAsset.h"

#include "Moving_Castle.h"
#include "Sound/SoundWave.h"

bool UCastleAudioConfigDataAsset::SetImportedAudioAssets(const TArray<USoundWave*>& InBackgroundMusic,
	const TArray<USoundWave*>& InSoundEffects, USoundWave* InAlternateMainTowerHit,
	const TArray<USoundWave*>& InWangcaiVoices, USoundWave* InCryBubbleVoice)
{
	// 先验证批量数量，避免不完整音频导入清空已配置的列表。
	if (InBackgroundMusic.Num() != 4 || InSoundEffects.Num() != static_cast<int32>(ECastleSFXEvent::Count)
		|| InWangcaiVoices.Num() != 3 || !IsValid(InAlternateMainTowerHit) || !IsValid(InCryBubbleVoice))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("音频配置 %s 拒绝导入：BGM %d/4、SFX %d/%d、旺财语音 %d/3，主塔备选音或气泡音缺失。"),
			*GetPathName(), InBackgroundMusic.Num(), InSoundEffects.Num(),
			static_cast<int32>(ECastleSFXEvent::Count), InWangcaiVoices.Num());
		return false;
	}
	const auto CopySounds = [](const TArray<USoundWave*>& Source, TArray<TObjectPtr<USoundWave>>& Destination)
	{
		Destination.Reset();
		for (USoundWave* Sound : Source) { Destination.Add(Sound); }
	};
	CopySounds(InBackgroundMusic, BackgroundMusicTracks);
	CopySounds(InSoundEffects, SoundEffects);
	AlternateMainTowerHit = InAlternateMainTowerHit;
	CopySounds(InWangcaiVoices, WangcaiVoices);
	CryBubbleVoice = InCryBubbleVoice;
	FText Reason;
	return ValidateConfiguration(Reason);
}

bool UCastleAudioConfigDataAsset::ValidateConfiguration(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const auto Fail = [this, &OutReason](const FString& Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("音频配置 %s：%s"), *GetPathName(), *Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	// 每个槽位必须指向单独的资产，防止误把同一录音填满整个音效表。
	TSet<const USoundWave*> UsedSounds;
	const auto CheckSound = [&UsedSounds, &Fail](const USoundWave* Sound, const FString& Label, const bool bLoop)
	{
		if (!IsValid(Sound) || !FMath::IsFinite(Sound->Duration) || Sound->Duration <= 0.0f
			|| Sound->bLooping != bLoop || UsedSounds.Contains(Sound))
		{
			return Fail(FString::Printf(TEXT("%s 的音频 %s 缺失、重复、时长无效或循环配置错误。"), *Label, *GetPathNameSafe(Sound)));
		}
		UsedSounds.Add(Sound);
		return true;
	};
	if (BackgroundMusicTracks.Num() != 4 || SoundEffects.Num() != static_cast<int32>(ECastleSFXEvent::Count) || WangcaiVoices.Num() != 3)
	{
		return Fail(TEXT("必须配置 4 条 BGM、全部 20 个音效事件和 3 条旺财语音。"));
	}
	for (int32 Index = 0; Index < BackgroundMusicTracks.Num(); ++Index)
	{
		if (!CheckSound(BackgroundMusicTracks[Index], FString::Printf(TEXT("BGM %d"), Index + 1), true)) { return false; }
	}
	for (int32 Index = 0; Index < static_cast<int32>(ECastleSFXEvent::Count); ++Index)
	{
		const auto Event = static_cast<ECastleSFXEvent>(Index);
		if (!CheckSound(SoundEffects[Index], UEnum::GetValueAsString(Event), false)) { return false; }
	}
	if (!CheckSound(AlternateMainTowerHit, TEXT("第二条主塔受击音效"), false)
		|| !CheckSound(CryBubbleVoice, TEXT("城主气泡语音"), false)) { return false; }
	for (int32 Index = 0; Index < WangcaiVoices.Num(); ++Index)
	{
		if (!CheckSound(WangcaiVoices[Index], FString::Printf(TEXT("旺财语音 %d"), Index + 1), false)) { return false; }
	}
	const auto IsVolumeValid = [](const float Volume) { return FMath::IsFinite(Volume) && Volume > 0.0f && Volume <= 2.0f; };
	if (!IsVolumeValid(BGMVolume) || !IsVolumeValid(SFXVolume) || !IsVolumeValid(VoiceVolume))
	{
		return Fail(TEXT("三类音量均须为 (0, 2] 内的有限数。"));
	}
	if (!FMath::IsFinite(HighFreqSFXCooldown) || HighFreqSFXCooldown < 0.0f || HighFreqSFXCooldown > 1.0f)
	{
		return Fail(TEXT("高频音效冷却须为 [0, 1] 秒内的有限数。"));
	}
	return true;
}
