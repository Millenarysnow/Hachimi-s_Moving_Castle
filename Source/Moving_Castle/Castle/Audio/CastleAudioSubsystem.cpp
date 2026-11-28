#include "Audio/CastleAudioSubsystem.h"

#include "Audio/CastleAudioConfigDataAsset.h"
#include "Audio/CastleAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "Sound/SoundWave.h"

namespace
{
	/** 只有音乐与语音需要保留组件句柄，以便切轨和跳句立即打断。 */
	void StopCastleAudioComponent(TObjectPtr<UAudioComponent>& Component)
	{
		if (IsValid(Component))
		{
			Component->Stop();
			Component->DestroyComponent();
		}
		Component = nullptr;
	}

	/** 群射和集中受击按事件节流，UI 与交易反馈始终逐次播放。 */
	bool IsHighFrequencyCastleSound(const ECastleSFXEvent Event)
	{
		switch (Event)
		{
		case ECastleSFXEvent::BowShot:
		case ECastleSFXEvent::ArrowFlight:
		case ECastleSFXEvent::AttackMainTower:
		case ECastleSFXEvent::CannonFire:
		case ECastleSFXEvent::Bees:
		case ECastleSFXEvent::DragonFire:
			return true;
		default:
			return false;
		}
	}
}

void UCastleAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AudioRandom.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	const auto& ConfigPath = GetDefault<UCastleAudioSettings>()->AudioConfig;
	auto* Loaded = ConfigPath.LoadSynchronous();
	if (!IsValid(Loaded))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("音频子系统 %s 无法加载 CastleAudioSettings.AudioConfig：%s。"), *GetPathName(), *ConfigPath.ToString());
		return;
	}
	FText Reason;
	if (Loaded->ValidateConfiguration(Reason)) { Configuration = Loaded; }
}

void UCastleAudioSubsystem::Deinitialize()
{
	SetBGM(ECastleBGMTrack::None);
	StopDialogueVoice();
	Configuration = nullptr;
	Super::Deinitialize();
}

UCastleAudioSubsystem* UCastleAudioSubsystem::GetForWorld(const UWorld* World)
{
	auto* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCastleAudioSubsystem>() : nullptr;
}

void UCastleAudioSubsystem::SetBGM(const UWorld* World, const ECastleBGMTrack Track)
{
	if (auto* Audio = GetForWorld(World)) { Audio->SetBGM(Track); }
}

void UCastleAudioSubsystem::PlaySFX(const UWorld* World, const ECastleSFXEvent Event)
{
	if (auto* Audio = GetForWorld(World)) { Audio->PlaySFX(Event); }
}

bool UCastleAudioSubsystem::HasConfiguration() const
{
	if (IsValid(Configuration)) { return true; }
	UE_LOG(LogMoving_Castle, Error, TEXT("音频子系统 %s 无有效音频配置 %s，拒绝播放。"),
		*GetPathName(), *GetDefault<UCastleAudioSettings>()->AudioConfig.ToString());
	return false;
}

void UCastleAudioSubsystem::SetBGM(const ECastleBGMTrack Track)
{
	if (Track == CurrentTrack) { return; }
	if (Track == ECastleBGMTrack::None)
	{
		StopCastleAudioComponent(BGMComponent);
		CurrentTrack = Track;
		return;
	}
	if (!HasConfiguration()) { return; }
	const int32 TrackIndex = static_cast<int32>(Track) - static_cast<int32>(ECastleBGMTrack::MainMenu);
	auto* Sound = Configuration->BackgroundMusicTracks.IsValidIndex(TrackIndex) ? Configuration->BackgroundMusicTracks[TrackIndex].Get() : nullptr;
	if (!IsValid(Sound))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("音频配置 %s 无有效曲目 %s。"), *Configuration->GetPathName(), *UEnum::GetValueAsString(Track));
		return;
	}
	StopCastleAudioComponent(BGMComponent);
	CurrentTrack = Track;
	++BGMStartCount;
	// 跨图保留组件直到下一张地图接管。禁用声音的 UE 世界会合法返回空组件。
	BGMComponent = UGameplayStatics::CreateSound2D(GetWorld(), Sound, Configuration->BGMVolume, 1.0f, 0.0f, nullptr, true, false);
	if (BGMComponent) { BGMComponent->Play(); }
}

void UCastleAudioSubsystem::PlaySFX(const ECastleSFXEvent Event)
{
	if (!HasConfiguration()) { return; }
	const int32 EventIndex = static_cast<int32>(Event);
	auto* Sound = Configuration->SoundEffects.IsValidIndex(EventIndex) ? Configuration->SoundEffects[EventIndex].Get() : nullptr;
	if (!IsValid(Sound))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("音频配置 %s 无有效音效 %s。"), *Configuration->GetPathName(), *UEnum::GetValueAsString(Event));
		return;
	}
	if (IsHighFrequencyCastleSound(Event))
	{
		const double Now = FPlatformTime::Seconds();
		const double* Last = LastHighFreqStartTimes.Find(Event);
		if (Last && Now - *Last < Configuration->HighFreqSFXCooldown) { return; }
		LastHighFreqStartTimes.Add(Event, Now);
	}
	if (Event == ECastleSFXEvent::AttackMainTower && AudioRandom.RandHelper(2) == 1)
	{
		Sound = Configuration->AlternateMainTowerHit;
	}
	LastPlayedSFX = Event;
	if (Event == ECastleSFXEvent::EnterGame)
	{
		// 进入游戏音效在 OpenLevel 前一刻启动，需要在旧世界被卸载后继续播完。
		UGameplayStatics::SpawnSound2D(GetWorld(), Sound, Configuration->SFXVolume, 1.0f, 0.0f, nullptr, true);
		return;
	}
	UGameplayStatics::PlaySound2D(GetWorld(), Sound, Configuration->SFXVolume);
}

void UCastleAudioSubsystem::PlayDialogueVoice(USoundBase* Voice, const bool bUseWangcaiVoice)
{
	StopDialogueVoice();
	// 临时剧本的可选空语音已由剧本校验记 Warning。切到该行时仍打断上一句。
	if (!Voice && !bUseWangcaiVoice) { return; }
	if (!HasConfiguration()) { return; }
	if (bUseWangcaiVoice)
	{
		const int32 Count = Configuration->WangcaiVoices.Num();
		// 先抽取其余索引，再跨过上次的位置，让其它短音的概率相同。
		int32 Pick = AudioRandom.RandHelper(Count - (LastWangcaiIndex != INDEX_NONE ? 1 : 0));
		if (LastWangcaiIndex != INDEX_NONE && Pick >= LastWangcaiIndex) { ++Pick; }
		LastWangcaiIndex = Pick;
		Voice = Configuration->WangcaiVoices[Pick];
	}
	StartVoice(Voice);
}

void UCastleAudioSubsystem::StartVoice(USoundBase* Sound)
{
	CurrentVoice = Sound;
	// 录音跟随当前世界，切图时由对话结束钩子和 UE 世界卸载共同终止。
	VoiceComponent = UGameplayStatics::CreateSound2D(GetWorld(), Sound, Configuration->VoiceVolume, 1.0f, 0.0f, nullptr, false, false);
	if (VoiceComponent) { VoiceComponent->Play(); }
}

void UCastleAudioSubsystem::StopDialogueVoice()
{
	StopCastleAudioComponent(VoiceComponent);
	CurrentVoice = nullptr;
}

void UCastleAudioSubsystem::PlayTutorialCryBubbleVoice()
{
	if (HasConfiguration()) { PlayDialogueVoice(Configuration->CryBubbleVoice, false); }
}
