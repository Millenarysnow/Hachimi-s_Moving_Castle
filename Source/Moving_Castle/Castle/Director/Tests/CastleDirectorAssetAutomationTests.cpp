// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSettings.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"

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
	TestEqual(TEXT("正式导演配置引用 Overview 圆形传送门 8 的 Light 效果"), Config->NodePortalSystem->GetPathName(),
		FString(TEXT("/Game/Portals_VFXPack/Particles/P_Light_SpherePortal.P_Light_SpherePortal")));
	// 复用正式资产上的依赖，只在临时副本上覆盖节点权重和数量。
	UCastleDirectorConfigDataAsset* WeightConfig = DuplicateObject(Config, GetTransientPackage());
	WeightConfig->NodeCountMin = 2;
	WeightConfig->NodeCountMax = 3;
	WeightConfig->NormalNodeWeight = 0;
	WeightConfig->RewardNodeWeight = 0;
	WeightConfig->RestNodeWeight = 1;
	AddExpectedError(TEXT("普通与奖励权重之和须大于零"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("最多三个节点时拒绝只抽休息岛"), WeightConfig->ValidateConfig(Reason));
	WeightConfig->NodeCountMax = 2;
	TestTrue(TEXT("固定两个节点时允许只抽休息岛"), WeightConfig->ValidateConfig(Reason));
	WeightConfig->NodeCountMax = 3;
	WeightConfig->NormalNodeWeight = 1;
	TestTrue(TEXT("最多三个节点时允许普通权重为正且奖励为零"), WeightConfig->ValidateConfig(Reason));
	WeightConfig->NormalNodeWeight = 0;
	WeightConfig->RewardNodeWeight = 1;
	TestTrue(TEXT("最多三个节点时允许奖励权重为正且普通为零"), WeightConfig->ValidateConfig(Reason));
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
	UCastleDirectorNodeLabelWidget* NodeLabel = CreateWidget<UCastleDirectorNodeLabelWidget>(World, Config->NodeLabelWidgetClass);
	TestTrue(TEXT("HUD必需控件均绑定到C++字段"), HUD && HUD->HasRequiredBindings());
	TestTrue(TEXT("奖励必需控件均绑定到C++字段"), Reward && Reward->HasRequiredBindings());
	TestTrue(TEXT("结束界面必需控件均绑定到C++字段"), GameOver && GameOver->HasRequiredBindings());
	TestTrue(TEXT("节点标签必需LabelText绑定到C++字段"), NodeLabel && NodeLabel->HasRequiredBindings());
	if (NodeLabel && NodeLabel->HasRequiredBindings())
	{
		const FText ExpectedText = FText::FromString(TEXT("精英战斗 · 第 5 关"));
		FText ActualText;
		FLinearColor ActualColor;
		TestTrue(TEXT("精英标签写入文字"), NodeLabel->SetNodeLabel(ExpectedText, FLinearColor::Red));
		TestTrue(TEXT("精英标签读取成功"), NodeLabel->TryGetNodeLabel(ActualText, ActualColor));
		TestEqual(TEXT("屏幕空间标签显示正确关卡文字"), ActualText.ToString(), ExpectedText.ToString());
		TestTrue(TEXT("精英标签使用红色"), ActualColor.Equals(FLinearColor::Red));
		NodeLabel->SetNodeLabel(FText::FromString(TEXT("普通战斗 · 第 1 关")), FLinearColor::White);
		TestTrue(TEXT("普通标签读取成功"), NodeLabel->TryGetNodeLabel(ActualText, ActualColor));
		TestTrue(TEXT("普通标签使用白色"), ActualColor.Equals(FLinearColor::White));
	}
	return true;
}
#endif
