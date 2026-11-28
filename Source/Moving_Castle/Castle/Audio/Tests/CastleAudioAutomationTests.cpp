#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Audio/CastleAudioConfigDataAsset.h"
#include "Audio/CastleAudioSettings.h"
#include "Audio/CastleAudioSubsystem.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"

namespace
{
	const TCHAR* const CastleAudioDialogueRoot = TEXT("/Game/MovingCastle/Data/Tutorial");

	UCastleDialogueScriptDataAsset* LoadCastleAudioDialogueForTest(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s/DA_%s.DA_%s"), CastleAudioDialogueRoot, Name, Name);
		return LoadObject<UCastleDialogueScriptDataAsset>(nullptr, *Path);
	}
}

/** 复核磁盘上交付的语音引用，逐行断言顺序，避免同为有效音频的错绑通过总表校验。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleAudioAssetValidationTest, "MovingCastle.Audio.ConfigAndAssetValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleAudioAssetValidationTest::RunTest(const FString&)
{
	const auto* Config = GetDefault<UCastleAudioSettings>()->AudioConfig.LoadSynchronous();
	if (!TestNotNull(TEXT("项目设置可加载正式音频总表"), Config)) { return false; }
	TestEqual(TEXT("音频总表资产路径"), Config->GetPathName(), FString(TEXT("/Game/MovingCastle/Data/Audio/DA_AudioConfig.DA_AudioConfig")));
	FText Reason;
	if (!TestTrue(TEXT("配置有四首循环 BGM、21 条音效、三条旺财音及气泡音"), Config->ValidateConfiguration(Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	const TCHAR* ExpectedBGM[] = {TEXT("SW_BGM_MainMenu"), TEXT("SW_BGM_Build"), TEXT("SW_BGM_Battle"), TEXT("SW_BGM_SkyIsland")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedBGM); ++Index)
	{
		const FString ExpectedPath = FString::Printf(TEXT("/Game/MovingCastle/Audio/BGM/%s.%s"), ExpectedBGM[Index], ExpectedBGM[Index]);
		TestEqual(FString::Printf(TEXT("BGM 顺序槽 %d"), Index), Config->BackgroundMusicTracks[Index]->GetPathName(), ExpectedPath);
	}
	const TCHAR* ExpectedSFX[] = {
		TEXT("SW_SFX_Interact"), TEXT("SW_SFX_ClosePanel"), TEXT("SW_SFX_Gamble"), TEXT("SW_SFX_Defeat"),
		TEXT("SW_SFX_Build"), TEXT("SW_SFX_Victory"), TEXT("SW_SFX_Demolish"), TEXT("SW_SFX_BowShot"),
		TEXT("SW_SFX_Button"), TEXT("SW_SFX_MainTowerHit_01"), TEXT("SW_SFX_BalloonPop"), TEXT("SW_SFX_CannonFire"),
		TEXT("SW_SFX_ArrowFlight"), TEXT("SW_SFX_CrateLanded"), TEXT("SW_SFX_PageTurn"), TEXT("SW_SFX_Reward"),
		TEXT("SW_SFX_Gold"), TEXT("SW_SFX_Bees"), TEXT("SW_SFX_EnterGame"), TEXT("SW_SFX_DragonFire")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedSFX); ++Index)
	{
		const FString ExpectedPath = FString::Printf(TEXT("/Game/MovingCastle/Audio/SFX/%s.%s"), ExpectedSFX[Index], ExpectedSFX[Index]);
		TestEqual(FString::Printf(TEXT("SFX 枚举槽 %d"), Index), Config->SoundEffects[Index]->GetPathName(), ExpectedPath);
	}
	TestEqual(TEXT("第二条主塔受击音效"), Config->AlternateMainTowerHit->GetPathName(),
		FString(TEXT("/Game/MovingCastle/Audio/SFX/SW_SFX_MainTowerHit_02.SW_SFX_MainTowerHit_02")));
	for (int32 Index = 0; Index < Config->WangcaiVoices.Num(); ++Index)
	{
		const FString Name = FString::Printf(TEXT("SW_Voice_Wangcai_%02d"), Index + 1);
		TestEqual(FString::Printf(TEXT("旺财语音 %d"), Index + 1), Config->WangcaiVoices[Index]->GetPathName(),
			FString::Printf(TEXT("/Game/MovingCastle/Audio/Voice/%s.%s"), *Name, *Name));
	}
	TestEqual(TEXT("城主气泡语音"), Config->CryBubbleVoice->GetPathName(),
		FString(TEXT("/Game/MovingCastle/Audio/Voice/SW_Voice_CryBubble.SW_Voice_CryBubble")));

	TSet<const USoundWave*> Waves;
	for (const auto& Sound : Config->BackgroundMusicTracks) { Waves.Add(Sound); }
	for (const auto& Sound : Config->SoundEffects) { Waves.Add(Sound); }
	for (const auto& Sound : Config->WangcaiVoices) { Waves.Add(Sound); }
	Waves.Add(Config->AlternateMainTowerHit);
	Waves.Add(Config->CryBubbleVoice);
	struct FExpectedScript { const TCHAR* Name; TArray<const TCHAR*> Voices; };
	// 空后缀表示旺财短音池。数字为原始配音文件编号，S09 按资产行存储顺序验收。
	const FExpectedScript Scripts[] = {
		{TEXT("Tutorial_S01"), {TEXT("Tutorial_01"), TEXT("Tutorial_02"), nullptr, TEXT("Tutorial_03"), nullptr,
			TEXT("Tutorial_04"), TEXT("Tutorial_05"), nullptr, TEXT("Tutorial_06"), nullptr}},
		{TEXT("Tutorial_S03"), {TEXT("Tutorial_07"), nullptr}},
		{TEXT("Tutorial_S04"), {TEXT("Tutorial_08")}},
		{TEXT("Tutorial_S05"), {TEXT("Tutorial_09")}},
		{TEXT("Tutorial_S06"), {TEXT("Tutorial_10"), nullptr}},
		{TEXT("Tutorial_S07"), {TEXT("Tutorial_11")}},
		{TEXT("Tutorial_S08"), {TEXT("Tutorial_12")}},
		{TEXT("Tutorial_S09"), {nullptr, TEXT("Tutorial_13"), nullptr, TEXT("Tutorial_14"), nullptr, TEXT("Tutorial_15")}},
		{TEXT("Tutorial_S10"), {nullptr}},
		{TEXT("Tutorial_S11"), {nullptr}},
		{TEXT("Tutorial_S12"), {nullptr, TEXT("Tutorial_18")}},
		{TEXT("Tutorial_S13"), {TEXT("Tutorial_19")}},
		{TEXT("Tutorial_S14"), {TEXT("Tutorial_22")}},
		{TEXT("Tutorial_Claim"), {TEXT("Tutorial_17")}},
		{TEXT("Tutorial_OutOfStock"), {TEXT("Tutorial_20")}},
		{TEXT("Tutorial_FarmOffer"), {TEXT("Tutorial_21")}},
		{TEXT("Tutorial_Closing"), {nullptr, TEXT("Tutorial_23"), nullptr}},
		{TEXT("Arrival_Shop"), {TEXT("Arrival_Shop")}},
		{TEXT("Arrival_NPC"), {TEXT("Arrival_NPC")}},
		{TEXT("Arrival_Gamble"), {TEXT("Arrival_Gamble_01"), nullptr, TEXT("Arrival_Gamble_02")}},
	};
	static_assert(UE_ARRAY_COUNT(Scripts) == 20);
	for (const auto& Expected : Scripts)
	{
		const auto* Script = LoadCastleAudioDialogueForTest(Expected.Name);
		if (!TestNotNull(Expected.Name, Script)) { continue; }
		TestTrue(FString::Printf(TEXT("%s 持久化强制正式语音"), Expected.Name), Script->bRequireVoiceLines);
		if (!Script->ValidateScript(Reason)) { AddError(Reason.ToString()); }
		if (!TestEqual(Expected.Name, Script->Lines.Num(), Expected.Voices.Num())) { continue; }
		for (int32 Index = 0; Index < Script->Lines.Num(); ++Index)
		{
			const auto& Line = Script->Lines[Index];
			const TCHAR* Voice = Expected.Voices[Index];
			const FString Label = FString::Printf(TEXT("%s 第 %d 行配音"), Expected.Name, Index + 1);
			TestEqual(Label, Line.bUseWangcaiVoice, Voice == nullptr);
			if (!Voice)
			{
				TestNull(Label, Line.VoiceLine.Get());
				continue;
			}
			const auto* Sound = Cast<USoundWave>(Line.VoiceLine.Get());
			if (TestNotNull(Label, Sound))
			{
				const FString ExpectedPath = FString::Printf(TEXT("/Game/MovingCastle/Audio/Voice/SW_Voice_%s.SW_Voice_%s"), Voice, Voice);
				TestEqual(Label, Sound->GetPathName(), ExpectedPath);
				TestTrue(Label, !Sound->bLooping && FMath::IsFinite(Sound->Duration) && Sound->Duration > 0.0f);
				Waves.Add(Sound);
			}
		}
	}
	TestEqual(TEXT("总表和正式剧本共同引用 55 条不同录音"), Waves.Num(), 55);
	return true;
}

/** 用正式 MainMap 的开局、对话与死亡入口验证状态；显式关闭世界音频以涵盖无设备运行。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleAudioRuntimeHooksTest, "MovingCastle.Audio.RuntimeHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleAudioRuntimeHooksTest::RunTest(const FString&)
{
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = FPlatformTime::Seconds() + 45.0]()
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Player = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
		if (!Player || !Director || !Director->IsRunning())
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			AddError(TEXT("MainMap 控制器和正式导演未就绪"));
			return true;
		}
		auto* Audio = UCastleAudioSubsystem::GetForWorld(World);
		auto* Dialogue = World->GetGameInstance()->GetSubsystem<UCastleDialogueSubsystem>();
		auto* MainTower = World->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
		auto* Opening = LoadCastleAudioDialogueForTest(TEXT("Tutorial_S01"));
		if (!TestNotNull(TEXT("正式音频实例"), Audio) || !TestNotNull(TEXT("正式对话实例"), Dialogue)
			|| !TestNotNull(TEXT("正式主塔"), MainTower) || !TestNotNull(TEXT("正式测试剧本"), Opening))
		{
			return true;
		}
		TestEqual(TEXT("正式局开场请求建造曲"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::Build);
		const bool bPreviousAudioPlayback = World->bAllowAudioPlayback;
		World->bAllowAudioPlayback = false;
		ON_SCOPE_EXIT { World->bAllowAudioPlayback = bPreviousAudioPlayback; };
		// 同一实例在禁用音频的世界中走原生 PlaySound2D/CreateSound2D 路径，相关 Error/ensure 会使测试失败。
		Audio->SetBGM(ECastleBGMTrack::MainMenu);
		const uint32 StartCount = Audio->GetBGMStartCount();
		Audio->SetBGM(ECastleBGMTrack::MainMenu);
		TestEqual(TEXT("同曲不重新起播"), Audio->GetBGMStartCount(), StartCount);
		Audio->SetBGM(ECastleBGMTrack::Battle);
		TestEqual(TEXT("无音频设备仍提交战斗曲请求"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::Battle);
		TestEqual(TEXT("切曲只启动一次"), Audio->GetBGMStartCount(), StartCount + 1);
		Audio->PlaySFX(ECastleSFXEvent::Button);
		TestEqual(TEXT("无音频设备仍提交音效请求"), Audio->GetLastPlayedSFX(), ECastleSFXEvent::Button);

		FText Reason;
		if (!TestTrue(TEXT("正式语音剧本启动"), Dialogue->PlayScript(Opening, Player, Reason))) { AddError(Reason.ToString()); return true; }
		TestEqual(TEXT("首行选中首条录音"), Audio->GetCurrentVoiceSound(), Opening->Lines[0].VoiceLine.Get());
		Dialogue->AdvanceDialogueForAutomation();
		TestEqual(TEXT("快进切换语音，替换上一句"), Audio->GetCurrentVoiceSound(), Opening->Lines[1].VoiceLine.Get());
		for (int32 Index = 1; Index < Opening->Lines.Num(); ++Index) { Dialogue->AdvanceDialogueForAutomation(); }
		TestFalse(TEXT("剧本顺序播放结束"), Dialogue->IsDialoguePlaying());
		TestNull(TEXT("对话正常结束停止语音"), Audio->GetCurrentVoiceSound());
		if (!TestTrue(TEXT("重新开始以验证取消"), Dialogue->PlayScript(Opening, Player, Reason))) { AddError(Reason.ToString()); return true; }
		Dialogue->CancelForController(*Player);
		TestNull(TEXT("控制器取消立即停止语音"), Audio->GetCurrentVoiceSound());

		auto* Temporary = NewObject<UCastleDialogueScriptDataAsset>();
		auto& Line = Temporary->Lines.AddDefaulted_GetRef();
		Line.LineId = TEXT("OptionalVoice");
		Line.SpeakerName = INVTEXT("测试发言人");
		Line.TextTemplate = INVTEXT("这句临时台词没有录音。");
		Line.bEndsScript = true;
		AddExpectedMessage(TEXT("存在未配置语音的台词"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestTrue(TEXT("临时剧本校验成功并记录一次预期 Warning"), Temporary->ValidateScript(Reason));
		if (!TestTrue(TEXT("临时剧本无语音仍显示"), Dialogue->PlayScript(Temporary, Player, Reason))) { AddError(Reason.ToString()); return true; }
		TestNull(TEXT("临时剧本无语音保持语音通道空白"), Audio->GetCurrentVoiceSound());
		Dialogue->AdvanceDialogueForAutomation();

		TestTrue(TEXT("主塔真实死亡触发游戏结束"), MainTower->ApplyDamage(MainTower->CurrentHealth));
		TestEqual(TEXT("正式导演切换到 GameOver"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
		TestEqual(TEXT("GameOver 停止当前音乐"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::None);
		TestEqual(TEXT("主塔死亡提交失败音效"), Audio->GetLastPlayedSFX(), ECastleSFXEvent::Defeat);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(CastlePIEAutomation::StopPIE()));
	return true;
}

#endif
