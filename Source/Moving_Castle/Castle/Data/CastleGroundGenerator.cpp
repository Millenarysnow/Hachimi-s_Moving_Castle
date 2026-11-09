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
	// 默认颜色池与策划案中的功能房间颜色一致；灰色道路由起始格逻辑单独处理。
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
		if (Color == ECastleTileColor::Gray)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("随机地面生成失败：功能颜色池不能包含灰色，道路起始格由策略单独生成。"));
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

			// 原点作为道路起始格，保证道路在随机功能地面上始终存在合法的第一步。
			if (Request.bReserveRoadStartTile && Cell.Coordinate == Request.Origin)
			{
				Cell.Color = ECastleTileColor::Gray;
				Cell.Number = 0;
			}
			else
			{
				const int32 ColorIndex = RandomStream.RandRange(0, FunctionalColors.Num() - 1);
				Cell.Color = FunctionalColors[ColorIndex];
				Cell.Number = RandomStream.RandRange(MinNumber, MaxNumber);
			}

			Result.Cells.Add(Cell);
		}
	}

	return Result;
}

