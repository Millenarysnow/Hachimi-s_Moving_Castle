#pragma once

#include "Commandlets/Commandlet.h"
#include "HachimisV4ImportCommandlet.generated.h"

/** 由作者 FBX 与交付清单生成 V4 分件，也可独立导入结构套件与四格屋面调色板。 */
UCLASS()
class UHachimisV4ImportCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
