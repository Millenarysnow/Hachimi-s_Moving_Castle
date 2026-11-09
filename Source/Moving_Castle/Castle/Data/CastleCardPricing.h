// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CastleGameplayTypes.h"
#include "CastleCardPricing.generated.h"

/** 同一张卡牌在购买和出售时可以使用不同的定价规则。 */
UENUM(BlueprintType)
enum class ECastleCardPriceKind : uint8
{
	Buy UMETA(DisplayName = "购买"),
	Sell UMETA(DisplayName = "出售")
};

/** 商店只依赖此定价接口；替换算法不需要修改商店交易代码或卡牌 UI。 */
UINTERFACE(BlueprintType)
class MOVING_CASTLE_API UCastleCardPricing : public UInterface
{
	GENERATED_BODY()
};

class MOVING_CASTLE_API ICastleCardPricing
{
	GENERATED_BODY()

public:
	/**
	 * 计算非负金币价格。false 必须附带失败原因，商店不会偷偷换用其他算法。
	 * 购买报价在商品生成时保存，支付金额始终和当前商品显示的价格一致。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "移动城堡|商店定价")
	bool TryCalculatePrice(const FCastleBuildingDescriptor& Descriptor, ECastleCardPriceKind Kind, int32& OutPrice, FText& OutFailureReason) const;
};

/**
 * 无需 Data Asset 的测试算法：道路购买价 2；功能建筑购买价 5 + 数字×2 + (等级-1)×10。
 * 出售价为购买价的一半向下取整。后续可用实现同一接口的 C++ 或蓝图类整体替换。
 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API UCastleSimpleCardPricing : public UObject, public ICastleCardPricing
{
	GENERATED_BODY()

public:
	/** 校验描述和具体类一致后再报价，拒绝无效类型、颜色、数字或等级。 */
	virtual bool TryCalculatePrice_Implementation(const FCastleBuildingDescriptor& Descriptor, ECastleCardPriceKind Kind, int32& OutPrice, FText& OutFailureReason) const override;
};
