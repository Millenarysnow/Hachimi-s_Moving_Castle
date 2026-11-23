#include "CastleShieldTowerEffectComponent.h"

#include "CastleBoard.h"
#include "CastleBuildings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"

bool UCastleShieldTowerEffectComponent::CalculateShieldBlocks(int64& OutBlocks) const
{
	OutBlocks = 0;
	const ACastleBuildingBase* Tower = Cast<ACastleBuildingBase>(GetOwner());
	if (!IsValid(Tower) || Tower->GetBuildingLevel() < 1 || Tower->GetBuildingLevel() > 5 || Tower->GetEffectiveBuildingNumber() <= 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 次数计算失败：拥有者、等级或数字无效。"), *GetPathNameSafe(Tower));
		return false;
	}
	const int32 Level = Tower->GetBuildingLevel();
	int64 Total = Tower->GetEffectiveBuildingNumber();
	int32 AttackTowerCount = 0;
	if (Level > 1)
	{
		ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(Tower);
		if (!Board)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 无所属棋盘，无法计算六面连通分量。"), *Tower->GetPathName());
			return false;
		}
		TArray<ACastleBuildingBase*> Candidates = Board->GetPlacedBuildings();
		if (Level <= 3)
		{
			// D14 同色传递不能借异色建筑搭桥；Lv4/5 才允许整个六面连通分量。
			Candidates = Candidates.FilterByPredicate([Tower](const ACastleBuildingBase* Other) { return Other->GetBuildingColor() == Tower->GetBuildingColor(); });
		}
		TArray<ACastleBuildingBase*> Connected;
		if (!Board->GetConnectedBuildings(Tower, Candidates, Connected)) { return false; }
		Total = 0;
		for (const ACastleBuildingBase* Other : Connected)
		{
			if (Total > MAX_int64 - Other->GetEffectiveBuildingNumber())
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 连通数字和溢出。"), *Tower->GetPathName());
				return false;
			}
			Total += Other->GetEffectiveBuildingNumber();
			if (Other != Tower && (Other->GetBuildingType() == ECastleBuildingType::ArcherTower || Other->GetBuildingType() == ECastleBuildingType::CannonTower)) { ++AttackTowerCount; }
		}
	}
	if (Level == 5)
	{
		// 每个攻击塔实例只翻倍一次，多格塔不能按占用格数反复乘二，也不使用浮点幂丢失精度。
		for (int32 Index = 0; Index < AttackTowerCount; ++Index)
		{
			if (Total > MAX_int64 / 2)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 的 2^%d 攻击塔倍率导致抵挡次数溢出。"), *Tower->GetPathName(), AttackTowerCount);
				return false;
			}
			Total *= 2;
		}
	}
	OutBlocks = Total;
	return true;
}

bool UCastleShieldTowerEffectComponent::PreparePresentation()
{
	ACastleShieldTowerBuilding* Tower = Cast<ACastleShieldTowerBuilding>(GetOwner());
	if (!Tower || !Tower->ShieldBubble || !Tower->ShieldRemainingText || !Tower->ShieldBubble->GetStaticMesh()
		|| !Tower->ShieldBubble->GetMaterial(0) || !Tower->ShieldRemainingText->GetMaterial(0)
		|| !FMath::IsFinite(AttractionRadius) || AttractionRadius <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 缺少护盾球体/文字/材质资源或吸引半径非法，材质须配置 M_CastleShield 与 M_CastleShieldCounter。"), *GetNameSafe(GetOwner()));
		return false;
	}
	const FVector Scale = Tower->GetActorScale3D();
	if (Scale.ContainsNaN() || Scale.GetAbsMin() <= SMALL_NUMBER)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 缩放非法，无法创建与吸引半径一致的护盾球体。"), *Tower->GetPathName());
		return false;
	}
	// 世界半径与子系统吸引范围一致；绝对缩放避免棋盘缩放把可见球体再次放大。
	Tower->ShieldBubble->SetAbsolute(false, false, true);
	Tower->ShieldBubble->SetWorldScale3D(FVector(AttractionRadius / 50.0f));
	const FBox VisualBounds = Tower->GetVisualBounds();
	if (!VisualBounds.IsValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 缺少有效美术包围盒，无法把剩余次数文字放在屋顶上方。"), *Tower->GetPathName());
		return false;
	}
	// 占位模型和以后正式模型可能高于吸引球体；文字必须跟随实际美术顶部，不能按半径固定在屋顶内部。
	// 与既有建筑标签一样先转换到 Actor 局部空间，保留模型作者变换及棋盘旋转/缩放。
	const FBox LocalBounds = VisualBounds.TransformBy(Tower->GetActorTransform().ToInverseMatrixWithScale());
	const FVector LocalLabel(LocalBounds.GetCenter().X, LocalBounds.GetCenter().Y, LocalBounds.Max.Z + 150.0f);
	Tower->ShieldRemainingText->SetWorldLocation(Tower->GetActorTransform().TransformPosition(LocalLabel));
	UCastleShieldTowerSubsystem* Shields = GetWorld()->GetSubsystem<UCastleShieldTowerSubsystem>();
	if (!Shields)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 无法访问本世界护盾子系统。"), *Tower->GetPathName());
		return false;
	}
	if (!ShieldStateHandle.IsValid())
	{
		RegisteredShields = Shields;
		ShieldStateHandle = Shields->OnShieldStateChanged.AddUObject(this, &UCastleShieldTowerEffectComponent::HandleShieldStateChanged);
	}
	return true;
}

