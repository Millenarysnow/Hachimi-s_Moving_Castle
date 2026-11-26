// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMainTowerBalloonComponent.h"
#include "Castle/MainMenu/CastleMenuModel.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 与指定 560 cm 交付 assembly_manifest.json 的网格名、UE 厘米坐标及数组顺序一一对应。 */
	enum class ELiftAsset : uint8
	{
		Tether = 1, Scout, Spindle, Lantern, Crown, PodRose, PodTeal
	};

	// 首项固定挂环仍须加载并校验。五级塔身已烘焙同位置的挂环，重复实例化会造成共面闪烁。
	const TCHAR* const LiftMeshNames[] = {
		TEXT("SM_LiftAnchor"), TEXT("SM_LiftTether"), TEXT("SM_LiftBalloon_Scout"), TEXT("SM_LiftBalloon_Spindle"),
		TEXT("SM_LiftBalloon_Lantern"), TEXT("SM_LiftBalloon_Crown"), TEXT("SM_LiftBalloon_Pod_Rose"), TEXT("SM_LiftBalloon_Pod_Teal")
	};
	constexpr TCHAR TowerAssetRoot[] = TEXT("/Game/MovingCastle/Model/MainTower");

	struct FLiftMount
	{
		const TCHAR* Name;
		ELiftAsset Balloon;
		FVector Start;
		FVector End;
		double Yaw;
		double Scale;
		double TetherRadius;

		/** 游戏和首玩预览使用同一条明确的绳眼至气球底眼变换。 */
		FTransform TetherTransform() const
		{
			const FVector Cable = End - Start;
			return FTransform(FQuat::FindBetweenNormals(FVector::UpVector, Cable.GetUnsafeNormal()), Start,
				FVector(TetherRadius / 5.5, TetherRadius / 5.5, Cable.Length() / 100.0));
		}
		FTransform BalloonTransform() const { return FTransform(FRotator(0, Yaw, 0), End, FVector(Scale)); }
	};

	const FLiftMount FirstFlight[] = {
		{TEXT("LiftMain"), ELiftAsset::Scout, {0, 0, 2888.3999}, {0, 0, 3027.0}, 7.0, 1.0, 5.5}
	};
	const FLiftMount Longwatch[] = {
		{TEXT("LiftMain"), ELiftAsset::Spindle, {0, 0, 4008.4}, {0, 0, 4161.9999}, -11.0, 1.06, 5.5}
	};
	const FLiftMount Beacon[] = {
		{TEXT("LiftMain"), ELiftAsset::Lantern, {0, 0, 5128.4}, {0, 0, 5292.0002}, 4.0, 1.12, 5.5},
		{TEXT("LiftFL"), ELiftAsset::PodRose, {-312, 312, 4013.7001}, {-588, 305, 4857.0}, -20.0, 1.02, 4.4}
	};
	const FLiftMount TwinEscort[] = {
		{TEXT("LiftMain"), ELiftAsset::Lantern, {0, 0, 6248.4001}, {0, 0, 6429.0001}, -16.0, 1.23, 5.5},
		{TEXT("LiftFL"), ELiftAsset::PodRose, {-312, 312, 5133.7002}, {-616, 304, 6048.0003}, 13.0, 1.17, 4.4},
		{TEXT("LiftBR"), ELiftAsset::PodRose, {312, -312, 5133.7002}, {616, -304, 6102.0}, -21.0, 1.17, 4.4}
	};
	const FLiftMount CrownFleet[] = {
		{TEXT("LiftMain"), ELiftAsset::Crown, {0, 0, 7368.3998}, {0, 0, 7600.0}, -7.0, 1.24, 5.5},
		{TEXT("LiftFL"), ELiftAsset::PodRose, {-312, 312, 6253.6999}, {-765, 395, 7173.9998}, 15.0, 1.25, 4.4},
		{TEXT("LiftFR"), ELiftAsset::PodTeal, {312, 312, 6253.6999}, {98, 920, 7361.0001}, -17.0, 1.16, 4.4},
		{TEXT("LiftBR"), ELiftAsset::PodRose, {312, -312, 6253.6999}, {765, -395, 7216.9998}, -14.0, 1.25, 4.4},
		{TEXT("LiftBL"), ELiftAsset::PodTeal, {-312, -312, 6253.6999}, {-98, -920, 7402.9999}, 11.0, 1.16, 4.4}
	};

	struct FTowerLiftTier
	{
		int32 Layers;
		const TCHAR* MeshName;
		double Eaves;
		TArrayView<const FLiftMount> Mounts;
	};

	// 烘焙塔冠的标高由作者给出，索引 4 仅提供 11 层配置和表现校验。
	const FTowerLiftTier LiftTiers[] = {
		{3, TEXT("SM_MainTower_L01"), 1680.0, FirstFlight},
		{5, TEXT("SM_MainTower_L02"), 2800.0, Longwatch},
		{7, TEXT("SM_MainTower_L03"), 3920.0, Beacon},
		{9, TEXT("SM_MainTower_L04"), 5040.0, TwinEscort},
		{11, TEXT("SM_MainTower_L05"), 6160.0, CrownFleet}
	};

	FString TowerKitObjectPath(const TCHAR* Folder, const TCHAR* Name)
	{
		return FString::Printf(TEXT("%s/%s/%s.%s"), TowerAssetRoot, Folder, Name, Name);
	}

	/** 只销毁本次拥有的组件，注册失败时也能撤下已经注册的前半批。 */
	void DestroyLiftParts(TArray<FCastleMainTowerLiftParts>& Parts)
	{
		for (const FCastleMainTowerLiftParts& Rig : Parts)
		{
			Rig.Balloon->DestroyComponent();
			Rig.Tether->DestroyComponent();
		}
		Parts.Reset();
	}
}

