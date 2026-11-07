// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CastleGameplayTypes.generated.h"

class ACastleBuildingBase;

/**
 * 地面和房间共用的颜色枚举。
 *
 * 这里保留灰色是为了表达道路的特殊属性；道路并不是普通的功能房间，
 * 因此棋盘在判断放置匹配时会单独处理灰色 0 号道路。
 */
UENUM(BlueprintType)
enum class ECastleTileColor : uint8
{
	Gray UMETA(DisplayName = "灰色"),
	Red UMETA(DisplayName = "红色"),
	Blue UMETA(DisplayName = "蓝色"),
	Green UMETA(DisplayName = "绿色"),
	Yellow UMETA(DisplayName = "黄色"),
	Purple UMETA(DisplayName = "紫色")
};
/**
 * 当前阶段实现的建筑类别。
 *
 * 敌人、NPC 和战斗逻辑不属于本阶段，所以这里仅描述棋盘和背包需要识别的
 * 道路、箭塔、炮塔、农田四类建筑。
 */
UENUM(BlueprintType)
enum class ECastleBuildingType : uint8
{
	Road UMETA(DisplayName = "道路"),
	ArcherTower UMETA(DisplayName = "箭塔"),
	CannonTower UMETA(DisplayName = "炮塔"),
	Farm UMETA(DisplayName = "农田")
};

/**
 * 网格单元的六个可吸附面。
 *
 * 该枚举只描述射线当前命中的面，不直接决定是否允许放置建筑；放置规则
 * 仍然由棋盘统一校验，控制器只负责把玩家视线转换成稳定的网格目标。
 */
UENUM(BlueprintType)
enum class ECastleGridFace : uint8
{
	Up UMETA(DisplayName = "上"),
	Down UMETA(DisplayName = "下"),
	PositiveX UMETA(DisplayName = "+X"),
	NegativeX UMETA(DisplayName = "-X"),
	PositiveY UMETA(DisplayName = "+Y"),
	NegativeY UMETA(DisplayName = "-Y")
};

/**
 * 屏幕中心射线解析后的网格目标。
 *
 * Board 负责产生这份结果，PlayerController 只消费它来更新可视化指示器。
 * IndicatorTransform 已经包含棋盘 Actor 的世界变换，因此棋盘旋转或缩放
 * 时指示平面仍会与实际网格保持一致。
 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleGridCursorTarget
{
	GENERATED_BODY()

	/** 当前射线命中的格子坐标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	FIntVector Cell = FIntVector::ZeroValue;

	/** 当前命中的格子面。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	ECastleGridFace Face = ECastleGridFace::Up;

	/** 面外侧紧邻的候选坐标，供后续放置系统直接使用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	FIntVector AdjacentCell = FIntVector::ZeroValue;

	/** 指示平面在世界中的法线方向。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	FVector WorldNormal = FVector::UpVector;

	/** 指示平面的完整世界变换，包含位置、朝向和尺寸。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	FTransform IndicatorTransform = FTransform::Identity;

	/** 射线是否成功吸附到棋盘网格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	bool bValid = false;
};

/**
 * 单个地面格子的生成结果。
 *
 * 地面生成器只负责产生这份纯数据，棋盘不会反过来修改生成器的策略。
 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleGroundCell
{
	GENERATED_BODY()

	/** 地面格子的整数网格坐标。当前地面生成阶段只使用 Z=0。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面")
	FIntVector Coordinate = FIntVector::ZeroValue;

	/** 该格子的固定颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面")
	ECastleTileColor Color = ECastleTileColor::Gray;

	/** 该格子的固定数字，正常功能地面范围为 1 到 9，道路使用 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面")
	int32 Number = 0;

	/** 判断该地面格子是否是灰色 0 号道路起始格。 */
	bool IsRoadTile() const
	{
		return Color == ECastleTileColor::Gray && Number == 0;
	}
};

/**
 * 传给地面生成器的请求参数。
 *
 * GridSize 描述棋盘边界，生成器可以根据自己的策略决定每个格子的内容，
 * 但必须返回同一范围内完整且不重复的地面结果。
 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleGroundGenerationRequest
{
	GENERATED_BODY()

	/** 棋盘尺寸，X 和 Y 必须为正数，Z 表示允许建筑堆叠的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面生成")
	FIntVector GridSize = FIntVector(10, 10, 8);

	/** 棋盘网格原点，生成结果中的坐标会从该坐标开始。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面生成")
	FIntVector Origin = FIntVector::ZeroValue;

	/** 随机策略使用的种子；相同参数应得到相同结果，便于复现问题。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面生成")
	int32 Seed = 1337;

	/** 是否要求生成器在原点保留一个灰色 0 号起始格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面生成")
	bool bReserveRoadStartTile = true;

	/** 检查生成请求是否具备有效的棋盘平面尺寸。 */
	bool IsValid() const
	{
		return GridSize.X > 0 && GridSize.Y > 0 && GridSize.Z > 0;
	}
};

/**
 * 地面生成器返回给棋盘的完整结果。
 *
 * 使用数组而不是直接暴露棋盘内部的 TMap，保证生成器和棋盘之间只有一个
 * 明确的数据边界；棋盘在接收时会检查重复坐标、越界和数量是否完整。
 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleGroundGenerationResult
{
	GENERATED_BODY()

	/** 本次生成的所有地面格子。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地面生成")
	TArray<FCastleGroundCell> Cells;

	/** 在结果数组中查找指定坐标的地面格子。 */
	bool FindCell(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const
	{
		for (const FCastleGroundCell& Cell : Cells)
		{
			if (Cell.Coordinate == Coordinate)
			{
				OutCell = Cell;
				return true;
			}
		}

		return false;
	}
};

/**
 * 建筑卡牌和建筑 Actor 共用的最小描述。
 *
 * 背包只保存这份描述，不保存 Actor 指针；从背包放置时由棋盘按照描述重新
 * 生成 Actor，这也是后续实现合成替换 Actor 的基础。
 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBuildingDescriptor
{
	GENERATED_BODY()

	/** 用于生成建筑 Actor 的具体类，必须是 ACastleBuildingBase 的子类。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑")
	TSubclassOf<ACastleBuildingBase> BuildingClass;

	/** 建筑的类别，必须和 BuildingClass 的默认类别一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑")
	ECastleBuildingType BuildingType = ECastleBuildingType::Road;

	/** 建筑颜色。道路必须使用灰色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑")
	ECastleTileColor Color = ECastleTileColor::Gray;

	/** 建筑数字。道路必须使用 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑")
	int32 Number = 0;

	/** 建筑等级。道路固定为 1，功能建筑由具体等级子类决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑")
	int32 Level = 1;

	/** 判断描述是否至少指定了一个可生成的建筑类。详细属性校验由棋盘完成。 */
	bool HasBuildingClass() const;
};