bool UCastleShieldTowerEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	int64 Computed = 0;
	if (!CalculateShieldBlocks(Computed) || !PreparePresentation()) { return false; }
	ACastleShieldTowerBuilding* Tower = CastChecked<ACastleShieldTowerBuilding>(GetOwner());
	UCastleShieldTowerSubsystem* Shields = RegisteredShields.Get();
	int64 Current = 0;
	Shields->GetShieldRemainingBlocks(Tower, Current);
	const int64 Refreshed = FMath::Max(Current, Computed);
	const float Reflection = Tower->GetBuildingLevel() >= 3 ? static_cast<float>(Tower->GetEffectiveBuildingNumber()) : 0.0f;
	if (Shields->UpsertActiveShield(Tower, Tower->GetActorLocation(), AttractionRadius, Refreshed, Reflection) == 0) { return false; }
	UE_LOG(LogMoving_Castle, Display, TEXT("盾塔 %s Lv%d 展开/刷新护盾：当前 %lld，本次 %lld，结果 %lld，反伤 %.1f。"),
		*Tower->GetName(), Tower->GetBuildingLevel(), Current, Computed, Refreshed, Reflection);
	return true;
}

void UCastleShieldTowerEffectComponent::HandleShieldStateChanged(UObject* Owner, const int64 Blocks)
{
	// 委托携带 OwnerKey，每座盾塔仅更新自己的表现，不会把其他盾塔的破盾广播当作自身状态。
	if (Owner != GetOwner()) { return; }
	ACastleShieldTowerBuilding* Tower = CastChecked<ACastleShieldTowerBuilding>(GetOwner());
	RemainingBlocks = Blocks;
	Tower->ShieldBubble->SetVisibility(Blocks > 0);
	Tower->ShieldRemainingText->SetVisibility(Blocks > 0);
	Tower->ShieldRemainingText->SetText(FText::AsNumber(Blocks));
	UE_LOG(LogMoving_Castle, Verbose, TEXT("盾塔 %s 剩余抵挡 %lld 次%s。"), *Tower->GetName(), Blocks, Blocks == 0 ? TEXT("，护盾表现已隐藏，建筑保留") : TEXT(""));
}

void UCastleShieldTowerEffectComponent::DeactivateAndUnbind()
{
	if (UCastleShieldTowerSubsystem* Shields = RegisteredShields.Get())
	{
		Shields->DeactivateShield(GetOwner());
		Shields->OnShieldStateChanged.Remove(ShieldStateHandle);
	}
	ShieldStateHandle.Reset();
	RegisteredShields.Reset();
	RemainingBlocks = 0;
}

void UCastleShieldTowerEffectComponent::OnBoardUnregistered(ACastleBoard& Board)
{
	DeactivateAndUnbind();
}

void UCastleShieldTowerEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeactivateAndUnbind();
	Super::EndPlay(EndPlayReason);
}
