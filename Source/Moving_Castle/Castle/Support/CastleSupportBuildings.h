#pragma once

#include "CastleBuildingBase.h"
#include "CastleSupportBuildings.generated.h"

/** 两个建筑族均使用独立身份和原生效果；等级蓝图只持有美术组件。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleBeeHiveBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()
public:
	explicit ACastleBeeHiveBuilding(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBeeHiveLevel1 : public ACastleBeeHiveBuilding
{
	GENERATED_BODY()
public:
	ACastleBeeHiveLevel1();
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBeeHiveLevel2 : public ACastleBeeHiveBuilding
{
	GENERATED_BODY()
public:
	ACastleBeeHiveLevel2();
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBeeHiveLevel3 : public ACastleBeeHiveBuilding
{
	GENERATED_BODY()
public:
	ACastleBeeHiveLevel3();
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBeeHiveLevel4 : public ACastleBeeHiveBuilding
{
	GENERATED_BODY()
public:
	ACastleBeeHiveLevel4();
};

UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleBalloonTowerBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()
public:
	explicit ACastleBalloonTowerBuilding(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBalloonTowerLevel1 : public ACastleBalloonTowerBuilding
{
	GENERATED_BODY()
public:
	ACastleBalloonTowerLevel1();
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBalloonTowerLevel2 : public ACastleBalloonTowerBuilding
{
	GENERATED_BODY()
public:
	ACastleBalloonTowerLevel2();
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBalloonTowerLevel3 : public ACastleBalloonTowerBuilding
{
	GENERATED_BODY()
public:
	ACastleBalloonTowerLevel3();
};

UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleBalloonTowerLevel4 : public ACastleBalloonTowerBuilding
{
	GENERATED_BODY()
public:
	ACastleBalloonTowerLevel4();
};
