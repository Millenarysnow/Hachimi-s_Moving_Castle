#include "CastlePlayerController.h"

#include "CastleMassSpawner.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"

void ACastlePlayerController::CastleSpawnPatrolNPCs(int32 Count)
{
	// 这是明确的本地验收入口，生产波次可继续直接使用 CastleMassSpawner。
	if (Count <= 0)
	{
		ShowGameplayMessage(TEXT("NPC 数量必须大于 0。"), false);
		return;
	}
	UMassEntityConfigAsset* Configuration = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassNPCBase"));
	if (!Configuration)
	{
		ShowGameplayMessage(TEXT("未找到 DA_MassNPCBase，无法生成可见巡逻 NPC。"), false);
		return;
	}
	if (IsValid(PatrolPreviewSpawner)) { PatrolPreviewSpawner->Destroy(); }
	PatrolPreviewSpawner = GetWorld()->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), FTransform::Identity, this);
	if (!PatrolPreviewSpawner)
	{
		ShowGameplayMessage(TEXT("无法创建共享巡逻生成器。"), false);
		return;
	}
	PatrolPreviewSpawner->Configuration = Configuration;
	PatrolPreviewSpawner->Count = Count;
	PatrolPreviewSpawner->bFollowSharedPatrolRoute = true;
	PatrolPreviewSpawner->FinishSpawning(FTransform::Identity);
	ShowGameplayMessage(FString::Printf(TEXT("已设置 %d 个巡逻 NPC；整条路线就绪后沿路线生成。"), Count), true);
}
