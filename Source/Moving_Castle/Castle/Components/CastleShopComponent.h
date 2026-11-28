// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleGameplayTypes.h"
#include "CastleShopComponent.generated.h"

class ACastleBoard;
class UCastleBackpackComponent;
class UCastleBadgeRuntimeSubsystem;
class UCastleEconomyComponent;

/** 商品保存完整卡牌和生成时的报价，售罄后仍保留卡面，直到主动刷新。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleShopOffer
{
	GENERATED_BODY()

	/** 购买后原样送入背包的建筑描述。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "商店")
	FCastleBuildingDescriptor Descriptor;

	/** 内部货架保存定价策略的原价；公开的货架副本包含本次实际买牌价。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "商店")
	int32 Price = 0;

	/** 每个槽位每轮只能购买一次，不会购买后偷偷补牌。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "商店")
	bool bSold = false;
};

/** 开关商店、刷新、购买或出售成功后通知表现层重新同步。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCastleShopChanged);

/**
 * 单局房间商店，只负责生成商品和组织交易，不负责 UI 或输入。
 * 自动出牌指首次打开和主动刷新时生成六张牌；关闭再打开保留商品及售罄状态。
 * 所有依赖均显式注入，配置无效时返回错误，不生成替代商品或更换定价算法。
 */
