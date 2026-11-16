// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CastleDeterministicPCGNoise.generated.h"

/** 可替换的确定性空间噪声来源；实现必须只依赖输入，不能保留递增随机状态。 */
UINTERFACE(BlueprintType)
class MOVING_CASTLE_API UCastlePCGNoiseProvider : public UInterface
{
	GENERATED_BODY()
};

class MOVING_CASTLE_API ICastlePCGNoiseProvider
{
	GENERATED_BODY()

public:
	/** 对相同种子、三维格坐标和通道名必须返回相同的 [0,1) 值。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="建筑PCG|噪声")
	float SampleNoise(int32 Seed, FIntVector Cell, FName Channel) const;
};

/** 项目内默认实现：使用稳定整数混合哈希，不读取全局或有状态随机数流。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleDeterministicPCGNoise : public UObject, public ICastlePCGNoiseProvider
{
	GENERATED_BODY()

public:
	virtual float SampleNoise_Implementation(int32 Seed, FIntVector Cell, FName Channel) const override;

	/** 按输入权重抽取一个索引；权重总和无效时返回失败，不自动改用均匀分布。 */
	UFUNCTION(BlueprintCallable, Category="建筑PCG|噪声")
	bool SampleWeightedIndex(int32 Seed, FIntVector Cell, FName Channel, const TArray<float>& Weights, int32& OutIndex) const;

	/** 纯函数入口便于规则求值和不依赖 UObject 生命周期的自动化验证。 */
	static float SampleNoiseValue(int32 Seed, FIntVector Cell, FName Channel);
};
