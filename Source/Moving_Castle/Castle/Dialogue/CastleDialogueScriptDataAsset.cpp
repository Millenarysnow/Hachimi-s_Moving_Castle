#include "CastleDialogueScriptDataAsset.h"

#include "Moving_Castle.h"

bool UCastleDialogueScriptDataAsset::ValidateScript(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (Lines.IsEmpty())
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("剧本 %s 没有任何台词行。"), *GetPathName()));
	}
	else if (!FMath::IsFinite(CharactersPerSecond) || CharactersPerSecond <= 0.0f)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("剧本 %s 的 CharactersPerSecond 必须是有限正数，当前值为 %.3f。"),
			*GetPathName(), CharactersPerSecond));
	}
	if (!OutFailureReason.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	}

	TSet<FName> LineIds;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FCastleDialogueLine& Line = Lines[Index];
		if (Line.LineId.IsNone() || LineIds.Contains(Line.LineId))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("剧本 %s 第 %d 行的 LineId 为空或重复：%s。"),
				*GetPathName(), Index, *Line.LineId.ToString()));
			break;
		}
		LineIds.Add(Line.LineId);
		// 左右贴图均为可选配置，发言身份与正文仍须明确。
		if (Line.TextTemplate.IsEmpty() || Line.SpeakerName.IsEmpty())
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("剧本 %s 行 %s 缺少正文或姓名。"),
				*GetPathName(), *Line.LineId.ToString()));
			break;
		}
		if (Line.bEndsScript && (Index != Lines.Num() - 1 || !Line.Choices.IsEmpty()))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("剧本 %s 的结束行 %s 必须是末行且不能包含分支选项。"),
				*GetPathName(), *Line.LineId.ToString()));
			break;
		}
	}
	if (OutFailureReason.IsEmpty() && !Lines.Last().bEndsScript)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("剧本 %s 的末行 %s 未标记为结束行。"),
			*GetPathName(), *Lines.Last().LineId.ToString()));
	}
	if (OutFailureReason.IsEmpty())
	{
		// 从末行反推。只有全部出口都已证实能结束时才标记上一行，避免有出口的循环通过校验。
		TMap<FName, int32> Indices;
		TArray<TArray<int32>> Predecessors;
		TArray<int32> UnverifiedExits;
		Predecessors.SetNum(Lines.Num());
		UnverifiedExits.SetNumZeroed(Lines.Num());
		for (int32 Index = 0; Index < Lines.Num(); ++Index) { Indices.Add(Lines[Index].LineId, Index); }
		for (int32 Index = 0; Index < Lines.Num() - 1; ++Index)
		{
			const auto& Line = Lines[Index];
			if (Line.Choices.IsEmpty())
			{
				Predecessors[Index + 1].Add(Index);
				UnverifiedExits[Index] = 1;
				continue;
			}
			for (const FCastleDialogueChoice& Choice : Line.Choices)
			{
				if (Choice.Text.IsEmpty() || Choice.TargetLineId.IsNone() || !LineIds.Contains(Choice.TargetLineId))
				{
					OutFailureReason = FText::FromString(FString::Printf(
						TEXT("剧本 %s 行 %s 的选项文案为空，或目标行 %s 不存在。"),
						*GetPathName(), *Line.LineId.ToString(), *Choice.TargetLineId.ToString()));
					break;
				}
				Predecessors[Indices.FindChecked(Choice.TargetLineId)].Add(Index);
				++UnverifiedExits[Index];
			}
			if (!OutFailureReason.IsEmpty()) { break; }
		}
		if (OutFailureReason.IsEmpty())
		{
			TArray<int32> Verified{Lines.Num() - 1};
			for (int32 Index = 0; Index < Verified.Num(); ++Index)
			{
				for (const int32 Previous : Predecessors[Verified[Index]])
				{
					if (--UnverifiedExits[Previous] == 0) { Verified.Add(Previous); }
				}
			}
			const int32 Invalid = UnverifiedExits.IndexOfByPredicate([](int32 Exits) { return Exits > 0; });
			if (Invalid != INDEX_NONE)
			{
				OutFailureReason = FText::Format(INVTEXT("剧本 {0} 行 {1} 存在无法保证结束的循环分支。"),
					FText::FromString(GetPathName()), FText::FromName(Lines[Invalid].LineId));
			}
		}
	}
	if (!OutFailureReason.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	}
	return true;
}

int32 UCastleDialogueScriptDataAsset::FindLineIndex(const FName LineId) const
{
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (Lines[Index].LineId == LineId) { return Index; }
	}
	return INDEX_NONE;
}
