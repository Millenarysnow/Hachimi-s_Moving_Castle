#pragma once

#include "Commandlets/Commandlet.h"
#include "HachimisV4ImportCommandlet.generated.h"

/** 由作者 FBX、清点结果及几何清单生成可复用的 UE 分件资源。 */
UCLASS()
class UHachimisV4ImportCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
