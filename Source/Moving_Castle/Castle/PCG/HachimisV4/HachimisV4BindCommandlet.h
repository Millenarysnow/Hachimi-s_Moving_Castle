#pragma once

#include "Commandlets/Commandlet.h"
#include "HachimisV4BindCommandlet.generated.h"

/** 将已导入的装配绑定到 34 个现有蓝图及共享零件目录。 */
UCLASS()
class UHachimisV4BindCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
