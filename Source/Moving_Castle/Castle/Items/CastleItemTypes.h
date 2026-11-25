#pragma once

#include "CoreMinimal.h"
#include "CastleGameplayTypes.h"
#include "Items/CastleBadgeTypes.h"
#include "CastleItemTypes.generated.h"

/** 本次领奖所用的候选值。数组已展开至非空，单个候选值由界面选中并锁定。 */
struct MOVING_CASTLE_API FCastleWildcardRoomOptions
{
	TArray<TObjectPtr<UCastleBuildingDefinition>> Definitions;
	TArray<ECastleTileColor> Colors;
	TArray<int32> Numbers;

	/** 确认参数必须属于同一份已解析候选值，且仍需通过正式一级描述符工厂。 */
	bool TryMakeDescriptor(UCastleBuildingDefinition* Definition, ECastleTileColor Color, int32 Number,
		FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason) const;
};

/** 只存在于待领取奖励。定好全部属性后才允许创建建筑描述符。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleWildcardRoomSpec
{
	GENERATED_BODY()

	/** 空数组取本次奖励入口显式提供的定义池。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "万能卡")
	TArray<TObjectPtr<UCastleBuildingDefinition>> AllowedDefinitions;

	/** 空数组表示全部正式五色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "万能卡")
	TArray<ECastleTileColor> AllowedColors;

	/** 空数组使用下方闭区间。显式集合可以包含区间外的合法数字。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "万能卡")
	TArray<int32> AllowedNumbers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "万能卡")
	int32 DefaultMinNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "万能卡")
	int32 DefaultMaxNumber = 6;

	/** 在显示界面以前一次校验全部正式候选值，配置非法时整项奖励失败。 */
	bool TryResolveOptions(TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
		FCastleWildcardRoomOptions& OutOptions, FText& OutFailureReason) const;

	/** 由万能卡解析器与直接发牌共用五种正式颜色。 */
	static TConstArrayView<ECastleTileColor> GetLegalColors();
};

/** 房间组模板决定属性的生成时机和发放方式。 */
UENUM(BlueprintType)
enum class ECastleRoomGroupType : uint8
{
	ThreeNumberFourArchers UMETA(DisplayName = "三张数字 4 箭塔"),
	ThreeConsecutiveFarms UMETA(DisplayName = "连号三张同色农田"),
	CustomNormalRoom UMETA(DisplayName = "自定义普通房间"),
	HighNumberRoom UMETA(DisplayName = "大数字房间"),
	CustomColorNormalRoom UMETA(DisplayName = "万能颜色普通房间"),
	RandomFunctionCustomRoom UMETA(DisplayName = "随机功能自定义房间"),
	FullyCustomRoom UMETA(DisplayName = "完全自定义房间"),
	NPCSpeedRoom UMETA(DisplayName = "加速 NPC 房间")
};

/** 决定房间组卡包的陈列材质，发牌内容仍由房间组模板决定。 */
UENUM(BlueprintType)
enum class ECastleRoomPackRarity : uint8
{
	None UMETA(DisplayName = "未配置"),
	White UMETA(DisplayName = "白"),
	Blue UMETA(DisplayName = "蓝"),
	Purple UMETA(DisplayName = "紫"),
	Gold UMETA(DisplayName = "金"),
	Holo UMETA(DisplayName = "炫彩")
};
