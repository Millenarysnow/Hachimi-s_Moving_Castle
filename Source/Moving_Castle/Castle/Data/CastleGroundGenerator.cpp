// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGroundGenerator.h"

#include "Moving_Castle.h"
#include "Castle/PCG/CastleDeterministicPCGNoise.h"

FCastleGroundGenerationResult UCastleGroundGenerator::GenerateGround_Implementation(const FCastleGroundGenerationRequest& Request) const
{
	FCastleGroundGenerationResult EmptyResult;

	if (!Request.IsValid())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("地面生成失败：请求尺寸无效，GridSize=(%d, %d, %d)。"), Request.GridSize.X, Request.GridSize.Y, Request.GridSize.Z);
		return EmptyResult;
	}

	UE_LOG(LogMoving_Castle, Error, TEXT("地面生成失败：没有提供具体的生成策略类。"));
	return EmptyResult;
}
UCastleRandomGroundGenerator::UCastleRandomGroundGenerator()
{
	// 包含原点在内的所有地面格默认从正式玩法色集合采样。
	FunctionalColors.Append(CastleTileColors::GetPlayable());
}

FCastleGroundGenerationResult UCastleRandomGroundGenerator::GenerateGround_Implementation(const FCastleGroundGenerationRequest& Request) const
{
	FCastleGroundGenerationResult Result;

	if (!Request.IsValid())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成失败：请求尺寸无效，GridSize=(%d, %d, %d)。"), Request.GridSize.X, Request.GridSize.Y, Request.GridSize.Z);
		return Result;
	}

	if (FunctionalColors.Num() == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成失败：功能颜色池为空。"));
		return Result;
	}

	for (const ECastleTileColor Color : FunctionalColors)
	{
		if (!CastleTileColors::IsPlayable(Color))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成器 %s 的颜色池含无效色值 %d。请使用红、黄、蓝、绿配置。"),
				*GetPathName(), static_cast<int32>(Color));
			return Result;
		}
	}

	if (MinNumber < 1 || MaxNumber > 9 || MinNumber > MaxNumber)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成失败：数字范围无效，MinNumber=%d，MaxNumber=%d。"), MinNumber, MaxNumber);
		return Result;
	}

	// 每格独立采样，固定通道字符串只参与稳定字符哈希，不依赖棋盘宽度或遍历顺序。
	Result.Cells.Reserve(Request.GridSize.X * Request.GridSize.Y);

	for (int32 Y = 0; Y < Request.GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < Request.GridSize.X; ++X)
		{
			FCastleGroundCell Cell;
			Cell.Coordinate = Request.Origin + FIntVector(X, Y, 0);

			// 颜色与数字分别采样，缩减颜色集合时数字通道仍可独立复现。
			const FIntVector SampleCell(Cell.Coordinate.X, Cell.Coordinate.Y, 0);
			const int32 ColorIndex = FMath::FloorToInt(UCastleDeterministicPCGNoise::SampleNoiseValue(
				Request.Seed, SampleCell, TEXT("GroundColor")) * FunctionalColors.Num());
			Cell.Color = FunctionalColors[ColorIndex];
			Cell.Number = MinNumber + FMath::FloorToInt(UCastleDeterministicPCGNoise::SampleNoiseValue(
				Request.Seed, SampleCell, TEXT("GroundNumber")) * (MaxNumber - MinNumber + 1));

			Result.Cells.Add(Cell);
		}
	}

	return Result;
}

