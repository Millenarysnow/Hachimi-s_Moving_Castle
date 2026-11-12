#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleBuildingTriggerDemo.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class ACastleMassSpawner;
class ACastlePatrolRoute;
class UCameraComponent;
class UTextRenderComponent;

	/** 直接运行的原生演示：箭塔、农田、共享巡逻 NPC 和会受伤的 Mass 敌人。 */
UCLASS()
class MOVING_CASTLE_API ACastleBuildingTriggerDemo : public AActor
{
	GENERATED_BODY()

public:
	ACastleBuildingTriggerDemo();
	virtual void Tick(float DeltaSeconds) override;

	/** 0=等待放置与导航，1=可运行，-1=明确失败；自动化与画面读取同一状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑触发演示")
	int32 Phase = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="建筑触发演示")
	TObjectPtr<ACastleBoard> Board;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="建筑触发演示")
	TArray<TObjectPtr<ACastleBuildingBase>> Buildings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="建筑触发演示")
	TObjectPtr<ACastleMassSpawner> Spawner;

	/** 箭塔攻击范围内的可见 Mass 敌人；死亡时由同一演示面板显示结果。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="建筑触发演示")
	TObjectPtr<ACastleMassSpawner> EnemySpawner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="建筑触发演示")
	TObjectPtr<ACastlePatrolRoute> Route;

	/** 关卡可调人数；验证大批实体时仍复用相同场景和生产代码入口。 */
	UPROPERTY(EditAnywhere, Category="建筑触发演示", meta=(ClampMin="1"))
	int32 NPCCount = 8;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> DemoCamera;

	/** 直接渲染在场景中的计数，验收反馈不依赖屏幕调试信息开关或 HUD。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> CounterA;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> CounterB;

	/** 直接显示敌人当前生命或死亡状态，验证结果不依赖编辑器 Output Log。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> EnemyStatus;

	/** 只比较整数变化，避免每帧重复创建文字、字体布局和渲染资源。 */
	int64 DisplayedCountA = -1;
	int64 DisplayedCountB = -1;
	float DisplayedEnemyHealth = -1.0f;

	/** 演示失败停止推进并显示原因，不能用隐藏的备用路线或模型补出成功。 */
	void Fail(const FText& Reason);
};
