#include "BuildingPartMatcher.h"

namespace BuildingPartMatcher
{
	/** 世界空间距离为主，少量朝向代价避免把最近的反向零件误当成最自然的延续。 */
	double Cost(const FBuildingPartMatchPoint& Source, const FBuildingPartMatchPoint& Target)
	{
		const double TurnDistance = Source.Rotation.AngularDistance(Target.Rotation) * FMath::Min(Source.Radius, Target.Radius);
		return FVector::DistSquared(Source.Center, Target.Center) + TurnDistance * TurnDistance * 0.1;
	}

	TArray<int32> Match(const TArray<FBuildingPartMatchPoint>& Sources, const TArray<FBuildingPartMatchPoint>& Targets)
	{
		TArray<int32> Result;
		Result.Init(INDEX_NONE, Targets.Num());
		TMap<FString, TArray<int32>> SourceGroups;
		TMap<FString, TArray<int32>> TargetGroups;
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			SourceGroups.FindOrAdd(Sources[Index].Identity).Add(Index);
		}
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
		{
			TargetGroups.FindOrAdd(Targets[Index].Identity).Add(Index);
		}
		for (const auto& Group : TargetGroups)
		{
			const TArray<int32>* SourceIndices = SourceGroups.Find(Group.Key);
			if (!SourceIndices)
			{
				continue;
			}
			const TArray<int32>& TargetIndices = Group.Value;
			// 匈牙利算法要求行数不大于列数；转置只改变求解方向，不改变一对一关系。
			const bool bRowsAreSources = SourceIndices->Num() <= TargetIndices.Num();
			const int32 Rows = FMath::Min(SourceIndices->Num(), TargetIndices.Num());
			const int32 Columns = FMath::Max(SourceIndices->Num(), TargetIndices.Num());
			TArray<double> RowPotential;
			TArray<double> ColumnPotential;
			TArray<int32> AssignedRow;
			TArray<int32> PreviousColumn;
			RowPotential.Init(0.0, Rows + 1);
			ColumnPotential.Init(0.0, Columns + 1);
			AssignedRow.Init(0, Columns + 1);
			PreviousColumn.Init(0, Columns + 1);
			for (int32 Row = 1; Row <= Rows; ++Row)
			{
				AssignedRow[0] = Row;
				int32 Column = 0;
				TArray<double> Best;
				TArray<bool> Used;
				Best.Init(TNumericLimits<double>::Max(), Columns + 1);
				Used.Init(false, Columns + 1);
				do
				{
					Used[Column] = true;
					const int32 ActiveRow = AssignedRow[Column];
					double Step = TNumericLimits<double>::Max();
					int32 NextColumn = 0;
					for (int32 Candidate = 1; Candidate <= Columns; ++Candidate)
					{
						if (Used[Candidate]) { continue; }
						const int32 SourceIndex = (*SourceIndices)[bRowsAreSources ? ActiveRow - 1 : Candidate - 1];
						const int32 TargetIndex = TargetIndices[bRowsAreSources ? Candidate - 1 : ActiveRow - 1];
						const double Reduced = Cost(Sources[SourceIndex], Targets[TargetIndex]) - RowPotential[ActiveRow] - ColumnPotential[Candidate];
						if (Reduced < Best[Candidate])
						{
							Best[Candidate] = Reduced;
							PreviousColumn[Candidate] = Column;
						}
						if (Best[Candidate] < Step)
						{
							Step = Best[Candidate];
							NextColumn = Candidate;
						}
					}
					for (int32 Index = 0; Index <= Columns; ++Index)
					{
						if (Used[Index])
						{
							RowPotential[AssignedRow[Index]] += Step;
							ColumnPotential[Index] -= Step;
						}
						else
						{
							Best[Index] -= Step;
						}
					}
					Column = NextColumn;
				} while (AssignedRow[Column] != 0);
				// 沿增广路径重新分配，避免“逐件找最近”先占用了其他零件唯一合适的位置。
				do
				{
					const int32 Previous = PreviousColumn[Column];
					AssignedRow[Column] = AssignedRow[Previous];
					Column = Previous;
				} while (Column != 0);
			}
			for (int32 Column = 1; Column <= Columns; ++Column)
			{
				if (AssignedRow[Column] == 0) { continue; }
				const int32 SourceIndex = (*SourceIndices)[bRowsAreSources ? AssignedRow[Column] - 1 : Column - 1];
				const int32 TargetIndex = TargetIndices[bRowsAreSources ? Column - 1 : AssignedRow[Column] - 1];
				Result[TargetIndex] = SourceIndex;
			}
		}
		return Result;
	}
}
