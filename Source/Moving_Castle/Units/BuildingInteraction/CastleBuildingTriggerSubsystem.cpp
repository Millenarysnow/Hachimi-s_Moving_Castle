#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"

#include "CastleBuildingBase.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"

namespace CastleBuildingTriggers
{
	/** 调试开关只控制可视化，不改变逻辑次数；关闭后子系统不逐帧扫描建筑。 */
	static TAutoConsoleVariable<int32> Debug(TEXT("Castle.BuildingTriggers.Debug"), 0,
		TEXT("显示建筑逻辑触发区域与实例累计进入次数：0=关闭，1=开启。"), ECVF_Default);
}

bool UCastleBuildingTriggerSubsystem::RegisterBuilding(ACastleBuildingBase* Building, const FTransform& LocalToWorld,
	TArray<FBox>&& LocalBoxes, FText& OutFailure)
{
	check(IsInGameThread());
	OutFailure = FText::GetEmpty();
	if (!IsValid(Building) || Building->GetWorld() != GetWorld() || IdByBuilding.Contains(Building)
		|| LocalToWorld.ContainsNaN() || LocalToWorld.GetScale3D().GetAbsMin() <= UE_DOUBLE_SMALL_NUMBER || LocalBoxes.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("建筑触发登记的实例、世界、变换或区域无效，或该实例已登记。"));
		return false;
	}
	for (const FBox& Box : LocalBoxes)
	{
		if (!Box.IsValid || Box.Min.ContainsNaN() || Box.Max.ContainsNaN() || Box.GetSize().GetMin() <= 0.0)
		{
			OutFailure = FText::FromString(TEXT("建筑触发区域必须具有有限且为正的三维尺寸。"));
			return false;
		}
	}
	FRegistration Registration;
	Registration.Building = Building;
	Registration.Region.BuildingId = NextBuildingId++;
	Registration.Region.LocalToWorld = LocalToWorld;
	Registration.Region.LocalBoxes = MoveTemp(LocalBoxes);
	IdByBuilding.Add(Building, Registration.Region.BuildingId);
	Buildings.Add(Registration.Region.BuildingId, MoveTemp(Registration));
	++RegistryRevision;
	return true;
}

void UCastleBuildingTriggerSubsystem::UnregisterBuilding(ACastleBuildingBase* Building)
{
	check(IsInGameThread());
	uint64 Id;
	if (IdByBuilding.RemoveAndCopyValue(Building, Id))
	{
		Buildings.Remove(Id);
		++RegistryRevision;
	}
}

void UCastleBuildingTriggerSubsystem::NotifyBuildingStateChanged(ACastleBuildingBase* Building)
{
	check(IsInGameThread());
	if (IdByBuilding.Contains(Building)) { ++RegistryRevision; }
}

TSharedPtr<const FCastleBuildingTriggerPath, ESPMode::ThreadSafe> UCastleBuildingTriggerSubsystem::AcquireTriggerPath(
	const TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe>& Path)
{
	check(IsInGameThread());
	if (!Path || !Path->bValid || !Path->bClosedLoop) { CachedPath.Reset(); return nullptr; }
	if (CachedPath && CachedPath->PathRevision == Path->PathRevision && CachedPath->RegistryRevision == RegistryRevision)
	{
		return CachedPath;
	}
	TArray<FCastleBuildingTriggerRegion> Regions;
	for (const auto& Pair : Buildings)
	{
		const ACastleBuildingBase* Building = Pair.Value.Building.Get();
		if (IsValid(Building) && Building->CanReceiveBuildingTrigger()) { Regions.Add(Pair.Value.Region); }
	}
	auto Compiled = MakeShared<FCastleBuildingTriggerPath, ESPMode::ThreadSafe>();
	FText Failure;
	if (!FCastleBuildingTriggerPath::Compile(*Path, RegistryRevision, Regions, *Compiled, Failure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑进入路线编译失败：%s"), *Failure.ToString());
		CachedPath.Reset();
		return nullptr;
	}
	++CompilationCount;
	CachedPath = Compiled;
	return CachedPath;
}

void UCastleBuildingTriggerSubsystem::DispatchEntries(const FCastleBuildingTriggerPath& Path,
	const TConstArrayView<FCastleBuildingEntryEvent> Events)
{
	check(IsInGameThread());
	const UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	for (const FCastleBuildingEntryEvent& Event : Events)
	{
		// 效果回调可能销毁或合成建筑；每条消息重新核对登记版本，不继续消费过期批次。
		const auto CurrentPath = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->AcquireSharedPath();
		if (RegistryRevision != Path.RegistryRevision || !CurrentPath || !CurrentPath->bValid
			|| CurrentPath->PathRevision != Path.PathRevision) { return; }
		const FRegistration* Registration = Buildings.Find(Event.BuildingId);
		ACastleBuildingBase* Building = Registration ? Registration->Building.Get() : nullptr;
		FCastleBuildingTriggerContext Context;
		Context.Source = ECastleBuildingTriggerSource::NPCEntry;
		Context.NPC.Entity = Event.NPC;
		Context.NPC.World = GetWorld();
		Context.EntryLocation = Event.Location;
		if (!IsValid(Building) || !Building->CanReceiveBuildingTrigger() || !Units->IsUnitValid(Context.NPC)) { continue; }
		ICastleBuildingTriggerInterface* Entry = Cast<ICastleBuildingTriggerInterface>(Building);
		check(Entry);
		Entry->TriggerBuildingEffect(Context);
	}
}

void UCastleBuildingTriggerSubsystem::Tick(float DeltaTime)
{
	// 明确开启可视化才遍历实例；区域使用登记时的棋盘空间，不读取模型包围盒。
	for (const auto& Pair : Buildings)
	{
		const ACastleBuildingBase* Building = Pair.Value.Building.Get();
		if (!IsValid(Building)) { continue; }
		const FTransform& Transform = Pair.Value.Region.LocalToWorld;
		for (const FBox& Box : Pair.Value.Region.LocalBoxes)
		{
			DrawDebugBox(GetWorld(), Transform.TransformPosition(Box.GetCenter()),
				Box.GetExtent() * Transform.GetScale3D().GetAbs(), Transform.GetRotation(), FColor::Cyan, false, -1.0f, 0, 2.0f);
		}
		const FString Text = FString::Printf(TEXT("%s\n触发次数: %lld"), *Building->GetName(), Building->BuildingEffect->TriggerCount);
		DrawDebugString(GetWorld(), Building->GetActorLocation() + FVector(0, 0, 220), Text, nullptr, FColor::Yellow, 0.0f, true);
	}
}

TStatId UCastleBuildingTriggerSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleBuildingTriggerSubsystem, STATGROUP_Tickables); }
bool UCastleBuildingTriggerSubsystem::IsTickable() const { return !IsTemplate() && CastleBuildingTriggers::Debug.GetValueOnGameThread() != 0; }
bool UCastleBuildingTriggerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

void UCastleBuildingTriggerSubsystem::Deinitialize()
{
	CachedPath.Reset();
	Buildings.Reset();
	IdByBuilding.Reset();
	++RegistryRevision;
	Super::Deinitialize();
}
