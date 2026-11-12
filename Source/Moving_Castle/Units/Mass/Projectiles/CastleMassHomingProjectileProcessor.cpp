#include "CastleMassHomingProjectileProcessor.h"

#include "CastleMassFragments.h"
#include "CastleMassHomingProjectileFragments.h"
#include "CastleMassProcessors.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"

namespace CastleMassHomingProjectile
{
	/** 只在处理器本次执行期间有效；期间不执行结构性修改，延迟销毁交给生命周期处理器。 */
	struct FEnemyState
	{
		FVector Location;
		float Radius;
		FCastleMassUnitFragment* Unit;
	};
}

UCastleMassHomingProjectileProcessor::UCastleMassHomingProjectileProcessor()
	: ProjectileQuery(*this), EnemyQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
}

void UCastleMassHomingProjectileProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ProjectileQuery.AddTagRequirement<FCastleMassProjectileTag>(EMassFragmentPresence::All);
	ProjectileQuery.AddRequirement<FCastleMassHomingProjectileFragment>(EMassFragmentAccess::ReadOnly);
	// PreviousLocation 是逐帧状态；声明写权限，保证连续检测的数据不与其他查询并行读写。
	ProjectileQuery.AddRequirement<FCastleMassProjectileFragment>(EMassFragmentAccess::ReadWrite);
	ProjectileQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	ProjectileQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	ProjectileQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	ProjectileQuery.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadWrite);
	ProjectileQuery.AddConstSharedRequirement<FCastleMassMovementParameters>();

	EnemyQuery.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	EnemyQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EnemyQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EnemyQuery.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassHomingProjectileProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	using namespace CastleMassHomingProjectile;
	const float DeltaTime = Context.GetDeltaTimeSeconds();
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 追踪子弹处理失败：世界 %s 的帧时间无效（%f）。"), *GetNameSafe(GetWorld()), DeltaTime);
		return;
	}

	// 目标快照按实体句柄索引，避免每发子弹重新遍历所有敌人；生命保留指针以读取本帧最新结算。
	TMap<FMassEntityHandle, FEnemyState> Enemies;
	EnemyQuery.ForEachEntityChunk(Context, [&Enemies](FMassExecutionContext& Chunk)
	{
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Units = Chunk.GetMutableFragmentView<FCastleMassUnitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			Enemies.Add(Chunk.GetEntity(It), {Transforms[It].GetTransform().GetLocation(), Radii[It].Radius, &Units[It]});
		}
	});

	ProjectileQuery.ForEachEntityChunk(Context, [&Enemies, &EntityManager, DeltaTime](FMassExecutionContext& Chunk)
	{
		const auto Homings = Chunk.GetFragmentView<FCastleMassHomingProjectileFragment>();
		const auto Projectiles = Chunk.GetMutableFragmentView<FCastleMassProjectileFragment>();
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Units = Chunk.GetMutableFragmentView<FCastleMassUnitFragment>();
		const float Speed = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings.MaxSpeed;
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			// 已经命中或显式回收的子弹不再结算；同帧 Movement 结束后的生命周期统一销毁。
			if (Units[It].Health <= 0.0f) { continue; }
			const FMassEntityHandle Target = Homings[It].Target;
			if (!Target.IsSet())
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("Mass 追踪子弹 %d 缺少锁定目标，必须通过 SpawnHomingProjectiles 生成。"), Chunk.GetEntity(It).Index);
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}
			const FEnemyState* Enemy = Enemies.Find(Target);
			if (Enemy == nullptr && EntityManager.IsEntityActive(Target))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("Mass 追踪子弹 %d 的目标 %d 缺少敌人身份、位置、半径或生命组成。"), Chunk.GetEntity(It).Index, Target.Index);
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}
			if (Enemy == nullptr || Enemy->Unit->Health <= 0.0f)
			{
				// 用户约定：目标死亡或实体消失即结束本发；不搜索替代目标，也不造成剩余伤害。
				UE_LOG(LogMoving_Castle, Warning, TEXT("Mass 追踪子弹 %d：锁定目标 %d 已死亡或消失，本发回收且不造成伤害。"), Chunk.GetEntity(It).Index, Target.Index);
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}

			FTransform& Transform = Transforms[It].GetMutableTransform();
			FCastleMassProjectileFragment& Projectile = Projectiles[It];
			if (Enemy->Location.ContainsNaN() || !FMath::IsFinite(Enemy->Unit->Health)
				|| !FMath::IsFinite(Enemy->Radius) || Enemy->Radius < 0.0f
				|| Transform.ContainsNaN() || !FMath::IsFinite(Speed) || Speed < 0.0f
				|| !FMath::IsFinite(Radii[It].Radius) || Radii[It].Radius < 0.0f
				|| !FMath::IsFinite(Projectile.Damage) || Projectile.Damage < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("Mass 追踪子弹 %d 或目标 %d 的运行数据无效：位置/速度/半径/生命/伤害必须为有效数值。"), Chunk.GetEntity(It).Index, Target.Index);
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}
			Projectile.PreviousLocation = Transform.GetLocation();
			const FVector ToTarget = Enemy->Location - Projectile.PreviousLocation;
			const double Distance = ToTarget.Size();
			// 零距离本身就是重合命中，不为方向补任意默认向量；其他情况严格使用配置的速度大小。
			FVector& Velocity = Velocities[It].Value;
			Velocity = Distance > 0.0 ? ToTarget * (Speed / Distance) : FVector::ZeroVector;
			Transform.AddToTranslation(Velocity * DeltaTime);
			if (Distance > 0.0 && Speed > 0.0f) { Transform.SetRotation(ToTarget.ToOrientationQuat()); }

			// 两个半径都以各自实体 Transform 原点为球心；线段扫过目标球即可命中，避免高速穿透。
			const double HitRadius = static_cast<double>(Radii[It].Radius) + Enemy->Radius;
			if (FMath::PointDistToSegmentSquared(Enemy->Location, Projectile.PreviousLocation, Transform.GetLocation()) <= FMath::Square(HitRadius))
			{
				// 复用生命归零的死亡约定；比较后减法避免负生命，也不通过上限夹紧修正输入伤害。
				Enemy->Unit->Health = Projectile.Damage >= Enemy->Unit->Health ? 0.0f : Enemy->Unit->Health - Projectile.Damage;
				Units[It].Health = 0.0f;
				Velocity = FVector::ZeroVector;
			}
		}
	});
}
