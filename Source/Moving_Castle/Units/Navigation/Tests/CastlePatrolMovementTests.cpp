#if WITH_DEV_AUTOMATION_TESTS

#include "CastleNavigationSubsystem.h"
#include "CastleMassTypes.h"
#include "Mass/CastlePatrolMovement.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleLadderSpeedBoundaryTest, "MovingCastle.Navigation.LadderSpeedBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleLadderSpeedBoundaryTest::RunTest(const FString& Parameters)
{
	// 1000cm 闭环中只有 [100,300) 是梯子，预期由各段独立的路程/速度求得。
	FCastleSharedNavigationPath Path;
	Path.Length = 1000.0;
	FCastleLadderPathRange& Range = Path.LadderRanges.AddDefaulted_GetRef();
	Range.StartDist = 100.0;
	Range.EndDist = 300.0;
	FCastleMassMovementSettings Settings;
	Settings.MaxSpeed = 100.0f;
	Settings.Acceleration = 0.0f;
	Settings.LadderSpeedMultiplier = 0.5f;
	double Speed = 0.0;
	double Travel = 0.0;
	TestTrue(TEXT("长帧进入梯子可积分"), CastlePatrolMovement::Advance(Path, Settings, 90.0, 1.0, Speed, Travel));
	TestEqual(TEXT("先走平地10cm，再爬45cm"), Travel, 55.0);
	CastlePatrolMovement::Advance(Path, Settings, 290.0, 1.0, Speed, Travel);
	TestEqual(TEXT("先爬10cm耗0.2秒，剩余0.8秒走平地80cm"), Travel, 90.0);
	Settings.LadderSpeedMultiplier = 2.0f;
	CastlePatrolMovement::Advance(Path, Settings, 290.0, 1.0, Speed, Travel);
	TestEqual(TEXT("梯子翻倍不会把倍率带到后面的平地"), Travel, 105.0);
	Settings.LadderSpeedMultiplier = 0.5f;
	CastlePatrolMovement::Advance(Path, Settings, 990.0, 2.0, Speed, Travel);
	TestEqual(TEXT("跨圈后仍在正确梯子边界切速"), Travel, 155.0);
	Settings.LadderSpeedMultiplier = 0.0f;
	TestFalse(TEXT("非法倍率明确失败"), CastlePatrolMovement::Advance(Path, Settings, 90.0, 1.0, Speed, Travel));
	TestEqual(TEXT("非法倍率单位只到梯口，不进入梯子"), Travel, 10.0);
	TestEqual(TEXT("非法倍率停止速度"), Speed, 0.0);
	Settings.LadderSpeedMultiplier = 0.5f;
	Settings.Acceleration = 1000.0f;
	TestTrue(TEXT("梯子加速积分成功"), CastlePatrolMovement::Advance(Path, Settings, 100.0, 1.0, Speed, Travel));
	TestTrue(TEXT("达到50cm/s前后分别积分，得到48.75cm"), FMath::IsNearlyEqual(Travel, 48.75, 0.00001));
	return true;
}

#endif
