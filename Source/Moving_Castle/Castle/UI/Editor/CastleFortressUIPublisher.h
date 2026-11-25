#pragma once

#include "Commandlets/Commandlet.h"
#include "CastleFortressUIPublisher.generated.h"

#if WITH_EDITOR
/** 教程配置重新生成时复用正式弹窗布局，返回编译后的类。 */
UClass* PublishCastleFortressTutorialPopup(FName Name);
#endif

/** 离线生成正式弹窗资产的编辑器入口，玩法蓝图不调用此工具。 */
UCLASS()
class UCastleFortressUIPublisherCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	/** 验证纹理并编译全部十一套 Widget Blueprint 后保存。 */
	virtual int32 Main(const FString& Params) override;
};
