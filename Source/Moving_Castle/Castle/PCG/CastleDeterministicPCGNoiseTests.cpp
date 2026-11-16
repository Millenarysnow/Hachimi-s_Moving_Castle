// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleDeterministicPCGNoise.h"
#include "Misc/AutomationTest.h"

/** 同一语义位置的噪声与调用顺序无关，且种子、坐标和通道都参与结果。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDeterministicPCGNoiseTest,
	"MovingCastle.Castle.PCG.DeterministicNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDeterministicPCGNoiseTest::RunTest(const FString& Parameters)
{
	const FIntVector Cell(4, -2, 7);
	const float Expected = UCastleDeterministicPCGNoise::SampleNoiseValue(183, Cell, TEXT("LadderPlacement"));
	TestTrue(TEXT("噪声落在 [0,1) 范围"), Expected >= 0.0f && Expected < 1.0f);
	for (int32 Index = 0; Index < 128; ++Index)
	{
		UCastleDeterministicPCGNoise::SampleNoiseValue(Index, FIntVector(Index, -Index, Index % 5), TEXT("Decoration"));
	}
	TestEqual(TEXT("插入其他查询不改变原位置噪声"), UCastleDeterministicPCGNoise::SampleNoiseValue(183, Cell, TEXT("LadderPlacement")), Expected);
	TestNotEqual(TEXT("改变种子会改变测试样本"), UCastleDeterministicPCGNoise::SampleNoiseValue(184, Cell, TEXT("LadderPlacement")), Expected);
	TestNotEqual(TEXT("改变坐标会改变测试样本"), UCastleDeterministicPCGNoise::SampleNoiseValue(183, Cell + FIntVector(1, 0, 0), TEXT("LadderPlacement")), Expected);
	TestNotEqual(TEXT("改变语义通道会改变测试样本"), UCastleDeterministicPCGNoise::SampleNoiseValue(183, Cell, TEXT("Decoration")), Expected);

	TArray<float> Weights{0.0f, 1.0f, 0.0f};
	int32 Selected = INDEX_NONE;
	UCastleDeterministicPCGNoise* Noise = NewObject<UCastleDeterministicPCGNoise>();
	TestTrue(TEXT("有效权重采样成功"), Noise->SampleWeightedIndex(183, Cell, TEXT("LadderPlacement"), Weights, Selected));
	TestEqual(TEXT("零权重项永不被选中"), Selected, 1);

	// 空间采样使用宽容的统计区间，检查明显偏置而不以单个随机样本充当均匀性证明。
	int32 Bins[8] = {};
	for (int32 Index = 0; Index < 8192; ++Index)
	{
		const float Value = UCastleDeterministicPCGNoise::SampleNoiseValue(183, FIntVector(Index, Index % 31, Index % 7), TEXT("Distribution"));
		TestTrue(TEXT("每个空间样本均处于合法范围"), Value >= 0.0f && Value < 1.0f);
		++Bins[static_cast<int32>(Value * 8.0f)];
	}
	for (const int32 Count : Bins)
	{
		TestTrue(TEXT("八个区间的样本数量无明显分布偏置"), Count > 850 && Count < 1200);
	}
	return true;
}

#endif
