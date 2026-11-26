#pragma once

#include "CoreMinimal.h"
#include "CastleGameplayTypes.h"
#include "CastleItemTypes.generated.h"

class UCastlePendingRoomReward;
class UCastleRoomGroupDataAsset;

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

	/** 空数组表示红、黄、蓝、绿全部玩法色。 */
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
};

/** 一条发牌规则的建筑来源；规则按资产列表顺序准备。 */
UENUM(BlueprintType)
enum class ECastleRoomRuleBuilding : uint8
{
	None UMETA(DisplayName = "未配置"),
	Fixed UMETA(DisplayName = "指定建筑"),
	RandomStandard UMETA(DisplayName = "随机标准房间"),
	RandomFunction UMETA(DisplayName = "随机功能塔"),
	EachFunction UMETA(DisplayName = "每类功能塔"),
	MainBarFirst UMETA(DisplayName = "主栏首卡定义"),
	MostPlacedFunction UMETA(DisplayName = "在场最多的功能塔")
};

/** 一条发牌规则的颜色来源；上一张是本批已准备的最近一张卡。 */
UENUM(BlueprintType)
enum class ECastleRoomRuleColor : uint8
{
	None UMETA(DisplayName = "未配置"),
	Fixed UMETA(DisplayName = "指定颜色"),
	RandomPlayable UMETA(DisplayName = "随机玩法色"),
	MainBarFirst UMETA(DisplayName = "主栏首卡同色"),
	PreviousCard UMETA(DisplayName = "上一张同色"),
	DifferentFromPreviousCard UMETA(DisplayName = "与上一张异色")
};

/** 一条发牌规则的数字来源；显式数字集合也可表达区间或固定序列。 */
UENUM(BlueprintType)
enum class ECastleRoomRuleNumber : uint8
{
	None UMETA(DisplayName = "未配置"),
	Fixed UMETA(DisplayName = "指定数字"),
	RandomChoice UMETA(DisplayName = "从数字集合抽取"),
	Sequence UMETA(DisplayName = "按数字集合顺序发放"),
	MainBarFirst UMETA(DisplayName = "主栏首卡同号"),
	PreviousCard UMETA(DisplayName = "上一张同号")
};

/** 组合建筑、颜色、数字和特质，发出一张或一批一级建筑卡。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleRoomCardRule
{
	GENERATED_BODY()

	/** 每种选定建筑发放的张数；主栏首卡张数模式以该卡的牌面数字计算。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	bool bCountFromMainBarFirstNumber = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	ECastleRoomRuleBuilding BuildingSource = ECastleRoomRuleBuilding::None;

	/** 仅在指定建筑时使用，必须能在本次定义池内定位唯一资产。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	ECastleBuildingType FixedBuildingType = ECastleBuildingType::NormalRoom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	ECastleRoomRuleColor ColorSource = ECastleRoomRuleColor::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	ECastleTileColor FixedColor = ECastleTileColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	ECastleRoomRuleNumber NumberSource = ECastleRoomRuleNumber::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	int32 FixedNumber = 0;

	/** 随机抽取时视为候选集合，顺序发放时保留资产中的数字顺序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	TArray<int32> NumberChoices;

	/** 各个维度可独立共享本条规则的首次抽样，例如同色的顺序 1、2、3。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	bool bShareBuildingDraw = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	bool bShareColorDraw = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则")
	bool bShareNumberDraw = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则|特质")
	ECastleRoomTrait RoomTrait = ECastleRoomTrait::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "发牌规则|特质")
	int32 TraitRemainingRounds = 0;
};

/** 房间组对主建筑栏的动作。临时栏仅参与发牌溢出和既有 FIFO 补位。 */
UENUM(BlueprintType)
enum class ECastleRoomGroupMainBarAction : uint8
{
	None UMETA(DisplayName = "无"),
	ConsumeFirst UMETA(DisplayName = "消耗主栏首卡"),
	RecolorMatching UMETA(DisplayName = "主栏指定颜色转换"),
	RecolorAllRandom UMETA(DisplayName = "主栏统一随机色")
};

/** 准备时记录主栏原样和转换后的牌序，提交时据此检查准备结果是否仍可用。 */
struct MOVING_CASTLE_API FCastlePreparedMainBarChange
{
	TArray<FCastleBuildingDescriptor> Before;
	TArray<FCastleBuildingDescriptor> After;
};

/** 单次交易的准备结果。跨帧保留待领取句柄时，调用方沿用现有的 UPROPERTY 持有方式。 */
struct MOVING_CASTLE_API FCastlePreparedRoomGroup
{
	/** 保留来源资产以便在提交失败时记录路径。 */
	const UCastleRoomGroupDataAsset* Group = nullptr;

	/** 准备成功后由背包按现有溢出规则整批入库。 */
	TArray<FCastleBuildingDescriptor> Cards;

	/** 自定义奖励在实际确认前只保留句柄。 */
	UCastlePendingRoomReward* PendingReward = nullptr;

	/** 无主栏动作时没有快照；有动作时需与发牌同一事务提交。 */
	TOptional<FCastlePreparedMainBarChange> MainBarChange;
};
