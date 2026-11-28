#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CastleMenuModel.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class UCastleMainTowerBalloonComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** 同网格、同材质、同属性色的零件在存档中共用资源路径，每次摆放保留独立的塔根局部变换。 */
USTRUCT()
struct FCastleMenuMeshBatch
{
	GENERATED_BODY()
	UPROPERTY() TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY() TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY() TArray<float> PrimitiveData;
	UPROPERTY() TArray<FTransform> Placements;
};

/** 对一次完整采集按网格建立临时索引，避免多房间的同类零件重复写资产路径。 */
class FCastleMenuModelBuilder
{
public:
	bool AddComponent(const UStaticMeshComponent& Part, const FTransform& TowerFrame, FString& Error);
	bool AddMesh(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials, const TArray<float>& Data,
		const FTransform& Local, FString& Error);
	TArray<FCastleMenuMeshBatch> Batches;
private:
	TMultiMap<UStaticMesh*, int32> ByMesh;
};

/** 专用于菜单的外观记录，版本号跟随展示数据结构，与可继续游玩的存档独立。 */
UCLASS()
class UCastleMenuModel : public USaveGame
{
	GENERATED_BODY()
public:
	static constexpr int32 CurrentVersion = 1;
	static constexpr const TCHAR* Slot = TEXT("CastleLastRunAppearance");
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() TArray<FCastleMenuMeshBatch> Tower;
	UPROPERTY() TArray<FCastleMenuMeshBatch> Rooms;

	/** 只有平台明确返回槽不存在时才构造首玩主塔。损坏或未知版本向界面返回具体错误。 */
	static UCastleMenuModel* LoadForMenu(UObject* Owner, FString& Error);
};

/** 由成功启动的正式局导演持有；按提交事件写盘，教学关卡和关卡销毁不参与记录。 */
UCLASS()
class UCastleMenuRecorder : public UObject
{
	GENERATED_BODY()
public:
	bool Start(ACastleBoard& InBoard, FString& Error);
	void Stop();
private:
	/** 此集合由整盘 PCG 已提交的占用提供，包含删除最后一栋后的空集合。 */
	void RecordRooms(const ACastleBoard& Source, const TArray<ACastleBuildingBase*>& Buildings);
	void RecordTower();
	bool CaptureTower(TArray<FCastleMenuMeshBatch>& Out, FString& Error) const;
	bool Write(FString& Error) const;
	UPROPERTY() TObjectPtr<UCastleMenuModel> Model;
	UPROPERTY() TObjectPtr<ACastleBoard> Board;
	UPROPERTY() TObjectPtr<UCastleMainTowerBalloonComponent> Lift;
};