UCastleMainTowerBalloonComponent::UCastleMainTowerBalloonComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 首次构造时解析硬引用供 Cook 查找；原生运行时实例也显式赋值，
	// 避免动态 NewObject 后依赖蓝图组件模板传播数组默认值。
	struct FKitAssets
	{
		TArray<UStaticMesh*> Towers;
		TArray<UStaticMesh*> Lift;
		UMaterialInterface* Dark = nullptr;
	};
	static const FKitAssets Kit = []
	{
		FKitAssets Assets;
		for (const FTowerLiftTier& Tier : LiftTiers)
		{
			const ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(*TowerKitObjectPath(TEXT("Tower"), Tier.MeshName));
			Assets.Towers.Add(Mesh.Object);
		}
		for (const TCHAR* Name : LiftMeshNames)
		{
			const ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(*TowerKitObjectPath(TEXT("LiftKit"), Name));
			Assets.Lift.Add(Mesh.Object);
		}
		// Dark 是清单中唯一没有被任何本期网格引用的材质，保留显式硬引用。
		const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(*TowerKitObjectPath(TEXT("Materials"), TEXT("M_Aero_Dark")));
		Assets.Dark = Material.Object;
		return Assets;
	}();
	TowerAssets = Kit.Towers;
	LiftAssets = Kit.Lift;
	DarkMaterial = Kit.Dark;
}

int32 UCastleMainTowerBalloonComponent::FindTierIndex(const int32 LayerCount)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(LiftTiers); ++Index)
	{
		if (LiftTiers[Index].Layers == LayerCount) { return Index; }
	}
	return INDEX_NONE;
}

bool UCastleMainTowerBalloonComponent::Initialize(UStaticMeshComponent* InTowerMesh, USceneComponent* InCrownAnchor,
	USceneComponent* InBalloonAnchor, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [&](const FString& Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法初始化 560 cm 套件：%s。"), *GetPathNameSafe(GetOwner()), *Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	if (!IsValid(GetOwner()) || TowerMesh || !IsValid(InTowerMesh) || !InTowerMesh->IsRegistered() || InTowerMesh->GetOwner() != GetOwner()
		|| !IsValid(InCrownAnchor) || !InCrownAnchor->IsRegistered() || InCrownAnchor->GetOwner() != GetOwner()
		|| !IsValid(InBalloonAnchor) || !InBalloonAnchor->IsRegistered() || InBalloonAnchor->GetOwner() != GetOwner())
	{
		return Reject(TEXT("塔身网格或运行时锚点缺失、尚未注册或已初始化"));
	}
	if (TowerAssets.Num() != UE_ARRAY_COUNT(LiftTiers) || LiftAssets.Num() != UE_ARRAY_COUNT(LiftMeshNames))
	{
		return Reject(TEXT("整塔或吊挂资产引用数量与清单不符"));
	}
	const auto ValidateMesh = [&](const UStaticMesh* Mesh, const TCHAR* ExpectedName)
	{
		if (!IsValid(Mesh) || Mesh->GetFName() != FName(ExpectedName))
		{
			return Reject(FString::Printf(TEXT("静态网格 %s 缺失或引用错误"), ExpectedName));
		}
		if (Mesh->GetStaticMaterials().IsEmpty())
		{
			return Reject(FString::Printf(TEXT("静态网格 %s 缺少作者材质槽"), ExpectedName));
		}
		for (const FStaticMaterial& Material : Mesh->GetStaticMaterials())
		{
			if (!IsValid(Material.MaterialInterface) || Material.MaterialInterface->GetFName() != Material.MaterialSlotName)
			{
				return Reject(FString::Printf(TEXT("静态网格 %s 的作者材质 %s 缺失或引用错误"),
					ExpectedName, *Material.MaterialSlotName.ToString()));
			}
		}
		return true;
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(LiftTiers); ++Index)
	{
		if (!ValidateMesh(TowerAssets[Index], LiftTiers[Index].MeshName)) { return false; }
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(LiftMeshNames); ++Index)
	{
		if (!ValidateMesh(LiftAssets[Index], LiftMeshNames[Index])) { return false; }
	}
	if (!IsValid(DarkMaterial))
	{
		return Reject(TEXT("材质 M_Aero_Dark 缺失"));
	}
	if (InTowerMesh->GetStaticMesh() != TowerAssets[0])
	{
		return Reject(FString::Printf(TEXT("整塔组件的默认网格 %s 应为 SM_MainTower_L01"),
			*GetPathNameSafe(InTowerMesh->GetStaticMesh())));
	}

	TowerMesh = InTowerMesh;
	CrownAnchor = InCrownAnchor;
	BalloonAnchor = InBalloonAnchor;
	return true;
}

