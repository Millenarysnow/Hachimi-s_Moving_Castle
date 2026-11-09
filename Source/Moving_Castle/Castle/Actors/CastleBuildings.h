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
/** 箭塔建筑族的抽象基类，等级子类只定义 C++ 逻辑类型，模型与升级映射来自 Data Asset。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleArcherTowerBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	/** 设置箭塔类别。 */
	ACastleArcherTowerBuilding();

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

/** 炮塔建筑族的抽象基类，等级子类只定义 C++ 逻辑类型，模型与升级映射来自 Data Asset。 */
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

/** 农田建筑族的抽象基类，等级子类只定义 C++ 逻辑类型，模型与升级映射来自 Data Asset。 */
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

