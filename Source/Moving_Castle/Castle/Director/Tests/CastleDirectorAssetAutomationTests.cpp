// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSettings.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Font.h"
#include "Engine/World.h"

/** 重载正式资产并创建实例，不能只用 WidgetTree 的名字清单冒充 BindWidget 已成功。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorAssetsTest, "MovingCastle.Director.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorAssetsTest::RunTest(const FString& Parameters)
{
	const UCastleDirectorSettings* Settings = GetDefault<UCastleDirectorSettings>();
	UCastleDirectorConfigDataAsset* Config = Settings->DirectorConfig.LoadSynchronous();
	if (!TestNotNull(TEXT("设置引用的正式导演DA可加载"), Config)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("正式导演DA配置通过校验"), Config->ValidateConfig(Reason))) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("自动启动地图只有一张"), Settings->AutoStartMaps.Num(), 1);
	TestEqual(TEXT("自动启动白名单是MainMap"), Settings->AutoStartMaps[0].GetLongPackageName(), FString(TEXT("/Game/MovingCastle/Maps/MainMap")));
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("创建界面验证世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCastleDirectorHUDWidget* HUD = CreateWidget<UCastleDirectorHUDWidget>(World, Config->HUDWidgetClass);
	UCastleRewardChoiceWidget* Reward = CreateWidget<UCastleRewardChoiceWidget>(World, Config->RewardWidgetClass);
	UCastleGameOverWidget* GameOver = CreateWidget<UCastleGameOverWidget>(World, Config->GameOverWidgetClass);
	TestTrue(TEXT("HUD必需控件均绑定到C++字段"), HUD && HUD->HasRequiredBindings());
	TestTrue(TEXT("奖励必需控件均绑定到C++字段"), Reward && Reward->HasRequiredBindings());
	TestTrue(TEXT("结束界面必需控件均绑定到C++字段"), GameOver && GameOver->HasRequiredBindings());
	// 此处只输出默认离线字库的实际证据，不把缺字当作已解决的中文标签验收。
	UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
	if (TestNotNull(TEXT("字体查证必须加载实际默认离线字库"), Font))
	{
		const bool bHasChinese = Font->IsRemapped ? Font->CharRemap.Contains(static_cast<uint16>(TEXT('普')))
			: Font->Characters.IsValidIndex(static_cast<int32>(TEXT('普')));
		AddInfo(FString::Printf(TEXT("默认节点字体 %s 的中文字形“普”覆盖=%s，IsRemapped=%d，字形数=%d。"),
			*Font->GetPathName(), bHasChinese ? TEXT("true") : TEXT("false"), Font->IsRemapped, Font->Characters.Num()));
	}
	return true;
}
#endif
