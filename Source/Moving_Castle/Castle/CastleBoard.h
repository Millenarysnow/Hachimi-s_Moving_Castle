// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleBoard.generated.h"

class ACastleBuildingBase;
class UCastleBackpackComponent;
class UCastleGroundGenerator;

/**
 * 逻辑棋盘 Actor。
 *
 * 棋盘只保存地面生成结果、建筑占用索引和放置/合成规则。地面具体如何生成
 * 由 UCastleGroundGenerator 决定，建筑具体的表现由各个建筑子类决定，两者都
 * 不会把随机算法或美术组件写进棋盘本身。
 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBoard : public AActor
{
	GENERATED_BODY()

public:
	/** 构造函数设置默认棋盘尺寸、网格间距和随机生成策略。 */
	ACastleBoard();

	/** 棋盘 X/Y 平面尺寸和允许的垂直层数。地面只生成在 Z=0。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸")
	FIntVector GridSize = FIntVector(10, 10, 8);

	/** 棋盘网格原点，地面生成结果和放置坐标都使用同一坐标系。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸")
	FIntVector GridOrigin = FIntVector::ZeroValue;

	/** 相邻 X/Y 网格中心之间的世界距离，当前仅用于逻辑 Actor 的世界定位。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0"))
	float CellSize = 200.0f;

	/** 相邻垂直层的世界高度，当前仅用于逻辑 Actor 的世界定位。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0"))
	float LayerHeight = 200.0f;

	/** 地面生成策略类，可替换为固定模板或其他自定义策略。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	TSubclassOf<UCastleGroundGenerator> GroundGeneratorClass;

	/** 随机策略使用的种子，保持不变即可复现同一局地面布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	int32 GroundSeed = 1337;

	/** 是否要求随机生成器在棋盘原点生成灰色 0 号道路起始格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	bool bReserveRoadStartTile = true;

	/** BeginPlay 时自动生成地面，关闭后可以由关卡蓝图手动调用 GenerateGround。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	bool bGenerateGroundOnBeginPlay = true;

	/** 使用当前 GroundGeneratorClass 生成并接收一份新的地面结果。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|棋盘")
	bool GenerateGround(FText& OutFailureReason);

	/**
	 * 接收外部生成器的完整结果。
	 *
	 * 该函数会检查数量、坐标范围、重复坐标和地面属性；通过后才会替换棋盘的
	 * 地面状态，保证棋盘不会接受半份或不合法的生成结果。
	 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|棋盘")
	bool ApplyGroundGenerationResult(const FCastleGroundGenerationResult& Result, FText& OutFailureReason);

	/** 查询一个坐标对应的地面数据。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool TryGetGroundCell(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const;

	/** 判断坐标是否在棋盘边界内。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool IsCoordinateWithinBoard(const FIntVector& Coordinate) const;

	/**
	 * 检查一张建筑卡牌是否可以放到目标坐标。
	 *
	 * 放置只检查目标格和六个面相邻格；道路只接受道路地面或道路邻居，功能
	 * 建筑不会把道路当成颜色/数字匹配来源。
	 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool CanPlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const;

	/** 在棋盘上生成一座建筑，成功后可选择自动执行合成替换。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|棋盘")
	ACastleBuildingBase* PlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, FText& OutFailureReason);

	/** 从背包取出一张卡牌并尝试放置；放置失败时卡牌仍保留在原槽位。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|棋盘")
	ACastleBuildingBase* PlaceBuildingFromBackpack(const FIntVector& Coordinate, UCastleBackpackComponent* Backpack, int32 BackpackIndex, bool bTryMerge, FText& OutFailureReason);

	/** 直接从场上移除建筑，不处理背包。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|棋盘")
	bool RemoveBuilding(ACastleBuildingBase* Building, FText& OutFailureReason);

	/** 移除场上建筑并将其当前描述放回背包，背包满时整个操作失败。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|棋盘")
	bool RemoveBuildingToBackpack(ACastleBuildingBase* Building, UCastleBackpackComponent* Backpack, FText& OutFailureReason);

	/** 返回当前场上独立建筑 Actor 的数量，合成建筑只计一个 Actor。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	int32 GetPlacedBuildingCount() const;

	/** 返回当前场上独立建筑 Actor 的列表副本，供调试和后续效果统计使用。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	TArray<ACastleBuildingBase*> GetPlacedBuildings() const;

	/** 将网格坐标转换为棋盘 Actor 所在世界中的网格中心。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	FVector GetGridWorldLocation(const FIntVector& Coordinate) const;

protected:
	/** 根据 bGenerateGroundOnBeginPlay 决定是否在运行开始时生成地面。 */
	virtual void BeginPlay() override;

	/** 已经通过校验并被棋盘接收的地面结果。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|地面")
	TArray<FCastleGroundCell> GeneratedGroundCells;

	/** 每个被占用坐标对应的建筑 Actor，合成 Actor 会映射多个坐标。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|建筑")
	TMap<FIntVector, TObjectPtr<ACastleBuildingBase>> BuildingByCoordinate;

	/** 获取六个面相邻坐标，不包含斜角坐标。 */
	void GetFaceNeighborCoordinates(const FIntVector& Coordinate, TArray<FIntVector>& OutNeighbors) const;

	/** 返回指定坐标当前占用的建筑，没有建筑时返回空指针。 */
	ACastleBuildingBase* GetBuildingAt(const FIntVector& Coordinate) const;

	/** 在不改变 Actor 生命周期的情况下，把建筑的所有坐标注册到棋盘索引。 */
	bool RegisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason);

	/** 从棋盘索引移除建筑的所有坐标，但不销毁 Actor。 */
	bool UnregisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason);

	/** 生成并初始化一个建筑 Actor，尚未写入棋盘占用索引。 */
	ACastleBuildingBase* SpawnBuildingActor(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& OccupiedCoordinates, FText& OutFailureReason);

	/** 尝试对新放置的建筑连续执行可行的两两合成。 */
	ACastleBuildingBase* TryMergePlacedBuilding(ACastleBuildingBase* Building, FText& OutFailureReason);

	/** 查找可以和当前建筑合成的相邻建筑。 */
	ACastleBuildingBase* FindMergePartner(ACastleBuildingBase* Building) const;

	/** 校验两个建筑的联合占用坐标是否符合当前等级规定的形状。 */
	bool BuildMergedFootprint(const ACastleBuildingBase* First, const ACastleBuildingBase* Second, TArray<FIntVector>& OutFootprint) const;

	/** 生成高一级 Actor、替换两个旧 Actor，并更新全部坐标索引。 */
	ACastleBuildingBase* MergeBuildings(ACastleBuildingBase* First, ACastleBuildingBase* Second, const TArray<FIntVector>& MergedFootprint, FText& OutFailureReason);

	/** 校验建筑描述和具体类默认对象是否一致。 */
	bool ValidateBuildingDescriptor(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const;

	/** 检查指定地面坐标是否存在，允许查询上层坐标对应的平面地面。 */
	bool TryGetGroundCellAtPlane(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const;

	/** 判断一个建筑描述是否能和邻居或目标地面形成匹配。 */
	bool HasPlacementMatch(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor) const;
};