bool UCastleMainTowerBalloonComponent::PrepareTier(const int32 LayerCount, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Tower = GetOwner();
	const auto Reject = [&](const TCHAR* Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法准备 %d 层吊挂套件：%s。"),
			*GetPathNameSafe(Tower), LayerCount, Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	const int32 TierIndex = FindTierIndex(LayerCount);
	if (TierIndex == INDEX_NONE)
	{
		return Reject(TEXT("层数须为 3、5、7、9 或 11"));
	}
	if (!IsValid(TowerMesh) || !TowerMesh->IsRegistered() || TowerMesh->Mobility != EComponentMobility::Movable
		|| !IsValid(CrownAnchor) || !CrownAnchor->IsRegistered() || !IsValid(BalloonAnchor) || !BalloonAnchor->IsRegistered()
		|| !IsValid(Tower) || !IsValid(Tower->GetRootComponent()) || PendingTierIndex != INDEX_NONE)
	{
		return Reject(TEXT("塔身、运行时锚点未就绪或已有待提交的吊挂套件"));
	}
	const FTowerLiftTier& Tier = LiftTiers[TierIndex];
	const auto CreatePart = [&](const ELiftAsset Asset, const TCHAR* Type, const FLiftMount& Mount, const FTransform& Relative)
	{
		// 组件名包含挂点，便于用编辑器和验收日志定位具体的绳眼与气球。
		const FName BaseName(*FString::Printf(TEXT("Tower%s_%s"), Type, Mount.Name));
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Tower,
			MakeUniqueObjectName(Tower, UStaticMeshComponent::StaticClass(), BaseName));
		Part->SetupAttachment(Tower->GetRootComponent());
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetRelativeTransform(Relative);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetStaticMesh(LiftAssets[static_cast<int32>(Asset)]);
		Part->SetVisibility(false);
		Tower->AddInstanceComponent(Part);
		Part->RegisterComponent();
		return Part;
	};

	for (const FLiftMount& Mount : Tier.Mounts)
	{
		FCastleMainTowerLiftParts& Rig = PendingParts.AddDefaulted_GetRef();
		// 清单起点位于塔身烘焙挂环的吊耳孔。拉索沿局部 +Z 延伸，100 cm 端点缩放后抵达气球底眼。
		Rig.Tether = CreatePart(ELiftAsset::Tether, TEXT("LiftTether"), Mount, Mount.TetherTransform());
		Rig.Balloon = CreatePart(Mount.Balloon, TEXT("LiftBalloon"), Mount, Mount.BalloonTransform());
		if (!Rig.Tether->IsRegistered() || !Rig.Balloon->IsRegistered())
		{
			DiscardPreparedTier();
			return Reject(TEXT("拉索或大气球组件注册失败"));
		}
	}
	PendingTierIndex = TierIndex;
	return true;
}

void UCastleMainTowerBalloonComponent::DiscardPreparedTier()
{
	DestroyLiftParts(PendingParts);
	PendingTierIndex = INDEX_NONE;
}

void UCastleMainTowerBalloonComponent::CommitPreparedTier(const float CurrentHealth, const float MaxHealth)
{
	check(LiftTiers[PendingTierIndex].Mounts.Num() == PendingParts.Num());
	const FTowerLiftTier& Tier = LiftTiers[PendingTierIndex];
	TowerMesh->SetStaticMesh(TowerAssets[PendingTierIndex]);
	CrownAnchor->SetRelativeLocation(FVector(0, 0, Tier.Eaves));
	BalloonAnchor->SetRelativeLocation(Tier.Mounts[0].Start);

	DestroyLiftParts(LiveParts);
	LiveParts = MoveTemp(PendingParts);
	PendingTierIndex = INDEX_NONE;
	LiveCount = 0;
	UpdateHealth(CurrentHealth, MaxHealth);
	// 即使没有剩余气球，本次换塔仍改变了塔身。非零时 UpdateHealth 已发出同一次通知。
	if (LiveCount == 0) { OnDisplayChanged.Broadcast(); }
}

