#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleGameplayTypes.h"
#include "CastleMassTypes.h"
#include "CastleNavigationSubsystem.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerPath.h"
#include "CastleSupportTowerSubsystem.generated.h"

class ACastleBeeSwarmVisual;
class ACastleBoard;
class ACastleSupplyBalloon;
class UCastleSupportTowerData;
struct FMassEntityManager;

/** Tick 中一次收集实体位置；敌人生命在结算后就地更新，让同帧后续批次不重复击杀。 */
struct FCastleSupportUnit
{
	FCastleMassUnitHandle Handle;
	FVector Location = FVector::ZeroVector;
	float Health = 0.0f;
};

/** 支援战斗只在游戏线程运行：NPC 进入先入队，生成、索敌、落地和招募在 Mass 阶段外结算。 */
UCLASS()
class MOVING_CASTLE_API UCastleSupportTowerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 当次触发数字随请求保存，即使本帧随后又有 NPC 进入同一座支援塔也不会串值。 */
	bool QueueLaunch(ACastleBuildingBase& Source, int64 NumberBonus = 0);
	/** 三种敌人攻击和在途敌弹共用同一世界内不复用的气球 ID。 */
	bool FindBalloonTarget(const FVector& Origin, uint64& OutId, FVector& OutLocation) const;
	bool GetBalloonTarget(uint64 Id, FVector& OutLocation) const;
	bool DamageBalloon(uint64 Id, float Damage);
	/** 玩家手动点破。敌方打爆从 DamageBalloon 进入，破裂来源随飞行状态保存。 */
	bool PopBalloon(uint64 Id);
	float GetTowerDamageMultiplier(const ACastleBuildingBase& Tower) const;
	float GetArcherRateMultiplier(const ACastleBuildingBase& Tower) const;
	/** 合并空投箭塔增益、箭塔徽章和本次战斗的首次受击增益。 */
	float GetTowerFireRateMultiplier(const ACastleBuildingBase& Tower) const;
	/** 导演结束本场时清理尚未结算的支援效果和蓝色空投增援，不影响其他兵团。 */
	bool EndBattleSupport();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	/** 每次触发保留一个主巢队列，击杀繁殖的分支不推进也不阻挡队列。 */
	struct FBeeSchedule
	{
		TArray<TWeakObjectPtr<ACastleBuildingBase>> Backups;
		int32 Next = 0;
	};
	struct FSwarm
	{
		TWeakObjectPtr<ACastleBuildingBase> Source;
		TWeakObjectPtr<ACastleBuildingBase> Home;
		TWeakObjectPtr<ACastleBeeSwarmVisual> Visual;
		TSharedPtr<FBeeSchedule> Schedule;
		FVector HomeLocation = FVector::ZeroVector;
		FMassEntityHandle Target;
		int32 Count = 0;
		int32 Level = 0;
		float Damage = 0.0f;
		float Speed = 0.0f;
		double AttackSeconds = 0.0;
		double HitProgress = 0.0;
		double AttachedDelta = 0.0;
		bool bAttached = false;
		bool bReturning = false;
	};

	struct FBalloon
	{
		TWeakObjectPtr<ACastleBuildingBase> Source;
		TWeakObjectPtr<ACastleBoard> Board;
		TWeakObjectPtr<ACastleSupplyBalloon> Visual;
		TArray<FVector> Path;
		int32 NextPoint = 1;
		int32 Level = 1;
		int32 Number = 0;
		int32 Sum = 0;
		ECastleTileColor Color = ECastleTileColor::Gray;
		float Health = 0.0f;
		bool bCargoReleased = false;
		/** 点破时锁定摘绳手套加成，只扩大对敌伤害和减速。 */
		double ExtraImpactRadius = 0.0;
		FVector Landing = FVector::ZeroVector;
	};

	/** 重叠投放分别计时；移动使用最强存续效果，塔攻击与飞行也采用同一叠加规则。 */
	struct FUnitModifier
	{
		FMassEntityHandle Entity;
		float Multiplier;
		double Expires;
	};
	struct FTowerModifier
	{
		TWeakObjectPtr<ACastleBuildingBase> Tower;
		float Multiplier;
		double Expires;
	};
	struct FTimeModifier
	{
		float Multiplier;
		double Expires;
	};

	/** 临时路线是专用贴地快照；正常巡逻进度冻结，接回原点后再继续。 */
	struct FNPCJourney
	{
		FCastleSharedNavigationPath Path;
		FCastleBuildingTriggerPath Triggers;
		TWeakObjectPtr<ACastleBuildingBase> Source;
		double Distance = 0.0;
		double Speed = 0.0;
		double JoinDistance = 0.0;
		int32 JoinRevision = INDEX_NONE;
		bool bRally = false;
	};

	struct FLaunchRequest
	{
		TWeakObjectPtr<ACastleBuildingBase> Source;
		int64 NumberBonus;
	};
	TArray<FLaunchRequest> PendingLaunches;
	TArray<FSwarm> Swarms;
	TMap<uint64, FBalloon> Balloons;
	uint64 NextBalloonId = 1;
	TArray<FUnitModifier> MovementModifiers;
	TSet<FMassEntityHandle> ModifiedUnits;
	TArray<FTowerModifier> DamageModifiers;
	TArray<FTowerModifier> ArrowModifiers;
	TArray<FTimeModifier> FlightModifiers;
	TMap<FMassEntityHandle, FNPCJourney> Journeys;
	TArray<FCastleMassUnitHandle> RecruitedNPCs;

	bool GatherUnits(FMassEntityManager& Manager, ECastleMassUnitKind Kind, TArray<FCastleSupportUnit>& OutUnits) const;
	void Launch(ACastleBuildingBase& Source, int64 NumberBonus);
	bool LaunchSwarm(FSwarm&& Batch, const FVector& Start, const UCastleSupportTowerData& Data);
	bool BuildHiveSchedule(const ACastleBoard& Board, const ACastleBuildingBase& Source, int64 SourceNumber, FBeeSchedule& OutSchedule) const;
	void TickSwarms(float DeltaTime, TArray<FCastleSupportUnit>& Enemies);
	bool LaunchBalloon(ACastleBuildingBase& Source, ACastleBoard& Board, const UCastleSupportTowerData& Data, int64 Number);
	bool PopBalloonInternal(uint64 Id, bool bPlayerPopped);
	void TickBalloons(float DeltaTime, FMassEntityManager& Manager, TArray<FCastleSupportUnit>& Enemies);
	bool ApplyLanding(const FBalloon& Flight, FMassEntityManager& Manager, TArray<FCastleSupportUnit>& Enemies);
	bool FireAdjacentAttacks(const FBalloon& Flight, const TArray<FCastleSupportUnit>& Enemies) const;
	void RefreshModifiers(FMassEntityManager& Manager);
	float GetFlightMultiplier() const;
	bool RallyNPCs(FMassEntityManager& Manager, ACastleBuildingBase& Source, const FVector& Destination,
		TConstArrayView<FCastleSupportUnit> NPCs);
	bool RecruitNPCs(FMassEntityManager& Manager, ACastleBuildingBase& Source, const FVector& Destination, int32 Count);
	void TickJourneys(FMassEntityManager& Manager, float DeltaTime);
	void ClearFlightActors();
};
