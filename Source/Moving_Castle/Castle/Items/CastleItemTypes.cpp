#include "Items/CastleItemTypes.h"

#include "CastleBuildingDefinition.h"
#include "Moving_Castle.h"

#define LOCTEXT_NAMESPACE "CastleItemTypes"

TConstArrayView<ECastleTileColor> FCastleWildcardRoomSpec::GetLegalColors()
{
	static constexpr ECastleTileColor LegalColors[] = {
		ECastleTileColor::Red, ECastleTileColor::Blue, ECastleTileColor::Green,
		ECastleTileColor::Yellow, ECastleTileColor::Purple
	};
	return MakeArrayView(LegalColors);
}

bool FCastleWildcardRoomSpec::TryResolveOptions(
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool, FCastleWildcardRoomOptions& OutOptions,
	FText& OutFailureReason) const
{
	OutOptions = {};
	OutFailureReason = FText::GetEmpty();
	auto Fail = [&OutFailureReason](const FText& Reason)
	{
		OutFailureReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡选项配置无效：%s"), *Reason.ToString());
		return false;
	};
	if (DefaultMinNumber < 1 || DefaultMaxNumber > 9 || DefaultMinNumber > DefaultMaxNumber)
	{
		return Fail(LOCTEXT("WildcardDefaultRange", "默认数字范围必须满足 1 ≤ 最小值 ≤ 最大值 ≤ 9。"));
	}
	if (DefinitionPool.IsEmpty())
	{
		return Fail(LOCTEXT("WildcardEmptyPool", "没有为万能卡传入建筑定义池。"));
	}

	FCastleWildcardRoomOptions Candidate;
	const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> Definitions = AllowedDefinitions.IsEmpty()
		? DefinitionPool : MakeArrayView(AllowedDefinitions);
	for (UCastleBuildingDefinition* Definition : Definitions)
	{
		if (!IsValid(Definition) || !DefinitionPool.Contains(Definition) || Candidate.Definitions.Contains(Definition))
		{
			return Fail(FText::Format(LOCTEXT("WildcardInvalidDefinition", "定义 {0} 无效、不在本次定义池中或重复配置。"),
				FText::FromString(GetPathNameSafe(Definition))));
		}
		FText Reason;
		if (!Definition->ValidateConfiguration(Reason))
		{
			return Fail(FText::Format(LOCTEXT("WildcardInvalidDefinitionConfig", "定义 {0} 校验失败：{1}"),
				FText::FromString(Definition->GetPathName()), Reason));
		}
		Candidate.Definitions.Add(Definition);
	}

	const TConstArrayView<ECastleTileColor> LegalColors = GetLegalColors();
	const TConstArrayView<ECastleTileColor> Colors = AllowedColors.IsEmpty() ? LegalColors : MakeArrayView(AllowedColors);
	for (const ECastleTileColor Color : Colors)
	{
		if (!LegalColors.Contains(Color) || Candidate.Colors.Contains(Color))
		{
			return Fail(FText::Format(LOCTEXT("WildcardInvalidColor", "色值 {0} 非正式颜色或重复配置。"),
				FText::AsNumber(static_cast<int32>(Color))));
		}
		Candidate.Colors.Add(Color);
	}

	if (AllowedNumbers.IsEmpty())
	{
		for (int32 Number = DefaultMinNumber; Number <= DefaultMaxNumber; ++Number)
		{
			Candidate.Numbers.Add(Number);
		}
	}
	else
	{
		for (const int32 Number : AllowedNumbers)
		{
			if (Number < 1 || Number > 9 || Candidate.Numbers.Contains(Number))
			{
				return Fail(FText::Format(LOCTEXT("WildcardInvalidNumber", "数字 {0} 超出 1 至 9 的范围或重复配置。"),
					FText::AsNumber(Number)));
			}
			Candidate.Numbers.Add(Number);
		}
	}

	OutOptions = MoveTemp(Candidate);
	return true;
}

bool FCastleWildcardRoomOptions::TryMakeDescriptor(UCastleBuildingDefinition* Definition, const ECastleTileColor Color,
	const int32 Number, FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason) const
{
	OutDescriptor = {};
	if (!IsValid(Definition) || !Definitions.Contains(Definition) || !Colors.Contains(Color) || !Numbers.Contains(Number))
	{
		OutFailureReason = LOCTEXT("WildcardInvalidSelection", "请先选定本张万能卡允许的建筑、颜色和数字。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("万能卡确认失败：定义=%s，色值=%d，数字=%d。%s"),
			*GetPathNameSafe(Definition), static_cast<int32>(Color), Number, *OutFailureReason.ToString());
		return false;
	}
	if (!Definition->CreateDescriptor(1, Color, Number, OutDescriptor, OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡无法生成建筑定义 %s 的一级描述符：%s"),
			*Definition->GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
