#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Input/CastleControlMode.h"
#include "Castle/MainMenu/CastleMenuController.h"
#include "Castle/MainMenu/CastleMenuStage.h"
#include "CastlePlayerController.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "UnrealClient.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleStartMenuSessionTest, "MovingCastle.UI.StartMenu.Session",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleStartMenuSessionTest::RunTest(const FString& Parameters)
{
	using namespace CastlePIEAutomation;
	constexpr const TCHAR* StartMap = TEXT("/Game/MovingCastle/Maps/StartMenu");
	constexpr const TCHAR* Section = TEXT("MovingCastle.ControlSettings");
	constexpr const TCHAR* Key = TEXT("ControlMode");
	FString PriorMode;
	const bool bHadMode = GConfig->GetString(Section, Key, PriorMode, GGameUserSettingsIni);
	if (!StartPIE(*this, StartMap, FIntPoint(1280, 830))) { return false; }
	const FString Screenshots = FPaths::ProjectSavedDir() / TEXT("WorkerC/Screenshots");
	IFileManager::Get().MakeDirectory(*Screenshots, true);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Screenshots, Phase = 0, CaptureFrames = 0,
		Deadline = FPlatformTime::Seconds() + 90.0]() mutable
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			AddError(FString::Printf(TEXT("开始菜单换图在阶段 %d 超时。"), Phase));
			return true;
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World) { return false; }
		APlayerController* Player = World->GetFirstPlayerController();
		auto* Session = World->GetGameInstance()->GetSubsystem<UCastleMenuSessionSubsystem>();
		if (!TestNotNull(TEXT("PIE 已注册菜单会话子系统"), Session)) { return true; }
		if (Phase == 3)
		{
			auto* MainPlayer = Cast<ACastlePlayerController>(Player);
			if (!MainPlayer || !World->GetMapName().EndsWith(TEXT("MainMap"))) { return false; }
			FString StoredMode;
			const bool bLoadedMode = GConfig->GetString(Section, Key, StoredMode, GGameUserSettingsIni);
			TestTrue(TEXT("确认模式后立即进入主地图"), World->GetMapName().EndsWith(TEXT("MainMap")));
			TestTrue(TEXT("确认的设计模式已写入既有设置入口"), bLoadedMode && StoredMode == TEXT("Design"));
			UGameplayStatics::OpenLevel(World, FName(TEXT("/Game/MovingCastle/Maps/StartMenu")));
			++Phase;
			return false;
		}
		auto* Menu = Cast<ACastleMenuController>(Player);
		if (!Menu || !Menu->IsMenuReady()) { return false; }
		if (Phase == 0)
		{
			TActorIterator<ACastleMenuStage> Stage(World);
			if (!TestTrue(TEXT("开始地图的舞台已绑定 14 张切图"), Stage && Stage->MenuTextures.Num() == 14)) { return true; }
			for (const UTexture2D* Texture : Stage->MenuTextures)
			{
				TestTrue(TEXT("每张贴图引用均指向 HachimisKit"), IsValid(Texture)
					&& Texture->GetPathName().StartsWith(TEXT("/Game/MovingCastle/UI/MainMenu/HachimisKit/")));
			}
			TestFalse(TEXT("首次选择标记初始为假"), Session->bHasChosenControlModeThisSession);
			if (++CaptureFrames < 8) { return false; }
			// 比较 Slate 实际布局值，避免仅验证资源引用而漏掉热区被放大或偏移的情况。
			const TSharedPtr<SWidget> PlayButton = Menu->GetPlayButtonForAutomation();
			if (!TestTrue(TEXT("开始游戏按钮已构建"), PlayButton.IsValid())) { return true; }
			const FVector2D ButtonSize = PlayButton->GetCachedGeometry().GetLocalSize();
			TestTrue(TEXT("开始游戏热区使用设计尺寸"), ButtonSize.Equals(FVector2D(333, 60)));
			TestTrue(TEXT("默认键盘焦点位于开始游戏"), PlayButton->HasKeyboardFocus());
			FScreenshotRequest::RequestScreenshot(Screenshots / TEXT("StartMenu.png"), true, false);
			CaptureFrames = 0;
			++Phase;
			return false;
		}
		if (Phase == 1)
		{
			Menu->Choose(ECastleMenuAction::Play);
			TestTrue(TEXT("首次点击开始只打开模式选择层"), Menu->bModeSelectionOpen && !Menu->bLeaving);
			Menu->CancelControlMode();
			TestFalse(TEXT("取消后关闭模态且仍未确认"), Menu->bModeSelectionOpen || Session->bHasChosenControlModeThisSession);
			Menu->Choose(ECastleMenuAction::Play);
			TestTrue(TEXT("取消后下次点击仍弹出选择"), Menu->bModeSelectionOpen);
			++Phase;
			return false;
		}
		if (Phase == 2)
		{
			if (++CaptureFrames == 5)
			{
				FScreenshotRequest::RequestScreenshot(Screenshots / TEXT("StartMenu-ControlMode.png"), true, false);
			}
			if (CaptureFrames < 8) { return false; }
			Menu->ConfirmControlMode(ECastleControlMode::Design);
			TestTrue(TEXT("确认设置会话标记并请求进入正式地图"), Session->bHasChosenControlModeThisSession && Menu->bLeaving);
			TestFalse(TEXT("确认后收起模式面板"), Menu->bModeSelectionOpen);
			++Phase;
			return false;
		}
		// 经过实际 MainMap → StartMenu 旅行，验证新控制器仍读取同一个会话的标记。
		Menu->Choose(ECastleMenuAction::Play);
		TestTrue(TEXT("同一次运行返回后直接再次进入正式地图"), Menu->bLeaving);
		TestFalse(TEXT("第二次开始不挂载模式面板"), Menu->bModeSelectionOpen);
		return true;
	}));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(StopPIE()));
	// 测试通过实际保存入口验证整条路径。恢复运行前的用户设置键值，避免测试更改工作区预设。
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([PriorMode, bHadMode, Section, Key]()
	{
		if (bHadMode) { GConfig->SetString(Section, Key, *PriorMode, GGameUserSettingsIni); }
		else { GConfig->RemoveKey(Section, Key, GGameUserSettingsIni); }
		GConfig->Flush(false, GGameUserSettingsIni);
		return true;
	}));
	return true;
}

#endif
