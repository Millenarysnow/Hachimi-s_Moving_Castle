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
#include "MassVisualizationTrait.h"
#include "MassRepresentationTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

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
	UCastleBuildingDefinition* NormalRoomDefinition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::NormalRoom);
	UCastleBuildingDefinition* CannonDefinition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::CannonTower);
	TestTrue(TEXT("炮塔定义要求有效攻击数据"), CannonDefinition->ValidateConfiguration(FailureReason));
	CannonDefinition->AttackData = nullptr;
	TestFalse(TEXT("炮塔定义缺攻击 DA 明确失败"), CannonDefinition->ValidateConfiguration(FailureReason));
	TestTrue(TEXT("普通房间定义允许没有塔攻击资产"), NormalRoomDefinition->AttackData == nullptr && NormalRoomDefinition->ValidateConfiguration(FailureReason));
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
		TestTrue(FString::Printf(TEXT("%d 级均显式配置箭塔蓝图类"), Entry.Level),
			BuildingClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
		TestEqual(TEXT("等级行顺序和等级值一致"), Entry.Level, Index + 1);
		TestEqual(TEXT("保持已查证的各等级实际攻速"), Entry.AttacksPerSecond, Entry.Level == 1 ? 10.0f : 1.0f);
		TestEqual(TEXT("单发伤害按等级递增"), Entry.ProjectileDamage, static_cast<float>((Index + 1) * 5 + 5));
		TestEqual(TEXT("保持已查证的各等级实际半径"), Entry.AttackRadius, Entry.Level == 1 ? 3000.0f : static_cast<float>(1200 + (Index + 1) * 200));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCannonAttackAssetTest, "MovingCastle.BuildingEffects.CannonAttackAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCannonAttackAssetTest::RunTest(const FString& Parameters)
{
	auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, TEXT("/Game/MovingCastle/Data/Buildings/DA_CannonTower.DA_CannonTower"));
	if (!TestNotNull(TEXT("正式炮塔定义存在"), Definition)) { return false; }
	FText Reason;
	TestTrue(TEXT("炮塔五级定义与攻击配置有效"), Definition->ValidateConfiguration(Reason));
	auto* Attack = Definition->AttackData.Get();
	if (!TestNotNull(TEXT("正式炮塔引用专用攻击 DA"), Attack)) { return false; }
	TestEqual(TEXT("炮塔专用攻击资产路径"), Attack->GetPathName(), FString(TEXT("/Game/MovingCastle/Data/Buildings/DA_CannonTowerAttack.DA_CannonTowerAttack")));
	TestEqual(TEXT("炮塔发射偏移为260"), Attack->LaunchHeightOffset, 260.0f);
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		const auto* Row = Attack->FindLevel(Level);
		if (!TestNotNull(TEXT("每个精确等级均存在"), Row)) { return false; }
		TestEqual(TEXT("炮塔每秒一发"), Row->AttacksPerSecond, 1.0f);
		TestEqual(TEXT("每点数字十点伤害"), Row->ProjectileDamage, 10.0f);
		TestEqual(TEXT("炮塔射程2800"), Row->AttackRadius, 2800.0f);
	}
	auto* Config = Attack->ProjectileConfiguration.Get();
	if (!TestNotNull(TEXT("专用炮弹 Mass 配置存在"), Config)) { return false; }
	const auto* Projectile = Cast<UCastleMassHomingProjectileTrait>(Config->FindTrait(UCastleMassHomingProjectileTrait::StaticClass()));
	if (!TestNotNull(TEXT("炮弹有追踪 Trait"), Projectile)) { return false; }
	TestEqual(TEXT("炮弹速度1500"), Projectile->Movement.MaxSpeed, 1500.0f);
	TestEqual(TEXT("炮弹半径24"), Projectile->Radius, 24.0f);
	TestEqual(TEXT("炮弹寿命6秒"), Projectile->Lifetime, 6.0f);
	const auto* Visual = Cast<UMassVisualizationTrait>(Config->FindTrait(UMassVisualizationTrait::StaticClass()));
	if (!TestNotNull(TEXT("炮弹有实例化显示 Trait"), Visual)) { return false; }
	const auto& Meshes = Visual->StaticMeshInstanceDesc.Meshes;
	if (!TestEqual(TEXT("炮弹只有球体实例描述"), Meshes.Num(), 1)) { return false; }
	TestTrue(TEXT("炮弹使用显式球体资源"), Meshes[0].Mesh && Meshes[0].Mesh->GetPathName() == TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestEqual(TEXT("炮弹显式覆盖深金橙材质"), Meshes[0].MaterialOverrides.Num(), 1)) { return false; }
	FLinearColor Color;
	TestTrue(TEXT("炮弹颜色参数存在且为深金橙色"), Meshes[0].MaterialOverrides[0]->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EnemyColor")), Color)
		&& Color.Equals(FLinearColor(0.9f, 0.32f, 0.035f)));
	return true;
}

#endif
