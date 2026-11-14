// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CastleBuildingBase.h"
#include "CastleBuildings.generated.h"

/** 道路建筑没有合成等级，只负责作为人员路线的逻辑占用物。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleRoadBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	/** 设置道路类别和固定的 1 级属性。 */
	ACastleRoadBuilding();
};
/** 箭塔建筑族的抽象基类；等级蓝图直接继承对应 C++ 等级类，生成映射由 Data Asset 配置。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	/** 设置箭塔类别。 */
	explicit ACastleArcherTowerBuilding(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

};

/** 箭塔 1 级，默认占用一个网格。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerLevel1 : public ACastleArcherTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleArcherTowerLevel1();
};

/** 箭塔 2 级，默认由两个水平网格合成。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerLevel2 : public ACastleArcherTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleArcherTowerLevel2();
};

/** 箭塔 3 级，默认由四个网格合成。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerLevel3 : public ACastleArcherTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleArcherTowerLevel3();
};

/** 箭塔 4 级，默认由八个网格合成立体方块。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerLevel4 : public ACastleArcherTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleArcherTowerLevel4();
};

/** 箭塔 5 级，供后续奖励系统直接替换生成。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerLevel5 : public ACastleArcherTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleArcherTowerLevel5();
};

/** 炮塔建筑族的抽象基类；等级蓝图直接继承对应 C++ 等级类，生成映射由 Data Asset 配置。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleCannonTowerBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	/** 设置炮塔类别。 */
	ACastleCannonTowerBuilding();

};

/** 炮塔 1 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleCannonTowerLevel1 : public ACastleCannonTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleCannonTowerLevel1();
};

/** 炮塔 2 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleCannonTowerLevel2 : public ACastleCannonTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleCannonTowerLevel2();
};

/** 炮塔 3 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleCannonTowerLevel3 : public ACastleCannonTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleCannonTowerLevel3();
};

/** 炮塔 4 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleCannonTowerLevel4 : public ACastleCannonTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleCannonTowerLevel4();
};

/** 炮塔 5 级，供后续奖励系统直接替换生成。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleCannonTowerLevel5 : public ACastleCannonTowerBuilding
{
	GENERATED_BODY()

public:
	ACastleCannonTowerLevel5();
};

/** 农田建筑族的抽象基类；等级蓝图直接继承对应 C++ 等级类，生成映射由 Data Asset 配置。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleFarmBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	/** 设置农田类别。 */
	ACastleFarmBuilding();

};

/** 农田 1 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleFarmLevel1 : public ACastleFarmBuilding
{
	GENERATED_BODY()

public:
	ACastleFarmLevel1();
};

/** 农田 2 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleFarmLevel2 : public ACastleFarmBuilding
{
	GENERATED_BODY()

public:
	ACastleFarmLevel2();
};

/** 农田 3 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleFarmLevel3 : public ACastleFarmBuilding
{
	GENERATED_BODY()

public:
	ACastleFarmLevel3();
};

/** 农田 4 级。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleFarmLevel4 : public ACastleFarmBuilding
{
	GENERATED_BODY()

public:
	ACastleFarmLevel4();
};

/** 农田 5 级，供后续奖励系统直接替换生成。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleFarmLevel5 : public ACastleFarmBuilding
{
	GENERATED_BODY()

public:
	ACastleFarmLevel5();
};

/**
 * 盾塔建筑族的抽象基类，与炮塔、农田共用放置、合成、回收和触发框架。
 * 当前阶段保留基础 BuildingEffect 的触发计数，不实现护盾、嘲讽、抵挡或反伤。
 * 等级蓝图直接继承下方对应 C++ 类，美术和功能地板由蓝图显式配置。
 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleShieldTowerBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	/** 只设置盾塔类别，具体等级由各等级子类确定。 */
	ACastleShieldTowerBuilding();
};

/** 盾塔 1 级，占用一个网格，作为商店与开局卡牌的建筑类。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleShieldTowerLevel1 : public ACastleShieldTowerBuilding
{
	GENERATED_BODY()

public:
	/** 固定本类的一级身份，不在运行时推断等级。 */
	ACastleShieldTowerLevel1();
};

/** 盾塔 2 级，占用两个网格，允许沿用现有水平或垂直合成形状。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleShieldTowerLevel2 : public ACastleShieldTowerBuilding
{
	GENERATED_BODY()

public:
	/** 固定本类的二级身份，占用由统一棋盘规则计算。 */
	ACastleShieldTowerLevel2();
};

/** 盾塔 3 级，占用四个网格，合成时保留输入建筑实际形成的平面。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleShieldTowerLevel3 : public ACastleShieldTowerBuilding
{
	GENERATED_BODY()

public:
	/** 固定本类的三级身份，不另建盾塔专用合成流程。 */
	ACastleShieldTowerLevel3();
};

/** 盾塔 4 级，占用八格立方体，使用现有自动合成上限。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleShieldTowerLevel4 : public ACastleShieldTowerBuilding
{
	GENERATED_BODY()

public:
	/** 固定本类的四级身份，表现尺寸由等级蓝图保留。 */
	ACastleShieldTowerLevel4();
};

/** 盾塔 5 级，与其他塔一致预留奖励升级身份，不新增四升五自动合成。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleShieldTowerLevel5 : public ACastleShieldTowerBuilding
{
	GENERATED_BODY()

public:
	/** 固定本类的五级身份，供显式创建五级卡或后续奖励系统使用。 */
	ACastleShieldTowerLevel5();
};

