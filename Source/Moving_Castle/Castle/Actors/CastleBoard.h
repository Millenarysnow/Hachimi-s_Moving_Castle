// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleBoard.generated.h"

class ACastleBuildingBase;
class UCastleBackpackComponent;
class UCastleGroundGenerator;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

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

	/**
	 * 相邻 X/Y 网格中心之间的距离（厘米），地面、建筑占位和格面指示器共用此值。
	 * 一级箭塔主体的平面模块为 500×500 厘米；屋檐等外伸装饰不计入单格尺寸。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0", Units = "cm"))
	float CellSize = 500.0f;

	/** 相邻垂直层的距离（厘米）；与箭塔每层 500 厘米的主体拼接模块保持一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0", Units = "cm"))
	float LayerHeight = 500.0f;

	/**
	 * 棋盘第一层建筑中心相对于棋盘 Actor 原点的高度。
	 *
	 * 地面薄片中心位于 WorldBaseHeight - LayerHeight×0.45，因此层高扩大到
	 * 500 厘米时将本值同步设为 235，使地面中心仍在棋盘原点上方 10 厘米，
	 * 避免被场景地板遮挡。正式场景仍可在实例上调整首层建筑的基准高度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "0.0", Units = "cm"))
	float WorldBaseHeight = 235.0f;

	/** 可用建筑定义，也是商店商品池与开局测试卡组的唯一配置入口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|建筑定义")
	TArray<TObjectPtr<UCastleBuildingDefinition>> BuildingDefinitions;

	/** 商店和开局卡组在使用前校验整个池；无效或空配置直接拒绝。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑定义")
	bool ValidateBuildingDefinitions(FText& OutFailureReason) const;

	/** 开局样例使用池中此类别的首个定义，不生成资产之外的替代建筑。 */
	UCastleBuildingDefinition* FindBuildingDefinition(ECastleBuildingType BuildingType) const;

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

	/** 原型背包会自动装入道路、箭塔、炮塔和农田样例卡牌。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "棋盘|原型操作")
	TObjectPtr<UCastleBackpackComponent> PrototypeBackpack;

	/** 当前数字键对应的样例卡牌，便于在没有 UI 资源时直接验证放置和合成。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|原型操作")
	TArray<FCastleBuildingDescriptor> PrototypePalette;

	/** 当前选中的样例卡牌槽位，范围为 0 到 PrototypePalette.Num()-1。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|原型操作")
	int32 SelectedPrototypeSlot = 0;

	/**
	 * 切换供原型操作使用的背包卡槽。
	 *
	 * 按键由 CastlePlayerController 接收，棋盘只负责验证卡槽并更新样例状态，
	 * 这样棋盘不再创建自己的输入组件，也不会改变中心指针的鼠标捕获状态。
	 */
	void SelectPrototypeSlot(int32 SlotIndex);

	/**
	 * 使用中心指针结果放置当前样例卡。
	 *
	 * 地面顶面作为初始落点时使用该地面格坐标；指向建筑或其他格面时使用
	 * 当前格面外侧的相邻候选坐标。实际合法性仍完全由棋盘放置规则校验。
	 */
	ACastleBuildingBase* PlacePrototypeBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget, FText& OutFailureReason);

	/** 根据实际背包槽位放置卡牌，供商店购买后的任意颜色数字卡使用。 */
	ACastleBuildingBase* PlaceBackpackBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget, int32 BackpackIndex, FText& OutFailureReason);

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

	/** 读取场上实际占用格数；合成 Actor 的多个格子分别计入房间总量。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	int32 GetOccupiedRoomCellCount() const { return BuildingByCoordinate.Num(); }

	/** 返回当前场上独立建筑 Actor 的列表副本，供调试和后续效果统计使用。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	TArray<ACastleBuildingBase*> GetPlacedBuildings() const;

	/** 将网格坐标转换为棋盘 Actor 所在世界中的网格中心。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	FVector GetGridWorldLocation(const FIntVector& Coordinate) const;

	/**
	 * 将一条世界空间射线解析为当前吸附的格子和格面。
	 *
	 * 控制器只提供屏幕中心射线，棋盘只接受真实命中地面或建筑网格的结果，
	 * 没有命中可见建造面时返回无效目标，避免空中或棋盘边界产生虚假高亮。
	 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool TryResolveGridCursorTarget(const FVector& RayStart, const FVector& RayEnd, FCastleGridCursorTarget& OutTarget) const;

protected:
	/** 仅用于挂载地面占位组件和背包组件的根节点。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "占位可视化")
	TObjectPtr<USceneComponent> SceneRoot;

	/** UE 自带 BasicShapes/Cube，占位地面和建筑都使用该网格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "占位可视化")
	TObjectPtr<UStaticMesh> PrototypeCubeMesh;

	/** UE 自带基础材质，运行时为每个地面格子创建动态颜色实例。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "占位可视化")
	TObjectPtr<UMaterialInterface> PrototypeMaterial;

	/** 每个地面坐标对应的可点击占位组件。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "占位可视化")
	TMap<FIntVector, TObjectPtr<UStaticMeshComponent>> GroundVisuals;

	/** 地面数字牌和数字文本等辅助组件，重建地面时必须一并销毁，避免重复显示。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "占位可视化")
	TArray<TObjectPtr<USceneComponent>> GroundAuxiliaryVisuals;

	/** 根据 bGenerateGroundOnBeginPlay 决定是否在运行开始时生成地面。 */
	virtual void BeginPlay() override;

	/** 生成原型背包和四个数字键样例卡，卡牌属性来自本次实际生成的地面。 */
	void InitializePrototypeDeck();

	/** 查找背包中与当前样例卡牌完全匹配的第一张卡牌。 */
	int32 FindPrototypeCardIndex(const FCastleBuildingDescriptor& Descriptor) const;

	/** 将已接收的地面结果生成成可点击的彩色立方体占位格。 */
	void RebuildGroundVisualization();

	/** 根据命中信息生成一个完整的网格指针结果。 */
	void BuildGridCursorTarget(const FIntVector& Cell, const FVector& LocalImpactPoint, const FVector& LocalNormal, FCastleGridCursorTarget& OutTarget) const;

	/** 返回颜色对应的占位显示颜色。 */
	static FLinearColor GetPrototypeColor(ECastleTileColor Color);

	/** 在屏幕上显示当前原型操作结果和失败原因。 */
	void ShowPrototypeMessage(const FString& Message, const FColor& Color = FColor::White) const;

	/** 已经通过校验并被棋盘接收的地面结果。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|地面")
	TArray<FCastleGroundCell> GeneratedGroundCells;

	/** 每个被占用坐标对应的建筑 Actor，合成 Actor 会映射多个坐标。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|建筑")
	TMap<FIntVector, TObjectPtr<ACastleBuildingBase>> BuildingByCoordinate;

	/** 根据卡牌等级生成标准放置形状；以选择格作为最小角，沿用既有 1/2/4/8 格规则。 */
	TArray<FIntVector> BuildPlacementFootprint(const FIntVector& Coordinate, int32 Level) const;

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
