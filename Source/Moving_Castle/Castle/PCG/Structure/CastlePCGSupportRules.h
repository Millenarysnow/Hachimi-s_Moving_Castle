#pragma once

#include "Castle/PCG/CastleBuildingPCGRule.h"
#include "Castle/PCG/CastlePCGAttachmentKit.h"
#include "CastlePCGSupportRules.generated.h"

/** 每款斜撑独立配置，导入器和规则以同一个枚举识别几何语义。 */
UENUM()
enum class ECastlePCGBraceKind : uint8
{
	Cantilever,
	CornerFan,
	ThroughArch,
	DeepCorbel,
	OpposedSpan
};

USTRUCT()
struct FCastlePCGBraceKits
{
	GENERATED_BODY()

	/** 键是斜撑款式，值是对应家族的作者套件。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TMap<ECastlePCGBraceKind, TObjectPtr<UCastlePCGAttachmentKit>> ByKind;
};

USTRUCT()
struct FCastlePCGStepKits
{
	GENERATED_BODY()

	/** 坡顶和墙裙共用外露格顶面的安装锚点。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TObjectPtr<UCastlePCGAttachmentKit> Slope = nullptr;
	/** 宽边楼板使用上层建筑的格底锚点。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TObjectPtr<UCastlePCGAttachmentKit> Deck = nullptr;
};

namespace CastlePCGSupport
{
	/** 导入和完整性校验共用正式功能塔家族名单。 */
	TConstArrayView<FName> Families();
	TConstArrayView<ECastlePCGBraceKind> BraceKinds();
	FName BraceName(ECastlePCGBraceKind Kind);

	/** 作者网格局部 +Y 朝外，棋盘正交方向提供匹配的纯旋转。 */
	FQuat OutwardRotation(const FIntVector& Direction);
}

/** 为六族功能塔悬挑格选择纯视觉斜撑。 */
UCLASS(EditInlineNew)
class MOVING_CASTLE_API UCastlePCGOverhangBraceRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TMap<FName, FCastlePCGBraceKits> FamilyKits;

	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Structure; }
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override;
	bool ValidateKits(FString& Error) const;
};

/** 为水平二级塔的单侧叠建生成坡顶和在上层格拥有的宽边地板。 */
UCLASS(EditInlineNew)
class MOVING_CASTLE_API UCastlePCGHorizontalLevel2StepRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TMap<FName, FCastlePCGStepKits> FamilyKits;

	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Structure; }
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override;
	bool ValidateKits(FString& Error) const;
};
