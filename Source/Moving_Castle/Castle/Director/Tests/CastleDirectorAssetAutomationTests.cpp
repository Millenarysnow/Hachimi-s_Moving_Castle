// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSettings.h"
#include "Castle/SkyIslands/CastleSkyIslandWeightsDataAsset.h"
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
	TestEqual(TEXT("正式浮空门户缩放"), Config->NodePortalScale, 2.5f);
	TestEqual(TEXT("正式开局待命 NPC"), Config->InitialNPCCount, 1);
	TestEqual(TEXT("正式精英关间隔"), Config->EliteEveryNSteps, 10);
	const auto* IslandWeights = Config->SkyIslandWeights.Get();
	TestEqual(TEXT("正式导演引用独立权重配置"), GetPathNameSafe(IslandWeights),
		FString(TEXT("/Game/MovingCastle/Data/SkyIslands/DA_SkyIslandWeights.DA_SkyIslandWeights")));
	const int32 ExpectedReward[] = {2, 1, 1, 0};
	const int32 ExpectedBonus[] = {2, 1, 1, 1};
	TestTrue(TEXT("正式奖励节点权重 2:1:1"), IslandWeights->RewardShopWeight == 2
		&& IslandWeights->RewardNPCWeight == 1 && IslandWeights->RewardGambleWeight == 1);
	TestTrue(TEXT("正式房间传送门权重 2:1:1:1"), IslandWeights->BonusShopWeight == 2
		&& IslandWeights->BonusNPCWeight == 1 && IslandWeights->BonusGambleWeight == 1
		&& IslandWeights->BonusRestWeight == 1);
	// 使用固定种子验证分布和复现能力，四类逐项核对理论比例。
	for (const bool bBonus : {false, true})
	{
		FRandomStream Random(2038);
		FRandomStream Repeat(2038);
		const auto Select = [&](FRandomStream& Stream, ECastleSkyIslandType& Type)
		{
			return bBonus ? IslandWeights->TrySelectBonusSkyIsland(Stream, Type, Reason)
				: IslandWeights->TrySelectRewardSkyIsland(Stream, Type, Reason);
		};
		int32 Counts[4] = {};
		constexpr int32 Samples = 20000;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			ECastleSkyIslandType Type, RepeatedType;
			if (!Select(Random, Type) || !Select(Repeat, RepeatedType) || Type != RepeatedType
				|| static_cast<uint8>(Type) >= UE_ARRAY_COUNT(Counts))
			{
				AddError(TEXT("同种子必须逐次抽到同一合法类型。"));
				return false;
			}
			++Counts[static_cast<uint8>(Type)];
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Counts); ++Index)
		{
			const double Expected = bBonus ? ExpectedBonus[Index] / 5.0 : ExpectedReward[Index] / 4.0;
			TestTrue(TEXT("空岛分布接近配置权重"), FMath::Abs(Counts[Index] / double(Samples) - Expected) < 0.02);
		}
	}
	auto* InvalidWeights = DuplicateObject(IslandWeights, GetTransientPackage());
	FRandomStream Random(1);
	ECastleSkyIslandType Type = ECastleSkyIslandType::Rest;
	InvalidWeights->RewardShopWeight = 1;
	InvalidWeights->RewardNPCWeight = InvalidWeights->RewardGambleWeight = 0;
	for (int32 Seed = 0; Seed < 16; ++Seed)
	{
		FRandomStream SingleType(Seed);
		TestTrue(TEXT("单一非零权重总是选中商店"), InvalidWeights->TrySelectRewardSkyIsland(SingleType, Type, Reason)
			&& Type == ECastleSkyIslandType::Shop);
	}
	InvalidWeights->RewardShopWeight = 0;
	AddExpectedError(TEXT("奖励节点权重无效"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("拒绝全零奖励权重"), InvalidWeights->TrySelectRewardSkyIsland(Random, Type, Reason));
	InvalidWeights->RewardShopWeight = 1;
	InvalidWeights->BonusRestWeight = -1;
	AddExpectedError(TEXT("房间传送门权重无效"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("拒绝负权重"), InvalidWeights->TrySelectBonusSkyIsland(Random, Type, Reason));
	TestEqual(TEXT("拒绝不消耗随机流"), Random.GetCurrentSeed(), 1);

	// 复用正式资产上的依赖，只在临时副本上覆盖节点权重和数量。
	UCastleDirectorConfigDataAsset* WeightConfig = DuplicateObject(Config, GetTransientPackage());
	// 强制验证正式导演依赖，随后复原副本以继续节点权重场景。
	WeightConfig->SkyIslandWeights = nullptr;
	AddExpectedError(TEXT("SkyIslandWeights"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("导演缺少必填空岛概率资产时拒绝启动"), WeightConfig->ValidateConfig(Reason));
	WeightConfig->SkyIslandWeights = Config->SkyIslandWeights;
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
