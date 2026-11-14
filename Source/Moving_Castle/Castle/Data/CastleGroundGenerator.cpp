// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGroundGenerator.h"

#include "Moving_Castle.h"

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
	// 原点和其他地面统一从五色池生成，不再设置特殊起始格。
	FunctionalColors = {
		ECastleTileColor::Red,
		ECastleTileColor::Blue,
		ECastleTileColor::Green,
		ECastleTileColor::Yellow,
		ECastleTileColor::Purple
	};
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
		if (Color < ECastleTileColor::Red || Color > ECastleTileColor::Purple)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成失败：功能颜色池必须只包含红、蓝、绿、黄、紫五种有效颜色。"));
			return Result;
		}
	}

	if (MinNumber < 1 || MaxNumber > 9 || MinNumber > MaxNumber)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成失败：数字范围无效，MinNumber=%d，MaxNumber=%d。"), MinNumber, MaxNumber);
		return Result;
	}

	// FRandomStream 保证同一请求种子产生完全一致的结果，便于复现布局和调试放置规则。
	FRandomStream RandomStream(Request.Seed);
	Result.Cells.Reserve(Request.GridSize.X * Request.GridSize.Y);

	for (int32 Y = 0; Y < Request.GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < Request.GridSize.X; ++X)
		{
			FCastleGroundCell Cell;
			Cell.Coordinate = Request.Origin + FIntVector(X, Y, 0);

			// 所有格子统一生成五色 1～9 数字，包含棋盘原点。
			const int32 ColorIndex = RandomStream.RandRange(0, FunctionalColors.Num() - 1);
			Cell.Color = FunctionalColors[ColorIndex];
			Cell.Number = RandomStream.RandRange(MinNumber, MaxNumber);

			Result.Cells.Add(Cell);
		}
	}

	return Result;
}

