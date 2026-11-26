#pragma once

#include "Mass/EntityHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleBadgeRuntimeSubsystem.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class UCastleMainTowerComponent;
enum class ECastleBattleKind : uint8;
enum class ECastleDirectorPhase : uint8;
struct FCastleBuildingTriggerContext;

/** 单个 NPC 的两类限时徽章移速。快照仅含数值，不持有任何 UObject。 */
struct FCastleBadgeNPCMovement
{
	/** 同来源取最强并从最近一次生效时重新计时。 */
	struct FTimedMultiplier
	{
		float Multiplier = 1.0f;
		double EndsAt = 0.0;

		float At(double Now) const { return Now < EndsAt ? Multiplier : 1.0f; }
		void Apply(float NewMultiplier, double Now, double Duration)
		{
			Multiplier = FMath::Max(At(Now), NewMultiplier);
			EndsAt = Now + Duration;
		}
	};

	FTimedMultiplier Whistle;
	FTimedMultiplier March;

	/** 汽笛与行军独立计时，和其他玩法来源也采用乘算。 */
	float At(double Now) const { return Whistle.At(Now) * March.At(Now); }
};

/** 世界级徽章状态。导演、棋盘与战斗事件在游戏线程提交，Mass 位移只接受值类型快照。 */
UCLASS()
class MOVING_CASTLE_API UCastleBadgeRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 查询下一笔手动刷新。只有成功刷新才扣配额并推进环路采购签计数。 */
	int32 GetRemainingFreeShopRefreshes() const;
	bool IsMatchingShopRefreshDue() const;
	void RecordShopRefresh(bool bUsedFreeRefresh);
	/** 查询下一笔普通商店购买的赠牌数，与实际事务分开，允许配置失败时原地重试。 */
	int64 GetNextShopPurchaseRebate() const;
	void RecordShopPurchase();

	/** 预留本波首次真实进塔事件，避免原生效果尚未完成时重入导致重复加数字。 */
	uint64 ReserveFirstTowerEntry(int64& OutNumberBonus);
	/** 只有同一波且尚未提交的事件可以还回这次机会。 */
	void CancelFirstTowerEntry(uint64 Reservation);
	/** 调用者在游戏线程获取快照；已过期的两组效果可在复制前一起清除。 */
	TMap<FMassEntityHandle, FCastleBadgeNPCMovement> CaptureNPCMovement(double Now);
	/** 空投调动在游戏线程更新，不必复制整张表。 */
	const FCastleBadgeNPCMovement* FindNPCMovement(FMassEntityHandle Entity) const;
	/** 炮塔弹药库与棋盘连锁共用匹配条件，保证一次进塔不会给同一连锁塔重复排队。 */
	bool ShouldChainTower(const ACastleBoard& Board, const ACastleBuildingBase& Source, const ACastleBuildingBase& Target) const;
	/** 蜂群判定本次伤害确实杀死敌人之后调用。 */
	bool TryAwardBeeKill();
	/** 敌方致命受击时查询气球扣。成功结算但不触发时 OutRetainedHealth 为零。 */
	bool TryResolveLastBalloonClasp(const UCastleMainTowerComponent& Tower, float& OutRetainedHealth);

protected:
	/** 仅游戏和 PIE 世界具备独立的单局事件与结算状态。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void HandleRunStarted();
	void HandleEditingPhaseEntered();
	void HandleBattleCleared(ECastleBattleKind Kind, int32 Stage);
	void HandleDirectorPhaseChanged(ECastleDirectorPhase Phase);
	void HandleWaveStarted(int32 WaveIndex, int32 TotalWaves);
	void HandleBuildingTriggered(ACastleBuildingBase* Source, const FCastleBuildingTriggerContext& Context);
	void ResetBattleEffects();
	bool TryGrantGameplayGold(int64 Gold, const TCHAR* BadgeName) const;

	TWeakObjectPtr<ACastleBoard> BattleBoard;
	FDelegateHandle BuildingTriggeredHandle;
	FDelegateHandle RunStartedHandle;
	FDelegateHandle EditingEnteredHandle;
	FDelegateHandle BattleClearedHandle;
	FDelegateHandle DirectorPhaseHandle;
	FDelegateHandle WaveStartedHandle;
	/** 每局的两个商店计数只保存十次周期中的进度，长期游玩也不会溢出。 */
	int32 ShopPurchases = 0;
	int32 ShopRefreshes = 0;
	int32 FreeShopRefreshes = 0;
	/** 单场结束才清除存续的移速效果；切波只清本波触发资格。 */
	TMap<FMassEntityHandle, FCastleBadgeNPCMovement> NPCMovement;
	TSet<FMassEntityHandle> NPCsEnteredThisWave;
	uint64 WaveSerial = 0;
	int32 WhistleRecipients = 0;
	bool bWaveActive = false;
	bool bFirstTowerEntryUsed = false;
	bool bDispatchingChain = false;
	bool bLastBalloonClaspUsed = false;
};
