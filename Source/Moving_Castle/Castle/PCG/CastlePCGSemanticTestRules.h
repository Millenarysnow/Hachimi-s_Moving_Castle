#pragma once

#include "CastleBuildingPCGRule.h"
#include "CastlePCGAttachmentKit.h"
#include "CastlePCGSemantics.h"
#include "CastlePCGSemanticTestRules.generated.h"

/** 回归测试显式覆写门洞语义，用于证明消费者不会再次计算通行关系。 */
UCLASS(Transient, NotBlueprintable)
class UCastlePCGTestClosedPlanarRule : public UCastlePCGPlanarRule
{
	GENERATED_BODY()
public:
	bool bCloseDoors = true;

	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override
	{
		if (!Super::Evaluate(Query, Field, Error)) { return false; }
		if (bCloseDoors)
		{
			for (auto& Entry : Field.SideFaces)
			{
				if (Entry.Value.IsDoor()) { Entry.Value.State = ECastlePCGSide::Closed; }
			}
		}
		return true;
	}
};

/** 测试斜撑依赖侧下方支撑，额外支持把挂件重复写入用于错误分支验收。 */
UCLASS(Transient, NotBlueprintable)
class UCastlePCGTestBraceRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	FIntVector Owner = FIntVector::ZeroValue;
	bool bDuplicate = false;
	UPROPERTY(Transient)
	TObjectPtr<UCastlePCGAttachmentKit> Kit = nullptr;

	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Structure; }

	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override
	{
		const FIntVector SideSupport(-1, 0, -1);
		if (!Query.Find(Owner) || !Query.IsOverhanging(Owner) || !Query.FindOffset(Owner, SideSupport)) { return true; }
		const FCastlePCGAttachmentKey Key{Owner, TEXT("UnderEdge"), TEXT("RegressionBrace")};
		FCastlePCGAttachmentSpec Spec;
		Spec.Kit = Kit;
		Spec.AnchorTransform = FTransform(FRotator(0, 0, 25), FVector(0, 0, -60));
		return Field.AddAttachment(Key, Spec, Error) && (!bDuplicate || Field.AddAttachment(Key, Spec, Error));
	}
};

/** 替换屋顶只在指定悬挑格露天时生效；重新获得支撑后复原作者屋顶。 */
UCLASS(Transient, NotBlueprintable)
class UCastlePCGTestRoofRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	FIntVector Owner = FIntVector::ZeroValue;
	UPROPERTY(Transient)
	TObjectPtr<UCastlePCGAttachmentKit> Kit = nullptr;

	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Structure; }

	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override
	{
		FCastlePCGCellSpec* Cell = Field.Cells.Find(Owner);
		if (Cell && Query.IsSkyExposed(Owner) && Query.IsOverhanging(Owner))
		{
			Cell->Roof = ECastlePCGRoof::Override;
			Cell->RoofOverride.Kit = Kit;
			Cell->RoofOverride.AnchorTransform = FTransform(FVector(0, 0, 570));
			Cell->RoofOverride.bSelectable = true;
		}
		return true;
	}
};
