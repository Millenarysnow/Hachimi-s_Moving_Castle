#pragma once

#include "CastlePinballNPCSkyIslandDataAsset.h"
#include "Components/ActorComponent.h"
#include "CastlePinballNPCSimulationComponent.generated.h"

/** 一个局部坐标金币。稳定序号只用于关联可视球体，不参与碰撞。 */
struct FCastlePinballCoin
{
	uint64 Id = 0;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Velocity = FVector2D::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	TSet<int32> TriggeredGates;
};

/** 路线存局部坐标，进度达到阈值时立刻退出碰撞集合。 */
struct FCastlePinballNPC
{
	FVector2D Position = FVector2D::ZeroVector;
	double LaneCenter = 0.0;
	double HalfPatrolLength = 0.0;
	double LateralSpeed = 0.0;
	int32 Hits = 0;
	bool bRecruited = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(bool, FCastlePinballCommitRecruit, int32, FText&);

/** 由岛按帧推进的纯局部斜面运动。引擎网格只跟随结果，避免物理帧率改变结算。 */
UCLASS()
class MOVING_CASTLE_API UCastlePinballNPCSimulationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	bool Initialize(const UCastlePinballNPCSkyIslandDataAsset& InConfig, int32 Seed, FText& OutReason);
	void Clear();
	bool Advance(double DeltaSeconds, FText& OutReason);
	bool CanLaunch(double LateralPosition, double Angle, FText& OutReason) const;

	/** 调用方完成发射校验和扣费后同步提交，期间禁止重入。 */
	void CommitLaunch(double LateralPosition, double Angle);
	const TArray<FCastlePinballCoin>& GetCoins() const { return Coins; }
	const TArray<FCastlePinballNPC>& GetNPCs() const { return NPCs; }
	int32 GetRecruitedCount() const { return RecruitedCount; }
	FCastlePinballCommitRecruit OnCommitRecruit;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCastlePinballSimulationTest;
	friend class FCastlePinballIslandTest;
#endif
	enum class ECoinStepResult : uint8 { Active, Remove, Error };

	bool Step(FText& OutReason);
	ECoinStepResult AdvanceCoin(FCastlePinballCoin& Coin, FText& OutReason);
	void CrossGates(FCastlePinballCoin& Coin, const FVector2D& From, const FVector2D& To);
	bool HitNPC(int32 Index, FText& OutReason);

	UPROPERTY(Transient) TObjectPtr<const UCastlePinballNPCSkyIslandDataAsset> Config;
	TArray<FCastlePinballCoin> Coins;
	TArray<FCastlePinballNPC> NPCs;
	TArray<FVector2D> NPCStepStarts;
	double TimeRemainder = 0.0;
	double DownslopeGravity = 0.0;
	uint64 NextCoinId = 1;
	int32 RecruitedCount = 0;
	bool bFailed = false;
};
