#include "CastleMenuModel.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleMainTowerBalloonComponent.h"
#include "Castle/PCG/CastleBuildingPCGComponent.h"
#include "Castle/PCG/CastleBuildingPCGSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"

bool FCastleMenuModelBuilder::AddComponent(const UStaticMeshComponent& Part, const FTransform& TowerFrame, FString& Error)
{
	// 合并动画只移动副本。原件的世界变换已包含建筑朝向、层高和父组件缩放。
	return AddMesh(Part.GetStaticMesh(), Part.GetMaterials(), Part.GetCustomPrimitiveData().Data,
		Part.GetComponentTransform().GetRelativeTransform(TowerFrame), Error);
}

bool FCastleMenuModelBuilder::AddMesh(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
	const TArray<float>& Data, const FTransform& Local, FString& Error)
{
	if (!IsValid(Mesh) || !Mesh->IsAsset() || Materials.IsEmpty() || !Local.IsValid())
	{
		Error = FString::Printf(TEXT("展示零件 %s 缺少可保存的网格、材质槽或有效变换。"), *GetPathNameSafe(Mesh));
		return false;
	}
	TArray<TSoftObjectPtr<UMaterialInterface>> Assets;
	for (UMaterialInterface* Material : Materials)
	{
		// 瞬态 MID 路径无法跨进程恢复；明确报出该零件，避免把临时材质写成空引用。
		if (!IsValid(Material) || !Material->IsAsset())
		{
			Error = FString::Printf(TEXT("展示网格 %s 的第 %d 槽材质 %s 无法持久化。"),
				*Mesh->GetPathName(), Assets.Num(), *GetPathNameSafe(Material));
			return false;
		}
		Assets.Add(Material);
	}
	for (auto It = ByMesh.CreateConstKeyIterator(Mesh); It; ++It)
	{
		FCastleMenuMeshBatch& Batch = Batches[It.Value()];
		if (Batch.Materials == Assets && Batch.PrimitiveData == Data)
		{
			Batch.Placements.Add(Local);
			return true;
		}
	}
	ByMesh.Add(Mesh, Batches.Num());
	FCastleMenuMeshBatch& Batch = Batches.AddDefaulted_GetRef();
	Batch.Mesh = Mesh;
	Batch.Materials = MoveTemp(Assets);
	Batch.PrimitiveData = Data;
	Batch.Placements.Add(Local);
	return true;
}

UCastleMenuModel* UCastleMenuModel::LoadForMenu(UObject* Owner, FString& Error)
{
	ISaveGameSystem* Storage = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!Storage)
	{
		Error = FString::Printf(TEXT("存档槽 %s 的平台存储服务不可用。"), Slot);
		return nullptr;
	}
	const auto Exists = Storage->DoesSaveGameExistWithResult(Slot, 0);
	if (Exists == ISaveGameSystem::ESaveExistsResult::DoesNotExist)
	{
		// 首次游玩属于策划定义的正常状态。仅取正式套件的首级塔身和完整吊挂。
		FCastleMenuModelBuilder Builder;
		if (!GetDefault<UCastleMainTowerBalloonComponent>()->BuildFirstMenuTower(Builder, Error)) { return nullptr; }
		auto* First = NewObject<UCastleMenuModel>(Owner);
		First->Tower = MoveTemp(Builder.Batches);
		return First;
	}
	if (Exists != ISaveGameSystem::ESaveExistsResult::OK)
	{
		Error = FString::Printf(TEXT("检查展示存档 %s 失败，平台状态 %d。"), Slot, static_cast<int32>(Exists));
		return nullptr;
	}
	auto* Saved = Cast<UCastleMenuModel>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Saved || Saved->Version != CurrentVersion || Saved->Tower.IsEmpty())
	{
		Error = FString::Printf(TEXT("展示存档 %s 无法读取、缺少主塔或版本不兼容（读取版本 %d）。"), Slot, Saved ? Saved->Version : INDEX_NONE);
		return nullptr;
	}
	return Saved;
}

bool UCastleMenuRecorder::CaptureTower(TArray<FCastleMenuMeshBatch>& Out, FString& Error) const
{
	FCastleMenuModelBuilder Builder;
	if (!Lift->CollectMenuTower(Builder, Error)) { return false; }
	Out = MoveTemp(Builder.Batches);
	return true;
}

bool UCastleMenuRecorder::Write(FString& Error) const
{
	if (UGameplayStatics::SaveGameToSlot(Model, UCastleMenuModel::Slot, 0)) { return true; }
	Error = FString::Printf(TEXT("棋盘 %s 无法写入展示存档 %s。"), *GetPathNameSafe(Board), UCastleMenuModel::Slot);
	return false;
}

bool UCastleMenuRecorder::Start(ACastleBoard& InBoard, FString& Error)
{
	Board = &InBoard;
	Lift = IsValid(Board->MainTowerActor) ? Board->MainTowerActor->FindComponentByClass<UCastleMainTowerBalloonComponent>() : nullptr;
	auto* PCG = InBoard.GetWorld()->GetSubsystem<UCastleBuildingPCGSubsystem>();
	if (!IsValid(Lift) || !PCG)
	{
		Error = InBoard.GetPathName() + TEXT(" 缺少正式主塔吊挂或建筑 PCG，无法记录本局。");
		return false;
	}
	Model = NewObject<UCastleMenuModel>(this);
	if (!CaptureTower(Model->Tower, Error) || !Write(Error)) { return false; }
	PCG->OnLayoutResolved.AddUObject(this, &UCastleMenuRecorder::RecordRooms);
	Lift->OnDisplayChanged.AddUObject(this, &UCastleMenuRecorder::RecordTower);
	return true;
}

void UCastleMenuRecorder::RecordRooms(const ACastleBoard& Source, const TArray<ACastleBuildingBase*>& Buildings)
{
	if (&Source != Board) { return; }
	FCastleMenuModelBuilder Builder;
	FString Error;
	for (const ACastleBuildingBase* Building : Buildings)
	{
		if (!Building->BuildingPCG->CollectMenuParts(Builder, Board->MainTowerActor->GetActorTransform(), Error))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("记录建筑 %s 到 %s 失败：%s"), *Building->GetPathName(), UCastleMenuModel::Slot, *Error);
			return;
		}
	}
	// 必须接受空数组。最后一间拆掉后，下一次进入菜单应当只看见主塔。
	Model->Rooms = MoveTemp(Builder.Batches);
	if (!Write(Error)) { UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Error); }
}

void UCastleMenuRecorder::RecordTower()
{
	FString Error;
	if (!CaptureTower(Model->Tower, Error) || !Write(Error)) { UE_LOG(LogMoving_Castle, Error, TEXT("记录主塔失败：%s"), *Error); }
}

void UCastleMenuRecorder::Stop()
{
	// 注销后，卸载时销毁房间与气球产生的通知不会倒写上局记录。
	if (IsValid(Lift)) { Lift->OnDisplayChanged.RemoveAll(this); }
	if (IsValid(Board) && Board->GetWorld())
	{
		if (auto* PCG = Board->GetWorld()->GetSubsystem<UCastleBuildingPCGSubsystem>()) { PCG->OnLayoutResolved.RemoveAll(this); }
	}
}
