// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Tests/AutomationCommon.h"


namespace
{
 /** 在编辑器完成地图加载并启动PIE后检查正式开局，不保存地图。 */
 class FCheckDirectorMainMap final : public IAutomationLatentCommand
 {
 public:
  explicit FCheckDirectorMainMap(FAutomationTestBase* InTest) : Test(InTest) {}
  virtual bool Update() override
  {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World()) { World = Context.World(); break; }
	}
	if (!Test->TestNotNull(TEXT("独立游戏世界存在"), World)) { return true; }
	Test->TestTrue(TEXT("当前确实加载MainMap"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()).EndsWith(TEXT("/MainMap")));
	UCastleDirectorSubsystem* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
	if (!Test->TestTrue(TEXT("真实MainMap导演已自动启动"), Director && Director->IsRunning())) { return true; }
	Test->TestEqual(TEXT("真实开局为编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
	Test->TestEqual(TEXT("开局等级为1"), Director->GetLevel(), 1);
	Test->TestEqual(TEXT("开局经验为0"), Director->GetExperience(), 0);
	int32 NodeCount = 0;
	for (TActorIterator<ACastleDirectorNodeActor> It(World); It; ++It) { ++NodeCount; }
	Test->TestTrue(TEXT("真实世界中生成2至3个节点"), NodeCount >= 2 && NodeCount <= 3);
	FText Reason;
	Test->TestFalse(TEXT("开局没有商店权限"), Director->IsShopAccessAllowed(Reason));
	ACastlePlayerController* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
	if (!Test->TestNotNull(TEXT("本地控制器类型正确"), Controller)) { return true; }
	Controller->ToggleShop();
	Test->TestFalse(TEXT("真实B键入口拒绝开店"), Controller->GetShop()->IsShopOpen());
	Test->TestTrue(TEXT("真实HUD房间计数是10/15"), Director->GetHUDSummary().Contains(TEXT("房间 10/15")));
	FOutputDeviceNull Output;
	Test->TestTrue(TEXT("真实伤害命令可执行"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleDamageMainTower 99999"), Output, World));
	Test->TestEqual(TEXT("主塔死亡触发正式游戏结束"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
	Test->TestTrue(TEXT("世界已暂停"), World->IsPaused());
	Test->TestTrue(TEXT("游戏结束鼠标可见"), Controller->bShowMouseCursor);
	Test->TestTrue(TEXT("游戏结束已锁定移动"), Controller->IsMoveInputIgnored());
	Test->TestTrue(TEXT("游戏结束已锁定视角"), Controller->IsLookInputIgnored());
	Test->AddInfo(TEXT("MainMap启动、节点、HUD、商店门控、正式主塔死亡与暂停路径已验证；字体中文显示另行验收。"));
	return true;
  }
 private:
  FAutomationTestBase* Test;
 };
 /** 只停止本测试启动的PIE，等待世界销毁后才把控制权交回自动化队列。 */
 class FStopDirectorMainMap final : public IAutomationLatentCommand
 {
 public:
  explicit FStopDirectorMainMap(FAutomationTestBase* InTest) : Test(InTest) {}
  virtual bool Update() override
  {
   if (!AutomationCommon::GetAnyGameWorld()) { return true; }
   if (!bRequested)
   {
    UClass* ToolClass = FindObject<UClass>(nullptr, TEXT("/Script/EditorToolset.EditorAppToolset"));
    UFunction* Stop = ToolClass ? ToolClass->FindFunctionByName(TEXT("StopPIE")) : nullptr;
    if (!Stop) { Test->AddError(TEXT("官方StopPIE工具不可用，无法结束本测试PIE。")); return true; }
    struct FStopParams { UObject* ReturnValue = nullptr; } Params;
    ToolClass->GetDefaultObject()->ProcessEvent(Stop, &Params);
    bRequested = true;
    StartedAt = FPlatformTime::Seconds();
   }
   if (FPlatformTime::Seconds() - StartedAt > 15.0) { Test->AddError(TEXT("PIE结束等待超时。")); return true; }
   return false;
  }
 private:
  FAutomationTestBase* Test;
  bool bRequested = false;
  double StartedAt = 0.0;
 };
}

/** 真实MainMap通过编辑器加载和PIE验证，避免未烘焙-game进程缺失外部Actor的差异。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorMainMapTest, "MovingCastle.Director.MainMapSmoke",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorMainMapTest::RunTest(const FString& Parameters)
{
 if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
 ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckDirectorMainMap(this));
 ADD_LATENT_AUTOMATION_COMMAND(FStopDirectorMainMap(this));
 return true;
}
#endif
