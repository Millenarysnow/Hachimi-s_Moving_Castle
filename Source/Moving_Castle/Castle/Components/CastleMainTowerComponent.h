// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleMainTowerComponent.generated.h"

/** 主塔生命变化通知；伤害为零的攻击也会产生通知，便于原型阶段验收攻击事件。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FCastleMainTowerHealthChanged,
	float, PreviousHealth,
	float, CurrentHealth,
	float, Damage);

/**
 * 主塔的纯 C++ 生命组件。
 *
 * 主塔的模型可以继续由现有 Blueprint 提供，但生命、受击和失败判定数据
 * 由本组件统一保存，Mass 处理器只通过世界子系统调用它，不直接依赖模型蓝图。
 */
UCLASS(ClassGroup = (Castle), BlueprintType, meta = (BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleMainTowerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 创建组件并保留正常的 BeginPlay 初始化流程。 */
	UCastleMainTowerComponent();

	/** 主塔初始生命；棋盘生成时由战斗配置 Data Asset 写入。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔|生命", meta = (ClampMin = "0.01"))
	float MaxHealth = 1000.0f;

	/** 敌人飞行目标相对主塔 Actor 根节点的高度偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔|目标", meta = (Units = "cm"))
	float TargetHeightOffset = 250.0f;

	/** 当前生命；只允许本组件的受击入口修改。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "主塔|生命")
	float CurrentHealth = 0.0f;

	/** 已接收的攻击次数，包含伤害为零的攻击。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "主塔|生命")
	int32 AttackCount = 0;

	/** 生命变化通知；伤害为零时 PreviousHealth 与 CurrentHealth 相同。 */
	UPROPERTY(BlueprintAssignable, Category = "主塔|生命")
	FCastleMainTowerHealthChanged OnHealthChanged;

	/** 将当前生命重置为 MaxHealth；棋盘生成主塔时调用一次。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔")
	bool InitializeHealth();

	/** 对主塔造成非负伤害，生命会被限制在 [0, MaxHealth]。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔")
	bool ApplyDamage(float Damage);

	/** 返回敌人应该追踪的世界空间目标点。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|主塔")
	FVector GetTargetLocation() const;

	/** 返回主塔是否仍有生命。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|主塔")
	bool IsAlive() const { return CurrentHealth > 0.0f; }

protected:
	/** 组件进入游戏后校验并初始化生命，再注册到世界级主塔入口。 */
	virtual void BeginPlay() override;

	/** 组件退出时从世界级主塔入口注销，避免处理器持有过期指针。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

/**
 * 当前 World 的主塔注册入口。
 *
 * Mass 敌人处理器运行在游戏线程，可以通过这个子系统读取主塔目标位置并
 * 结算伤害；世界关闭或主塔销毁后注册会被清除，不保留跨关卡指针。
 */
UCLASS()
class MOVING_CASTLE_API UCastleMainTowerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只支持实际游戏和 PIE World，编辑器预览不注册主塔。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	/** 注册当前世界唯一的主塔组件；重复注册同一组件视为成功。 */
	bool RegisterMainTower(UCastleMainTowerComponent* Tower);

	/** 只允许注销当前已注册的同一个组件。 */
	bool UnregisterMainTower(UCastleMainTowerComponent* Tower);

	/** 读取敌人追踪的主塔目标点。 */
	bool GetMainTowerTargetLocation(FVector& OutLocation) const;

	/** 读取当前主塔生命，主塔不存在时返回 false。 */
	bool GetMainTowerHealth(float& OutCurrentHealth, float& OutMaxHealth) const;

	/** 记录一次攻击并结算伤害，伤害为零仍会增加主塔攻击计数。 */
	bool ApplyMainTowerDamage(float Damage);

	/** 供棋盘和自动化测试查询当前主塔组件。 */
	UCastleMainTowerComponent* GetRegisteredMainTower() const;

private:
	/** 只保存当前 World 内的弱引用；主塔 Actor 销毁后引用自动失效。 */
	TWeakObjectPtr<UCastleMainTowerComponent> RegisteredMainTower;
};
