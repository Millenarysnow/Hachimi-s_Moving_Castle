#pragma once

#include "CoreMinimal.h"
#include "CastleBadgeTypes.generated.h"

/** 徽章的玩法身份。同一类型可占用多个槽位。 */
UENUM(BlueprintType)
enum class ECastleBadgeType : uint8
{
	GambleLuck UMETA(DisplayName = "赌博幸运"),
	BlueRoomNumber UMETA(DisplayName = "蓝色房间数字"),
	UpgradeRoomCards UMETA(DisplayName = "功能塔升级赠牌"),
	MainTowerHealth UMETA(DisplayName = "主塔生命"),
	ArcherFireRate UMETA(DisplayName = "箭塔射速"),
	FirstMainTowerDamage UMETA(DisplayName = "首次受伤加速"),
	LowMainTowerHealth UMETA(DisplayName = "低血量压制")
};

/** 可供奖励资产配置的单枚徽章。整数增益必须填整数，触发式徽章使用固定倍率。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBadgeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	ECastleBadgeType Type = ECastleBadgeType::GambleLuck;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	float Magnitude = 0.15f;

	/** 取得指定种类的规则初值，供调试奖励与资产配置使用。 */
	static bool TryMakeStandard(ECastleBadgeType InType, FCastleBadgeDefinition& OutBadge);

	/** 拒绝超出该类型契约的配置，防止效果查询隐式截断或替换参数。 */
	bool Validate(FText& OutFailureReason) const;

	/** 用同一份效果数值生成预览及已持有效果文案。 */
	FText GetDisplayName() const;
	FText GetEffectSummary() const;
};
