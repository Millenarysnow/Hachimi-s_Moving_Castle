// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleMainTowerComponent.generated.h"

class UCastleMainTowerBalloonComponent;

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

	/** 气球上限随塔的等级变化；血量映射与气球回收共用本组件的数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔|气球")
	int32 MaxBalloons = 5;

	/** 升级与气球回收使用的基础生命单位，不受额外生命倍率影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔|气球")
	float HealthPerBalloon = 200.0f;

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

	/** 敌方攻击专用入口；致命伤害在提交前由徽章运行时检查气球扣。 */
	bool ApplyEnemyDamage(float Damage);

	/** 按指定生命值回血；不改变 AttackCount，并用负伤害广播实际恢复量。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔")
	bool ApplyHealing(float Amount);

	/** 升级先验证逻辑容量与生命；棋盘扩层事务按层数准备对应的视觉套件。 */
	bool PrepareTowerBalloonTier(int32 NewMaxBalloons, float NewBaseMaxHealth, bool bFullHeal, FText& OutReason);
	/** 棋盘扩层成功后提交数值，并向新套件同步真实生命比例。 */
	void CommitPreparedBalloonTier(int32 NewMaxBalloons, float NewBaseMaxHealth, bool bFullHeal);
	/** 棋盘扩层失败时撤下预建资源，现有数值与可见气球保持原状。 */
	void DiscardPreparedBalloonTier();

	/** 更新气球等级和基础最大生命，可由升级调用方选择同时补满。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔")
	bool SetTowerBalloonTier(int32 NewMaxBalloons, float NewBaseMaxHealth, bool bFullHeal, FText& OutReason);

	/** 以基础最大生命为基准设置绝对倍率，重复应用同一倍率不会累乘。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔")
	bool SetMaxHealthMultiplier(float Multiplier, bool bScaleCurrentHealth = true);

	/** 按每球基础生命扣血，只有扣完仍存活且至少留一个气球时才提交。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔")
	bool TryConsumeBalloons(int32 BalloonCount, FText& OutReason);

	bool TryConsumeBalloonHealth(int32 BalloonCount, FText& OutFailureReason)
	{
		return TryConsumeBalloons(BalloonCount, OutFailureReason);
	}

	/** 返回与当前生命同步的存活数量和本等级气球容量。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|主塔")
	void GetBalloonCounts(int32& OutCurrentBalloons, int32& OutMaxBalloons) const;

	/** 棋盘完成初始整塔和锚点布置后接入吊挂套件表现。 */
	bool AttachBalloonPresentation(UCastleMainTowerBalloonComponent* Presentation, FText& OutReason);

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

private:
	/** 保留未乘徽章倍率的上限；表现组件只接受生命组件发布的结果。 */
	float BaseMaxHealth = 1000.0f;
	float HealthMultiplier = 1.0f;
	UPROPERTY(Transient) TObjectPtr<UCastleMainTowerBalloonComponent> BalloonPresentation;

	/** 更新气球并广播原生命通知，使塔顶和 HUD 使用同一份已提交的数值。 */
	void NotifyHealthChanged(float PreviousHealth, float Damage);
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
