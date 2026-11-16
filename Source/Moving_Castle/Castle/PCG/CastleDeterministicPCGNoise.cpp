// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleDeterministicPCGNoise.h"
#include "Moving_Castle.h"

namespace CastlePCGNoise
{
	/** SplitMix 风格的 64 位雪崩混合，保证不同维度和通道参与同一纯函数哈希。 */
	uint32 Mix(uint64 Value)
	{
		Value += 0x9E3779B97F4A7C15ull;
		Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
		Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
		Value ^= Value >> 31;
		return static_cast<uint32>(Value >> 32);
	}

	uint64 Combine(uint64 State, uint64 Value)
	{
		return (State ^ (static_cast<uint64>(Mix(Value)) << 32 | Mix(Value ^ 0xD6E8FEB86659FD93ull)))
			* 0x9E3779B185EBCA87ull;
	}
}

float UCastleDeterministicPCGNoise::SampleNoiseValue(const int32 Seed, const FIntVector Cell, const FName Channel)
{
	uint64 Hash = 0x243F6A8885A308D3ull;
	Hash = CastlePCGNoise::Combine(Hash, static_cast<uint32>(Seed));
	Hash = CastlePCGNoise::Combine(Hash, static_cast<uint32>(Cell.X));
	Hash = CastlePCGNoise::Combine(Hash, static_cast<uint32>(Cell.Y));
	Hash = CastlePCGNoise::Combine(Hash, static_cast<uint32>(Cell.Z));
	// FName 的名称表索引可能随加载顺序变化；按大小写无关的字符编码哈希才能跨进程复现。
	const FString ChannelText = Channel.ToString().ToLower();
	for (const TCHAR Character : ChannelText)
	{
		Hash = CastlePCGNoise::Combine(Hash, static_cast<uint32>(Character));
	}
	// 取 24 位整数再乘 2^-24，结果可精确表示且严格小于 1。
	return static_cast<float>(CastlePCGNoise::Mix(Hash) >> 8) * (1.0f / 16777216.0f);
}

float UCastleDeterministicPCGNoise::SampleNoise_Implementation(const int32 Seed, const FIntVector Cell, const FName Channel) const
{
	return SampleNoiseValue(Seed, Cell, Channel);
}

bool UCastleDeterministicPCGNoise::SampleWeightedIndex(const int32 Seed, const FIntVector Cell, const FName Channel,
	const TArray<float>& Weights, int32& OutIndex) const
{
	OutIndex = INDEX_NONE;
	if (Weights.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑PCG权重采样失败：权重列表为空，通道 %s。"), *Channel.ToString());
		return false;
	}

	double TotalWeight = 0.0;
	for (const float Weight : Weights)
	{
		if (!FMath::IsFinite(Weight) || Weight < 0.0f)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑PCG权重采样失败：通道 %s 含非法权重。"), *Channel.ToString());
			return false;
		}
		TotalWeight += Weight;
	}
	if (!FMath::IsFinite(TotalWeight) || TotalWeight <= UE_DOUBLE_SMALL_NUMBER)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑PCG权重采样失败：通道 %s 的权重和必须大于零。"), *Channel.ToString());
		return false;
	}

	const double Target = static_cast<double>(SampleNoiseValue(Seed, Cell, Channel)) * TotalWeight;
	double Cumulative = 0.0;
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		Cumulative += Weights[Index];
		if (Target < Cumulative)
		{
			OutIndex = Index;
			return true;
		}
	}
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑PCG权重采样失败：通道 %s 的累积权重不覆盖采样值。"), *Channel.ToString());
	return false;
}