bool UCastleMainTowerBalloonComponent::UpdateHealth(const float CurrentHealth, const float MaxHealth)
{
	// 生命来源须提供有效区间内的原始值。报错前不改动存活颗数或组件显隐。
	if (!FMath::IsFinite(CurrentHealth) || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f
		|| CurrentHealth < 0.0f || CurrentHealth > MaxHealth)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 的吊挂生命无效：当前 %g，最大 %g。"),
			*GetPathNameSafe(GetOwner()), CurrentHealth, MaxHealth);
		return false;
	}
	const int32 RigCount = LiveParts.Num();
	check(RigCount > 0);
	// 有效生命比例乘以套件颗数再向上取整，结果一定落在 [0, RigCount]。
	const int32 Count = FMath::CeilToInt(static_cast<double>(CurrentHealth) * RigCount / MaxHealth);
	for (int32 Index = FMath::Min(Count, LiveCount); Index < FMath::Max(Count, LiveCount); ++Index)
	{
		const bool bVisible = Index < Count;
		LiveParts[Index].Balloon->SetVisibility(bVisible);
		LiveParts[Index].Tether->SetVisibility(bVisible);
	}
	if (Count < LiveCount)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔 %s 破裂 %d 颗大气球，剩余 %d 颗。"),
			*GetPathNameSafe(GetOwner()), LiveCount - Count, Count);
	}
	const bool bChanged = LiveCount != Count;
	LiveCount = Count;
	if (bChanged) { OnDisplayChanged.Broadcast(); }
	return true;
}

bool UCastleMainTowerBalloonComponent::CollectMenuTower(FCastleMenuModelBuilder& Builder, FString& Error) const
{
	if (!IsValid(TowerMesh) || !IsValid(GetOwner()) || LiveParts.IsEmpty())
	{
		Error = GetPathName() + TEXT(" 的塔身或已提交吊挂尚未就绪。");
		return false;
	}
	const FTransform Frame = GetOwner()->GetActorTransform();
	if (!Builder.AddComponent(*TowerMesh, Frame, Error)) { return false; }
	for (int32 Index = 0; Index < LiveCount; ++Index)
	{
		const FCastleMainTowerLiftParts& Rig = LiveParts[Index];
		if (!IsValid(Rig.Tether) || !IsValid(Rig.Balloon)) { Error = GetPathName() + TEXT(" 的有效吊挂缺少拉索或气球。"); return false; }
		if (!Builder.AddComponent(*Rig.Tether, Frame, Error) || !Builder.AddComponent(*Rig.Balloon, Frame, Error)) { return false; }
	}
	return true;
}

bool UCastleMainTowerBalloonComponent::BuildFirstMenuTower(FCastleMenuModelBuilder& Builder, FString& Error) const
{
	// 首级的网格、吊挂方向与材质均来自正式套件。模型丢失时保留作者资源名供定位。
	if (TowerAssets.Num() != UE_ARRAY_COUNT(LiftTiers) || LiftAssets.Num() != UE_ARRAY_COUNT(LiftMeshNames))
	{
		Error = TEXT("主塔套件缺少已配置的塔身或吊挂资产。");
		return false;
	}
	auto Add = [&](UStaticMesh* Mesh, const TCHAR* Name, const FTransform& Local)
	{
		if (!IsValid(Mesh)) { Error = FString::Printf(TEXT("首玩主塔缺少网格 %s。"), Name); return false; }
		TArray<UMaterialInterface*> Materials;
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials()) { Materials.Add(Slot.MaterialInterface); }
		return Builder.AddMesh(Mesh, Materials, {}, Local, Error);
	};
	if (!Add(TowerAssets[0], LiftTiers[0].MeshName, FTransform::Identity)) { return false; }
	for (const FLiftMount& Mount : LiftTiers[0].Mounts)
	{
		const int32 Tether = static_cast<int32>(ELiftAsset::Tether);
		const int32 Balloon = static_cast<int32>(Mount.Balloon);
		if (!Add(LiftAssets[Tether], LiftMeshNames[Tether], Mount.TetherTransform())
			|| !Add(LiftAssets[Balloon], LiftMeshNames[Balloon], Mount.BalloonTransform())) { return false; }
	}
	return true;
}
