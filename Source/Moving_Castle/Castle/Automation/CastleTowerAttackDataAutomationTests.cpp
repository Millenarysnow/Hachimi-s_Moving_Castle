// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleTowerAttackDataAsset.h"

#include "Castle/Actors/CastleBuildingBase.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "MassEntityConfigAsset.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileTrait.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"

#include <limits>

namespace CastleTowerAttackDataTests
{
	/** 测试显式提供五个等级和非空 Mass 配置，不依赖生产 Data Asset 的默认值。 */
	UCastleTowerAttackDataAsset* MakeValidAttackData()
	{
		UCastleTowerAttackDataAsset* Data = NewObject<UCastleTowerAttackDataAsset>();
		Data->ProjectileConfiguration = NewObject<UMassEntityConfigAsset>(Data);
		Data->ProjectileConfiguration->GetMutableConfig().AddTrait(
			*NewObject<UCastleMassHomingProjectileTrait>(Data->ProjectileConfiguration));
		for (int32 Level = 1; Level <= 5; ++Level)
		{
			FCastleTowerAttackLevelData Entry;
			Entry.Level = Level;
			Entry.AttacksPerSecond = 1.0f;
			Entry.ProjectileDamage = 10.0f;
			Entry.AttackRadius = 1000.0f;
			Data->Levels.Add(Entry);
		}
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerAttackDataValidationTest,
	"MovingCastle.BuildingEffects.TowerAttackDataValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTowerAttackDataValidationTest::RunTest(const FString& Parameters)
{
	using namespace CastleTowerAttackDataTests;
	UCastleTowerAttackDataAsset* Data = MakeValidAttackData();
	FText FailureReason;
	TestTrue(TEXT("配置完整的 1 至 5 级塔攻击资产有效"), Data->ValidateConfiguration(FailureReason));
	FDataValidationContext ValidContext;
	TestTrue(TEXT("内容浏览器校验采用相同规则"), Data->IsDataValid(ValidContext) == EDataValidationResult::Valid);
	TestTrue(TEXT("读取当前等级时返回精确行"), Data->FindLevel(3) == &Data->Levels[2]);
	TestNull(TEXT("未配置的等级不会回退到其他行"), Data->FindLevel(6));

	Data->Levels[4].Level = 4;
	TestFalse(TEXT("重复等级被拒绝"), Data->ValidateConfiguration(FailureReason));
	TestTrue(TEXT("重复等级错误指出原因"), FailureReason.ToString().Contains(TEXT("重复配置")));
	Data->Levels[4].Level = 5;

	Data->Levels.RemoveAt(2);
	TestFalse(TEXT("缺失等级被拒绝"), Data->ValidateConfiguration(FailureReason));
	TestTrue(TEXT("缺级错误指出具体等级"), FailureReason.ToString().Contains(TEXT("缺少 3 级")));
	Data->Levels.Insert(FCastleTowerAttackLevelData(), 2);
	Data->Levels[2].Level = 3;
	Data->Levels[2].AttacksPerSecond = 1.0f;
	Data->Levels[2].ProjectileDamage = 10.0f;
	Data->Levels[2].AttackRadius = 1000.0f;

	Data->Levels[0].AttacksPerSecond = 0.0f;
	TestFalse(TEXT("非正攻击速度被拒绝"), Data->ValidateConfiguration(FailureReason));
	Data->Levels[0].AttacksPerSecond = 1.0f;
	Data->Levels[0].ProjectileDamage = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("非有限子弹伤害被拒绝"), Data->ValidateConfiguration(FailureReason));
	Data->Levels[0].ProjectileDamage = 10.0f;
	Data->Levels[0].AttackRadius = -1.0f;
	TestFalse(TEXT("负攻击半径被拒绝"), Data->ValidateConfiguration(FailureReason));
	Data->Levels[0].AttackRadius = 1000.0f;

	Data->ProjectileConfiguration = nullptr;
	TestFalse(TEXT("空子弹 Mass 配置被拒绝"), Data->ValidateConfiguration(FailureReason));
	TestTrue(TEXT("空配置错误指出资产路径"), FailureReason.ToString().Contains(Data->GetPathName()));
	FDataValidationContext MissingProjectileContext;
	TestTrue(TEXT("内容浏览器拒绝无子弹配置的资产"), Data->IsDataValid(MissingProjectileContext) == EDataValidationResult::Invalid);
	Data->ProjectileConfiguration = NewObject<UMassEntityConfigAsset>(Data);
	TestFalse(TEXT("缺失追踪 Trait 的 Mass 配置被拒绝"), Data->ValidateConfiguration(FailureReason));

	UCastleBuildingDefinition* ArcherDefinition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	TestTrue(TEXT("箭塔定义必须引用有效攻击配置"), ArcherDefinition->ValidateConfiguration(FailureReason));
	ArcherDefinition->AttackData = nullptr;
	TestFalse(TEXT("箭塔定义缺少攻击资产时明确失败"), ArcherDefinition->ValidateConfiguration(FailureReason));
	UCastleBuildingDefinition* RoadDefinition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::Road);
	TestTrue(TEXT("道路定义允许没有塔攻击资产"), RoadDefinition->AttackData == nullptr && RoadDefinition->ValidateConfiguration(FailureReason));
	return true;
}

