#pragma once

#include "CoreMinimal.h"
#include "CastleAudioTypes.generated.h"

/** 游戏实例只持有一条 BGM；None 显式停止当前曲目。 */
UENUM(BlueprintType)
enum class ECastleBGMTrack : uint8
{
	None,
	MainMenu,
	Build,
	Battle,
	SkyIsland
};

/** 按玩法事件选择 2D 音效；主塔受击事件有两条录音。 */
UENUM(BlueprintType)
enum class ECastleSFXEvent : uint8
{
	Interact,
	ClosePanel,
	Gamble,
	Defeat,
	Build,
	Victory,
	Demolish,
	BowShot,
	Button,
	AttackMainTower,
	BalloonPop,
	CannonFire,
	ArrowFlight,
	CrateLanded,
	PageTurn,
	Reward,
	Gold,
	Bees,
	EnterGame,
	DragonFire,
	Count UMETA(Hidden)
};
