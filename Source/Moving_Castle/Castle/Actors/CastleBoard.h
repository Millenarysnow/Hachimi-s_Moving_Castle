// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleBoard.generated.h"

class ACastleBoard;
/** 新范围及外围地块完整提交后广播，监听者可以立即查询新范围。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleBoardExpanded, ACastleBoard*);
class UCastlePCGRuleSetDataAsset;
/** 一次放置/连续合成/移除已提交后的通用事件，消费者自行处理派生布局。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleBoardLayoutCommitted, ACastleBoard*, FVector);

class ACastleBuildingBase;
class ACastleGroundTileActor;
class ACastleMassEnemySpawner;
class UAssemblyFlightComponent;
class UCastleBackpackComponent;
class UCastleCombatDataAsset;
class UCastleGroundGenerator;
class UCastleMainTowerComponent;
class UCastleTowerSideGridComponent;
struct FCastleTowerSideCell;
class USceneComponent;
struct FCastleBuildingTriggerContext;

/** 只广播棋盘中建筑自身的成功触发；弹药库联动直接进入炮塔组件，不再广播，明确切断级联。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleBoardBuildingTriggered, ACastleBuildingBase*, const FCastleBuildingTriggerContext&);

/**
 * 逻辑棋盘 Actor。
 *
 * 地面和主塔侧格提供各自的初始放置来源；两种模式共享这一份三维建筑占用和合成规则，
 * 不在各个塔面分别管理建筑，也不把随机算法和美术细节放入占用管理。
 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBoard : public AActor
{
	GENERATED_BODY()

public:
	/** 显式规则资产，缺少配置时 PCG 子系统公开报错并拒绝替换。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="棋盘|PCG")
	TObjectPtr<UCastlePCGRuleSetDataAsset> PCGRuleSet;
	FCastleBoardLayoutCommitted OnLayoutCommitted;
	/** 构造函数设置默认棋盘尺寸、网格间距和随机生成策略。 */
	ACastleBoard();

	/** 起始 X/Y 尺寸和垂直层数；运行时扩张不修改它，世界坐标映射始终固定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸")
	FIntVector GridSize = FIntVector(10, 10, 8);

	/** 可扩张的单边上限；配置非法时明确拒绝，不夹紧。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="棋盘|尺寸")
	int32 MaxBoardSize = 15;

	/** 当前 XY 范围以外允许依附建筑伸出的格数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="棋盘|尺寸")
	int32 MaxOverhangCells = 2;

	/** 棋盘网格原点，地面生成结果和放置坐标都使用同一坐标系。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸")
	FIntVector GridOrigin = FIntVector::ZeroValue;

	/**
	 * 单个棋盘格实体的水平边长（厘米）。相邻格之间的间隙由 GroundTileGap 显式表达。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0", Units = "cm"))
	float GroundTileSize = 500.0f;

	/** 美术地块之间保留 15 厘米间隙；多格建筑功能地板的位置也必须采用该间隙对应的中心距。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm"))
	float GroundTileGap = 15.0f;

	/** 相邻 X/Y 网格中心之间的距离（厘米），必须等于 GroundTileSize + GroundTileGap。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0", Units = "cm"))
	float CellSize = 515.0f;

	/** 相邻垂直层的距离（厘米）；与外部建筑 FBX 的 560 厘米层高保持一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "1.0", Units = "cm"))
	float LayerHeight = 560.0f;

	/**
	 * 棋盘第一层网格中心相对于棋盘 Actor 原点的高度。
	 * 建筑 Actor 位于占用范围的底面中心，因此首层建筑原点还要沿棋盘局部 Z 下降半个层高。
	 *
	 * 地面薄片中心位于 WorldBaseHeight - LayerHeight×0.45；首层建筑底面仍由
	 * 棋盘生成逻辑沿局部 Z 下降半个层高对齐。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|尺寸", meta = (ClampMin = "0.0", Units = "cm"))
	float WorldBaseHeight = 262.0f;

	/** 一次操作会影响多处建筑时，调用方可为这些变化指定同一个世界空间传播原点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="棋盘|变化表现")
	bool bUseSharedVisualChangeOrigin = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="棋盘|变化表现", meta=(EditCondition="bUseSharedVisualChangeOrigin"))
	FVector VisualChangeOrigin = FVector::ZeroVector;

	/** 可用建筑定义，也是商店商品池与开局测试卡组的唯一配置入口。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|建筑定义")
	TArray<TObjectPtr<UCastleBuildingDefinition>> BuildingDefinitions;

	/** 商店和开局卡组在使用前校验整个池；无效或空配置直接拒绝。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑定义")
	bool ValidateBuildingDefinitions(FText& OutFailureReason) const;

	/** 开局样例使用池中此类别的首个定义，不生成资产之外的替代建筑。 */
	UCastleBuildingDefinition* FindBuildingDefinition(ECastleBuildingType BuildingType) const;

	/** 卡牌放置及摄影共用标准占格形状：以选择格为最小角，沿用 1/2/4/8 格规则。 */
	static TArray<FIntVector> BuildPlacementFootprint(const FIntVector& Coordinate, int32 Level);

	/** 旧地图仍采用地面模式；正式地图由关卡实例明确选择主塔侧面。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|建造面")
	ECastlePlacementSurfaceMode PlacementSurfaceMode = ECastlePlacementSurfaceMode::GroundBoard;

	/** 塔根至侧格面板中心的水平距离；塔侧地图必须按主塔模型明确配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|建造面", meta = (Units = "cm", EditCondition = "PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides"))
	float TowerSideSurfaceOffset = 0.0f;

	/** 校验当前模式的实际数据，不能从缺失的塔侧格回退到地面。 */
	bool ValidatePlacementSurface(FText& OutFailureReason) const;

	/** 供导演切换编辑表现；不移除塔身、建筑或格子的已生成属性。 */
	bool SetPlacementSurfacePresentationHidden(bool bHidden, FText& OutFailureReason);
	bool IsPlacementSurfacePresentationHidden() const;

	/** 查询确切塔身坐标和朝向的属性；不在四面之间搜索匹配。 */
	const FCastleTowerSideCell* FindTowerSideCell(const FIntVector& TowerCell, ECastleGridFace Face) const;

	/** 运行时和验收均可显式提交一个已解析的来源面，正常玩家仍走背包入口。 */
	ACastleBuildingBase* PlaceBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget,
		const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, FText& OutFailureReason);

	/** 地面生成策略类，可替换为固定模板或其他自定义策略。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	TSubclassOf<UCastleGroundGenerator> GroundGeneratorClass;

	/** 随机策略使用的种子，保持不变即可复现同一局地面布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	int32 GroundSeed = 1337;

	/** 仅地面模式可在 BeginPlay 自动生成地面；该开关不决定当前模式。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "棋盘|地面")
	bool bGenerateGroundOnBeginPlay = true;

	/** 主塔和开局敌人共用的战斗配置 Data Asset。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|配置")
	TObjectPtr<UCastleCombatDataAsset> CombatData;

	/** 自动生成的主塔模型类；默认指向 Content/MovingCastle/BP/BP_MainTower。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|主塔")
	TSubclassOf<AActor> MainTowerClass;

	/** BeginPlay 时是否在棋盘中心生成主塔。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|主塔")
	bool bSpawnMainTowerOnBeginPlay = true;

	/** BeginPlay 时是否在主塔周围生成战斗配置中的初始敌人。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人")
	bool bSpawnInitialEnemiesOnBeginPlay = true;

	/** 自动生成的主塔 Actor，供调试和后续 UI 读取。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "战斗|主塔")
	TObjectPtr<AActor> MainTowerActor;

	/** 自动生成的敌人批次管理器。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "战斗|敌人")
	TObjectPtr<ACastleMassEnemySpawner> InitialEnemySpawner;

	/** 原型背包装入普通房间、箭塔、炮塔、农田和盾塔各两张五色 1～9 号卡。 */
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

	/** 只控制地块表现和 Visibility 命中；重复设置同一状态没有副作用。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|棋盘")
	void SetGroundPresentationHidden(bool bInHidden);

	UFUNCTION(BlueprintPure, Category="移动城堡|棋盘")
	bool IsGroundPresentationHidden() const { return bGroundPresentationHidden; }

	/** 对称扩张一圈；数据或 Actor 创建失败时整次操作不提交。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|棋盘")
	bool ExpandBoard(FText& OutFailureReason);

	/** 当前 XY 闭区间；与固定世界坐标映射原点分离。 */
	UFUNCTION(BlueprintPure, Category="移动城堡|棋盘")
	void GetBoardExtent(FIntPoint& OutMin, FIntPoint& OutMax) const;

	FCastleBoardExpanded OnBoardExpanded;

	/** 查询一个坐标对应的地面数据。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool TryGetGroundCell(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const;

	/** 判断坐标是否在棋盘边界内。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool IsCoordinateWithinBoard(const FIntVector& Coordinate) const;

	/**
	 * 检查一张建筑卡牌是否可以放到目标坐标。
	 *
	 * 没有射线方向的坐标入口固定用下方顶面提供属性匹配：首层只匹配目标地面，
	 * 上层只匹配正下方建筑，不要求其余占用格有支撑。侧向悬空建造使用带来源的光标入口。
	 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool CanPlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const;

	/** 校验当前高亮面：只匹配该面所属地面或建筑，逐格检查占用与边界，允许下方无支撑。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool CanPlaceBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const;

	/** 生成并占用格子，游戏中先播放 Assembly Flight；返回新放置 Actor，完成后才可能被合并结果替换。 */
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

	/** 按真实占用索引查所属棋盘；独立生成、未注册或已注销的建筑没有所属棋盘。 */
	static ACastleBoard* FindBoardForBuilding(const ACastleBuildingBase* Building);

	/** 查询指定坐标当前登记的建筑（含侧挂越界格）；未占用时返回空指针。 */
	ACastleBuildingBase* GetBuildingAt(const FIntVector& Coordinate) const;

	/** 正下方底面投影内扫描指定层数；-1 表示全部下层，结果不含自身且按建筑实例去重。 */
	bool GetBuildingsBelow(const ACastleBuildingBase* Source, int32 LayerCount, TArray<ACastleBuildingBase*>& OutBuildings) const;

	/** 占用包围盒外扩；任一占用格命中即收录整座已完成建筑，结果含自身且按实例去重。 */
	bool GetBuildingsInExpandedBounds(const ACastleBuildingBase* Source, const FIntVector& Expansion,
		TArray<ACastleBuildingBase*>& OutBuildings) const;

	/** 在显式候选集合内按六面相邻 BFS；结果含源建筑，不能穿过集合外的建筑连接两端。 */
	bool GetConnectedBuildings(const ACastleBuildingBase* Source, const TArray<ACastleBuildingBase*>& Candidates,
		TArray<ACastleBuildingBase*>& OutBuildings) const;

	/** 按六面查询已落成邻居，按占格坐标排序；通行模式与同色或同号的 PCG 接口一致。 */
	bool GetFaceAdjacentBuildings(const ACastleBuildingBase* Source, bool bRequirePassage,
		TArray<ACastleBuildingBase*>& OutBuildings) const;

	/** 原生联动通知不保存历史或去重 NPC；同一次进入只由建筑接口广播一次。 */
	FCastleBoardBuildingTriggered OnBuildingTriggered;

	/** 将网格坐标转换为棋盘 Actor 所在世界中的网格中心。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	FVector GetGridWorldLocation(const FIntVector& Coordinate) const;

	/** 返回棋盘平面中心格；偶数尺寸取中心左下侧的确定格，保证每局可复现。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|战斗")
	FIntVector GetBoardCenterCoordinate() const;

	/**
	 * 将一条世界空间射线解析为当前吸附的格子和格面。
	 *
	 * 控制器只提供屏幕中心射线，棋盘只接受当前模式的真实格面或建筑网格，
	 * 没有命中可见建造面时返回无效目标，避免空中或棋盘边界产生虚假高亮。
	 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|棋盘")
	bool TryResolveGridCursorTarget(const FVector& RayStart, const FVector& RayEnd, FCastleGridCursorTarget& OutTarget) const;

protected:
	/** 已提交的对称扩张圈数，只影响范围，不参与坐标到世界变换。 */
	UPROPERTY(VisibleInstanceOnly, Category="棋盘|尺寸")
	int32 ExpansionRings = 0;

	/** 棋盘级表现状态，新生成地块必须继承。 */
	UPROPERTY(VisibleInstanceOnly, Category="棋盘|地面")
	bool bGroundPresentationHidden = false;

	/** 管理器自己的根节点，为棋盘坐标提供 Actor 变换空间。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "占位可视化")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 管理器只保存格子 Actor 索引；每个格子的数据、碰撞和表现由自身 Actor 承载。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "占位可视化")
	TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>> GroundTiles;

	/** 开局根据关卡模式准备初始来源，之后才按实际格子属性准备样例卡。 */
	virtual void BeginPlay() override;
	/** 清理播放回调和等待请求，退出关卡时不再触发升级。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 生成原型背包的样例卡，属性来自本次实际生成的建造面。 */
	void InitializePrototypeDeck();

	/** 查找背包中与当前样例卡牌完全匹配的第一张卡牌。 */
	int32 FindPrototypeCardIndex(const FCastleBuildingDescriptor& Descriptor) const;

	/** 批量生成独立格子 Actor；只有整批成功后才替换现有棋盘格。 */
	bool RebuildGroundVisualization(const TArray<FCastleGroundCell>& Cells, FText& OutFailureReason);

	/** 仅准备新 Actor，调用方在全批成功后提交索引；失败销毁本批临时 Actor。 */
	bool CreateGroundTileActors(const TArray<FCastleGroundCell>& Cells,
		TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>>& OutTiles, FText& OutFailureReason);

	/** 校验指定闭区间的完整地面数据，既用于整盘替换也用于扩张候选。 */
	bool ValidateGroundResult(const FCastleGroundGenerationResult& Result, FIntPoint Min, FIntPoint Max, FText& OutFailureReason) const;

	/** 销毁本管理器生成的全部地面格 Actor，并清空坐标索引。 */
	void DestroyGroundTileActors();

	/** 根据命中信息生成一个完整的网格指针结果。 */
	void BuildGridCursorTarget(const FIntVector& Cell, const FVector& LocalImpactPoint, const FVector& LocalNormal, FCastleGridCursorTarget& OutTarget) const;

	/** 在屏幕上显示当前原型操作结果和失败原因。 */
	void ShowPrototypeMessage(const FString& Message, const FColor& Color = FColor::White) const;

	/** 在中心格生成主塔模型并挂载 C++ 生命组件。 */
	bool SpawnMainTower(FText& OutFailureReason);

	/** 读取战斗配置并延迟生成初始敌人圆环。 */
	bool SpawnInitialEnemies(FText& OutFailureReason);

	/** 已经通过校验并被棋盘接收的地面结果。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|地面")
	TArray<FCastleGroundCell> GeneratedGroundCells;

	/** 每个被占用坐标对应的建筑 Actor，合成 Actor 会映射多个坐标。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "棋盘|建筑")
	TMap<FIntVector, TObjectPtr<ACastleBuildingBase>> BuildingByCoordinate;

	/** 获取六个面相邻坐标，不包含斜角坐标。 */
	void GetFaceNeighborCoordinates(const FIntVector& Coordinate, TArray<FIntVector>& OutNeighbors) const;

	/** 空源、未完成源或索引不完整是错误，不能伪装成没有邻居的成功查询。 */
	bool ValidateEffectQuerySource(const ACastleBuildingBase* Source) const;

	/** 在不改变 Actor 生命周期的情况下，把建筑的所有坐标注册到棋盘索引。 */
	bool RegisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason);

	/** 从棋盘索引移除建筑的所有坐标，但不销毁 Actor。 */
	bool UnregisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason);

	/** 在占用范围的底面中心生成并初始化建筑 Actor，尚未写入棋盘占用索引。 */
	ACastleBuildingBase* SpawnBuildingActor(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& OccupiedCoordinates, FText& OutFailureReason);

	/** 尝试对新放置的建筑连续执行可行的两两合成。 */
	ACastleBuildingBase* TryMergePlacedBuilding(ACastleBuildingBase* Building, const FVector& ChangeOrigin, FText& OutFailureReason);

	/** 放置完成前保存这次操作的合并意图和传播原点，避免后续放置改写前一次的上下文。 */
	struct FPendingPlacement
	{
		bool bTryMerge = false;
		FVector Origin = FVector::ZeroVector;
	};
	TMap<TWeakObjectPtr<ACastleBuildingBase>, FPendingPlacement> PendingPlacements;
	/** 已完成但邻居仍未完成的请求在之后的完成事件中重新检查，不使用轮询定时器。 */
	TMap<TWeakObjectPtr<ACastleBuildingBase>, FVector> ReadyMergeRequests;

	void HandlePlacementFinished(UAssemblyFlightComponent* Flight, bool bCompleted);
	void ResolveReadyMerges();
	void ForgetPlacement(ACastleBuildingBase* Building);
	UFUNCTION()
	void HandleBuildingDestroyed(AActor* Actor);

	/** 查找可以和当前建筑合成的相邻建筑。 */
	ACastleBuildingBase* FindMergePartner(ACastleBuildingBase* Building) const;

	/** 校验两个建筑的联合占用坐标是否符合当前等级规定的形状。 */
	bool BuildMergedFootprint(const ACastleBuildingBase* First, const ACastleBuildingBase* Second, TArray<FIntVector>& OutFootprint) const;

	/** 生成高一级 Actor 并替换坐标索引；旧 Actor 由整次连续合成抓取视觉后统一销毁。 */
	ACastleBuildingBase* MergeBuildings(ACastleBuildingBase* First, ACastleBuildingBase* Second, const TArray<FIntVector>& MergedFootprint, FText& OutFailureReason);

	/** 校验建筑描述和具体类默认对象是否一致。 */
	bool ValidateBuildingDescriptor(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const;

	/** 检查指定地面坐标是否存在，允许查询上层坐标对应的平面地面。 */
	bool TryGetGroundCellAtPlane(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const;

	/** 校验棋盘格实体尺寸、间隙、中心距和层高，拒绝无效配置而不自动修正。 */
	bool ValidateBoardDimensions(FText& OutFailureReason) const;

	/** 塔侧格归属于主塔 Actor；棋盘只查询由自身生成并登记的组件。 */
	UPROPERTY(Transient) TObjectPtr<UCastleTowerSideGridComponent> TowerSideGrid;

	/** 塔侧模式的中心整列不可被房间占用，在预览和真正登记时共同校验。 */
	bool IsMainTowerCellReserved(const FIntVector& Coordinate) const;

	/** 从唯一来源解析落点：地面顶面落在本格，塔侧格和建筑面落在外侧格。 */
	bool TryResolvePlacementCoordinate(const FCastleGridCursorTarget& CursorTarget, FIntVector& OutCoordinate, FText& OutFailureReason) const;

	/** 匹配指定来源；无光标的坐标入口仅使用下方顶面，绝不搜索其余邻居。 */
	bool HasPlacementMatch(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason) const;

	/** 公共校验入口，整次操作保留同一高亮面，防止背包扣牌或生成阶段丢失放置方向。 */
	bool CanPlaceBuildingInternal(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason) const;

	/** 校验后执行既有生成、占用、飞入和合成流程；光标来源只参与本次放置匹配。 */
	ACastleBuildingBase* PlaceBuildingInternal(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason);

	/** 扣牌前后都保留来源；失败不消费卡牌，也不改用其他面重试。 */
	ACastleBuildingBase* PlaceBuildingFromBackpackInternal(const FIntVector& Coordinate, UCastleBackpackComponent* Backpack, int32 BackpackIndex, bool bTryMerge, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason);
};
