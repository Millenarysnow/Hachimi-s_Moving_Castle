#pragma once

#include "Commandlets/Commandlet.h"
#include "CastleTutorialAssetsCommandlet.generated.h"

/** 离线构建正式教学地图、对话数据和纯布局蓝图。需要指定正式立绘源目录。 */
UCLASS()
class UCastleTutorialAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
