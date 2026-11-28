#include "Items/CastleRoomTraitComponent.h"

#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Items/CastleItemTypes.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

UCastleRoomTraitComponent::UCastleRoomTraitComponent()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
	DeparturePortalMesh = Mesh.Object;
	DeparturePortalMaterial = Material.Object;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetVisibility(false);
}

bool UCastleRoomTraitComponent::TryInitializeTrait(const FCastleBuildingDescriptor& Descriptor, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [&](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑定义 %s 无法初始化房间特质（组件 %s）：%s"),
			*GetPathNameSafe(Descriptor.Definition.Get()), *GetPathName(), *Reason.ToString());
		return false;
	};
	if (!IsValid(Descriptor.Definition) || !CastleRoomCards::ValidateTrait(Descriptor, OutReason))
	{
		return Reject(OutReason.IsEmpty() ? INVTEXT("缺少有效房间定义。") : OutReason);
	}
	if (Descriptor.RoomTrait == ECastleRoomTrait::Highway)
	{
		// 道路状态由棋盘索引决策，沿用建筑数字标签作为状态标识。
		return true;
	}
	if (Descriptor.RoomTrait != ECastleRoomTrait::SkyIslandPortal)
	{
		return Reject(INVTEXT("只有高速公路和传送门房间需要场上的特质组件。"));
	}
	if (!IsValid(DeparturePortalMesh) || !IsValid(DeparturePortalMaterial) || !SetStaticMesh(DeparturePortalMesh))
	{
		return Reject(INVTEXT("无法配置离岛门资源 /Engine/BasicShapes/Cube.Cube 和 /Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial。"));
	}
	SetMaterial(0, DeparturePortalMaterial);
	return true;
}

void UCastleRoomTraitComponent::ConfigurePortalTransform(const float CellSize, const float LayerHeight)
{
	// 引擎立方体的边长为 100 cm。薄门竖在房间中心，四周保留室内通行空间。
	SetRelativeScale3D(FVector(CellSize * 0.12f, CellSize * 0.36f, LayerHeight * 0.54f) / 100.0);
	SetRelativeLocation(FVector(0.0, 0.0, LayerHeight * 0.36f));
}

void UCastleRoomTraitComponent::SetPortalAvailable(const bool bAvailable)
{
	// 高速公路组件没有门体。只有挂上门体的组件才可以响应 Visibility。
	const bool bEnablePortal = bAvailable && GetStaticMesh() != nullptr;
	SetVisibility(bEnablePortal);
	SetCollisionEnabled(bEnablePortal ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

bool UCastleRoomTraitComponent::IsPortalAvailable() const
{
	return IsRegistered() && GetStaticMesh() == DeparturePortalMesh && IsValid(DeparturePortalMesh)
		&& GetCollisionEnabled() == ECollisionEnabled::QueryOnly && GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block;
}

bool UCastleRoomTraitComponent::TryInteractWithPortal(FText& OutReason)
{
	auto* Room = Cast<ACastleBuildingBase>(GetOwner());
	auto* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
	if (!IsValid(Room) || Room->GetRoomTraitComponent() != this || !IsPortalAvailable() || !Director)
	{
		OutReason = INVTEXT("传送门必须属于已建成的房间，且本世界需要有效导演。");
		UE_LOG(LogMoving_Castle, Error, TEXT("传送门组件 %s 无法交互：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	// 成功时导演在发布空岛阶段前销毁本组件，返回后不要访问本门的成员。
	return Director->TryEnterBonusSkyIslandFromPortal(Room, OutReason);
}
