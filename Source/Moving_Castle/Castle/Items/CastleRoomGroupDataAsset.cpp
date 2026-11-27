#include "Items/CastleRoomGroupDataAsset.h"

#include "Moving_Castle.h"

bool UCastleRoomGroupDataAsset::ValidateRarity(FText& OutReason) const
{
	// 五档数值连续，未配置哨兵与反序列化后的越界值都不能进入发放或商店。
	OutReason = FText::GetEmpty();
	if (Rarity >= ECastleItemRarity::Common && Rarity <= ECastleItemRarity::Legendary) { return true; }
	OutReason = FText::Format(INVTEXT("房间组 {0} 的卡包稀有度无效（{1}），必须配置为普通、优秀、稀有、史诗或传说。"),
		FText::FromString(GetPathName()), FText::AsNumber(static_cast<uint8>(Rarity)));
	UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
	return false;
}

bool UCastleRoomGroupDataAsset::ValidateGrantConfiguration(FText& OutReason) const
{
	if (!ValidateRarity(OutReason)) { return false; }
	const auto Reject = [this, &OutReason](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("房间组 %s：%s"), *GetPathName(), *Reason.ToString());
		return false;
	};
	if (DisplayName.IsEmpty() || ShopPrice < 0)
	{
		return Reject(INVTEXT("房间组必须配置名称与非负售价。"));
	}
	if (bChooseInPicker)
	{
		// 弹窗只确认一张卡，不能携带会在弹窗打开前执行的发牌或改栏动作。
		return (CardRules.IsEmpty() && EmptyMainBarCardRules.IsEmpty() && MainBarAction == ECastleRoomGroupMainBarAction::None)
			|| Reject(INVTEXT("自定义房间组不能同时配置直接发牌或主栏动作。"));
	}
	if (bLockRandomFunctionOnFirstOpen || (!EmptyMainBarCardRules.IsEmpty() && CardRules.IsEmpty()))
	{
		return Reject(INVTEXT("首次打开抽功能需要自定义弹窗，空主栏发牌需要同时配置常规发牌规则。"));
	}
	switch (MainBarAction)
	{
	case ECastleRoomGroupMainBarAction::None:
	case ECastleRoomGroupMainBarAction::ConsumeFirst:
		return !CardRules.IsEmpty() || Reject(INVTEXT("房间组没有配置发牌规则。"));
	case ECastleRoomGroupMainBarAction::RecolorMatching:
		return (CastleTileColors::IsPlayable(RecolorFrom) && CastleTileColors::IsPlayable(RecolorTo) && RecolorFrom != RecolorTo)
			|| Reject(INVTEXT("指定颜色转换需要两个不同的玩法色。"));
	case ECastleRoomGroupMainBarAction::RecolorAllRandom:
		return true;
	default:
		return Reject(INVTEXT("房间组主栏动作无效。"));
	}
}
