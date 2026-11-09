// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CastleGameplayTypes.h"
#include "CastleGroundGenerator.generated.h"

/**
 * 地面生成策略的抽象基类。
 *
 * 棋盘只依赖这个接口的生成结果，不依赖任何一种具体随机算法。后续如果
 * 需要固定模板、房间权重或关卡特定生成，只需增加新的子类即可。
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class MOVING_CASTLE_API UCastleGroundGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 根据请求生成完整地面数据。
	 *
	 * 基类实现只负责拒绝无效请求；具体策略必须在子类中覆盖该实现并返回
	 * 完整的 FCastleGroundGenerationResult。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "移动城堡|地面生成")
	FCastleGroundGenerationResult GenerateGround(const FCastleGroundGenerationRequest& Request) const;

	/** BlueprintNativeEvent 的 C++ 默认实现。 */
	virtual FCastleGroundGenerationResult GenerateGround_Implementation(const FCastleGroundGenerationRequest& Request) const;
};
/**
 * 当前阶段使用的随机地面生成策略。
 *
 * 该策略只生成逻辑数据，不创建任何 Static Mesh、PCG Graph 或材质，因此
 * 可以在没有美术资源的情况下先验证棋盘和放置规则。
 */
UCLASS(Blueprintable, BlueprintType)
class MOVING_CASTLE_API UCastleRandomGroundGenerator : public UCastleGroundGenerator
{
	GENERATED_BODY()

public:
	/** 构造函数会设置默认的功能颜色池。 */
	UCastleRandomGroundGenerator();

	/** 随机地面可使用的功能颜色，灰色不会从该池中随机选取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "随机地面")
	TArray<ECastleTileColor> FunctionalColors;

	/** 随机地面数字的最小值，正常范围是 1 到 9。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "随机地面", meta = (ClampMin = "1", ClampMax = "9"))
	int32 MinNumber = 1;

	/** 随机地面数字的最大值，正常范围是 1 到 9。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "随机地面", meta = (ClampMin = "1", ClampMax = "9"))
	int32 MaxNumber = 9;

	/** 生成随机地面结果的 C++ 实现。 */
	virtual FCastleGroundGenerationResult GenerateGround_Implementation(const FCastleGroundGenerationRequest& Request) const override;
};