/** 正式箭塔定义与追踪箭资产必须在内容目录中接好精确等级参数。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleArcherTowerAttackAssetTest,
	"MovingCastle.BuildingEffects.ArcherTowerAttackAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleArcherTowerAttackAssetTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTower.DA_ArcherTower"));
	if (!TestNotNull(TEXT("正式箭塔建筑定义存在"), Definition)) { return false; }

	UCastleTowerAttackDataAsset* Data = Definition->AttackData;
	if (!TestNotNull(TEXT("正式箭塔定义引用攻击参数资产"), Data)) { return false; }
	FText FailureReason;
	TestTrue(TEXT("正式箭塔攻击与等级配置通过运行时校验"), Definition->ValidateConfiguration(FailureReason));
	TestTrue(TEXT("攻击资产使用 Mass 追踪箭配置"), Data->ProjectileConfiguration != nullptr
		&& Data->ProjectileConfiguration->FindTrait(UCastleMassHomingProjectileTrait::StaticClass()) != nullptr);
	TestEqual(TEXT("发射点高度偏移为显式配置值"), Data->LaunchHeightOffset, 300.0f);
	TestEqual(TEXT("攻击资产包含 1 至 5 级"), Data->Levels.Num(), 5);
	for (int32 Index = 0; Index < Data->Levels.Num(); ++Index)
	{
		const FCastleTowerAttackLevelData& Entry = Data->Levels[Index];
		const FCastleBuildingLevelDefinition* BuildingLevel = Definition->FindLevel(Entry.Level);
		if (!TestNotNull(TEXT("正式箭塔定义存在对应等级类"), BuildingLevel)) { continue; }
		UClass* BuildingClass = BuildingLevel->BuildingClass.Get();
		if (!TestNotNull(TEXT("正式箭塔等级类已加载"), BuildingClass)) { continue; }
		ACastleBuildingBase* Defaults = Cast<ACastleBuildingBase>(BuildingClass->GetDefaultObject());
		if (!TestNotNull(TEXT("正式箭塔等级类 CDO 存在"), Defaults)) { continue; }
		TestTrue(FString::Printf(TEXT("%d 级 CDO 的 BuildingEffect 是通用塔攻击组件"), Entry.Level),
			IsValid(Defaults->BuildingEffect) && Defaults->BuildingEffect->IsA<UCastleTowerAttackEffectComponent>());
		TArray<UCastleBuildingEffectComponent*> EffectComponents;
		Defaults->GetComponents<UCastleBuildingEffectComponent>(EffectComponents);
		TestEqual(FString::Printf(TEXT("%d 级 CDO 只有一个建筑效果组件"), Entry.Level), EffectComponents.Num(), 1);
		if (Entry.Level == 1 || Entry.Level == 2 || Entry.Level == 4)
		{
			TestTrue(FString::Printf(TEXT("%d 级保留现有箭塔蓝图类"), Entry.Level),
				BuildingClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
		}
		TestEqual(TEXT("等级行顺序和等级值一致"), Entry.Level, Index + 1);
		TestEqual(TEXT("策划案每秒一箭"), Entry.AttacksPerSecond, 1.0f);
		TestEqual(TEXT("单发伤害按等级递增"), Entry.ProjectileDamage, static_cast<float>((Index + 1) * 5 + 5));
		TestEqual(TEXT("攻击半径按等级递增"), Entry.AttackRadius, static_cast<float>(1200 + (Index + 1) * 200));
	}
	return true;
}

#endif