UCLASS(ClassGroup = (Castle), meta = (BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleShopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 配置定价策略；商品池直接读取绑定棋盘的 BuildingDefinitions。 */
	UCastleShopComponent();

	/** 可在控制器蓝图的组件默认值中替换为任何实现 CastleCardPricing 的类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "商店|定价", meta = (MustImplement = "/Script/Moving_Castle.CastleCardPricing"))
	TSubclassOf<UObject> PricingStrategyClass;

	/** 随机流在初始化时设置一次，刷新推进随机流，重新开关商店不会推进。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "商店|出牌")
	int32 RandomSeed = 20261108;

	/** 手动刷新费用，设为 0 表示免费刷新；负数配置直接拒绝。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "商店|经济", meta = (ClampMin = "0"))
	int32 RefreshCost = 2;

	/** 场上占用格数加背包卡牌对应格数的总上限，后续由主塔等级系统调整。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "商店|容量", meta = (ClampMin = "1"))
	int32 RoomLimit = 30;

	/** 绑定玩家账户与棋盘背包；只允许初始化一次，防止切换账户后继承旧商品。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	bool InitializeShop(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, FText& OutFailureReason);

	/** 首次打开免费生成六张牌；后续打开保留原商品，可供未来波次系统直接调用。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	bool TryOpenShop(FText& OutFailureReason);

	/** 关闭只改变可见状态，不改金币、商品和背包。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	void CloseShop();

	/** 先生成并校验整批新商品，再扣费和替换，任何失败都保留旧商品及随机流。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	bool TryRefreshOffers(FText& OutFailureReason);

	/** 奖励专用免费整批换牌；失败时商品和正式随机流均保持不变。 */
	bool TryRefreshOffersForReward(FText& OutFailureReason);

	/** 闭店时配置教学货架及下一次刷新。空货架明确表示缺货，仍允许付费刷新。 */
	bool SetScriptedOffers(TConstArrayView<FCastleBuildingDescriptor> Current,
		TConstArrayView<FCastleBuildingDescriptor> OnRefresh, FText& OutReason);
	/** 固定货架允许零至六张商品，普通货架仍完整填满。 */
	bool UsesScriptedOffers() const { return bScriptedStock; }

	/** 奖励事务：在临时状态中初始化（如需）、免费换批并打开，全部成功后才提交。 */
	bool TryOpenForReward(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, FText& OutFailureReason);

	/** 导演在提交过关奖励前预生成免费商品，候选组件独立保存随机流和新商品。 */
	bool PrepareRewardShop(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, int32 CandidateMaxNumber,
		UCastleShopComponent& Candidate, FText& OutFailureReason);
	/** 只发布已成功准备的候选，供包含金币、回血与商店的联合奖励提交。 */
	void CommitRewardShop(UCastleShopComponent& Candidate);

	/** 修改房间总上限；非正数配置拒绝并保留原值。 */
	bool TrySetRoomLimit(int32 NewRoomLimit, FText& OutFailureReason);

	/** 设置下一批商品的数字闭区间；已生成的商品及售罄状态继续有效。 */
	bool TrySetOfferNumberRange(int32 MinNumber, int32 MaxNumber, FText& OutReason);
	int32 GetMaxOfferNumber() const { return MaxOfferNumber; }

	/** 购买前校验售罄、余额、背包和全局容量，失败不扣费也不移走商品。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	bool TryBuyOffer(int32 OfferIndex, FText& OutFailureReason);

	/** 商店打开时允许卖出背包指定槽位，收益同样通过独立定价接口计算。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	bool TrySellBackpackItem(int32 BackpackIndex, FText& OutFailureReason);

	/** 读取出售报价，UI 只展示报价，不自行复写算法。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	bool TryGetSellPrice(int32 BackpackIndex, int32& OutPrice, FText& OutFailureReason) const;

	/** 返回只读状态和商品副本，外部无法直接改售罄或标价。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	bool IsShopOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	bool IsInitialized() const { return bInitialized; }

	/** 摄影只读取本商店绑定的棋盘尺寸，不在世界中猜测应拍哪个棋盘。 */
	const ACastleBoard* GetBoundBoard() const { return Board.Get(); }

	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	TArray<FCastleShopOffer> GetOffers() const;

	/** 返回待出售背包卡牌的副本，面板可展示类型、颜色和数字后让玩家选择。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	TArray<FCastleBuildingDescriptor> GetBackpackItems() const;

	/** 合成房间按实际格数计数，避免合成后凭空释放购买额度。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	int32 GetOwnedRoomCount() const;

	/** 商店与导演共用占格统计，失败原因交给业务入口报告。 */
	static bool TryCountOwnedRoomCells(const ACastleBoard* InBoard, int32& OutCells, FText& OutReason);

	/** 表现层订阅此事件，避免商品操作依赖某一种 Widget。 */
	UPROPERTY(BlueprintAssignable, Category = "移动城堡|商店")
	FCastleShopChanged OnShopChanged;

private:
	friend class UCastleDirectorSubsystem;
	/** 导演通过整张成长表校验并成功扩层后，一次写入已验证的新商店上限。 */
	void CommitValidatedTowerGrowth(int32 NewRoomLimit, int32 NewMaxNumber);

	/** 共用交易前置校验，防止余额事件重入交易或绕过商店开关。 */
	bool ValidateTrade(FText& OutFailureReason) const;
	/** 游戏世界的普通交易需要本局徽章状态；离线商店输入无世界状态可记账。 */
	bool TryResolveTradeBadgeRuntime(UCastleBadgeRuntimeSubsystem*& OutRuntime, FText& OutFailureReason) const;

	/** 在临时随机流中完整生成六张商品，调用失败不影响已展示的数据。 */
	bool GenerateOffers(FRandomStream& Stream, TArray<FCastleShopOffer>& OutOffers, FText& OutFailureReason,
		TOptional<int32> FirstNumber = {}) const;
	/** 每张标准房独立抽建筑和四色；明确传入数字区间用于区分普通商品与返利卡。 */
	bool GenerateRandomRoom(FRandomStream& Stream, int32 MinNumber, int32 MaxNumber, TOptional<int32> ForcedNumber,
		FCastleBuildingDescriptor& OutCard, FText& OutFailureReason) const;
	int32 GetBuyPrice(const FCastleShopOffer& Offer) const;

	/** 付费刷新和奖励刷新共用候选生成及提交事务。 */
	bool RefreshOffersInternal(bool bRewardRefresh, FText& OutFailureReason);

	/** 定价接口唯一调用边界，额外拒绝策略返回的负价。 */
	bool CalculatePrice(const FCastleBuildingDescriptor& Descriptor, bool bSelling, int32& OutPrice, FText& OutFailureReason) const;

	/** 下列对象由组件引用保活，生命周期随所属玩家控制器结束。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleEconomyComponent> Economy;

	UPROPERTY(Transient)
	TObjectPtr<ACastleBoard> Board;

	UPROPERTY(Transient)
	TObjectPtr<UCastleBackpackComponent> Backpack;

	UPROPERTY(Transient)
	TObjectPtr<UObject> PricingStrategy;

	UPROPERTY(Transient)
	TArray<FCastleShopOffer> Offers;
	/** 教学货架与随机货架互斥，重复打开时保留售罄状态。 */
	UPROPERTY(Transient) TArray<FCastleShopOffer> ScriptedRefreshOffers;
	bool bScriptedStock = false;
	bool bScriptedRefreshPending = false;

	/** 临时生成成功后才推进此流，失败重试不会消耗随机结果。 */
	FRandomStream RandomStream;
	/** 未由导演接管的商店保留原型商品范围；正式单局启动时显式设置等级范围。 */
	int32 MinOfferNumber = 1;
	int32 MaxOfferNumber = 9;
	bool bInitialized = false;
	bool bOpen = false;
	bool bTrading = false;
};
