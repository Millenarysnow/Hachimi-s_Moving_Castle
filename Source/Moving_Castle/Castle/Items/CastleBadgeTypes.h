#pragma once

#include "CoreMinimal.h"
#include "CastleBadgeTypes.generated.h"

class UTexture2D;

/** 徽章的玩法身份。资产按名称配置，枚举序号独立于图标目录编号。 */
UENUM(BlueprintType)
enum class ECastleBadgeType : uint8
{
	None = 0 UMETA(Hidden),
	// 六种持续使用的效果保留枚举序号，移除的类型不提供重定向。
	BlueAbacus = 1 UMETA(DisplayName = "蓝釉算盘"),
	WorkshopRefund = 2 UMETA(DisplayName = "工坊返料章"),
	BalloonRack = 3 UMETA(DisplayName = "备用气球挂架"),
	QuickstringPass = 4 UMETA(DisplayName = "快弦工牌"),
	BurstAlarm = 5 UMETA(DisplayName = "破球警钟"),
	EmergencyBallast = 6 UMETA(DisplayName = "应急压舱扣"),
	FirepowerCard UMETA(DisplayName = "旺财的火力牌"),
	CoinPurse UMETA(DisplayName = "管家的零钱袋"),
	StartingFunds UMETA(DisplayName = "充足的启动资金"),
	DoubleDoorPass UMETA(DisplayName = "双门打卡牌"),
	SpareBowstring UMETA(DisplayName = "门后备用弦"),
	ShopDiscount UMETA(DisplayName = "商店折扣"),
	ShopRebate UMETA(DisplayName = "商店返利"),
	ShiftWhistle UMETA(DisplayName = "值班汽笛"),
	StringGlove UMETA(DisplayName = "摘绳手套"),
	FreeRefresh UMETA(DisplayName = "翻货免单章"),
	QuickLadder UMETA(DisplayName = "快速梯子"),
	LoopPurchase UMETA(DisplayName = "环路采购签"),
	LateAdvantage UMETA(DisplayName = "后手优势"),
	SiphonArrow UMETA(DisplayName = "虹吸箭簇"),
	RapidMarch UMETA(DisplayName = "急速行军"),
	BerserkBee UMETA(DisplayName = "狂暴工蜂"),
	ChainReaction UMETA(DisplayName = "连锁反应"),
	LapReassignment UMETA(DisplayName = "一圈调岗令"),
	LastBalloonClasp UMETA(DisplayName = "最后一颗气球扣")
};

/** 可供奖励资产配置的单枚徽章。整数增益必须填整数，触发式徽章使用固定倍率。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBadgeDefinition
{
	GENERATED_BODY()

	/** 未配置和已删除的徽章类型都不能成为持有项。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	ECastleBadgeType Type = ECastleBadgeType::None;

	/** 按徽章类型填写的效果参数；零表示尚未配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	float Magnitude = 0.0f;

	/** 发放时随效果一起复制到持有槽或待替换槽，跨地图仍可展示同一张 PNG。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章|展示")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** 取得指定种类的规则初值，供调试奖励与资产配置使用。 */
	static bool TryMakeStandard(ECastleBadgeType InType, FCastleBadgeDefinition& OutBadge);

	/** 拒绝超出该类型契约的配置，防止效果查询隐式截断或替换参数。 */
	bool Validate(FText& OutFailureReason) const;

	/** 用同一份效果数值生成预览及已持有效果文案。 */
	FText GetDisplayName() const;
	FText GetEffectSummary() const;
};
