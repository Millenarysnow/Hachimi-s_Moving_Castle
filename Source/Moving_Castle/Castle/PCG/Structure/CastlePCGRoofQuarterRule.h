#pragma once

#include "Castle/PCG/CastleBuildingPCGRule.h"
#include "CastlePCGRoofQuarterRule.generated.h"

class UCastleHachimisV4Layout;

UENUM()
enum class ECastlePCGRoofQuarterState : uint8
{
	Original = 0,
	FlushX = 1,
	FlushY = 2,
	FlushXY = 3
};

/** 该片的所有状态公用同一格底锚点，格与有符号轴均以未旋转的 UE 作者坐标声明。 */
USTRUCT()
struct FCastlePCGRoofQuarterPiece
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	FName Slot;

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	FIntVector Owner = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	FIntPoint Outward = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	FVector PivotInOwner = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	TMap<ECastlePCGRoofQuarterState, TObjectPtr<UCastlePCGAttachmentKit>> Variants;
};

/** 显式绑定作者布局与其已烘焙的常规屋面变体。 */
USTRUCT()
struct FCastlePCGRoofQuarterForm
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	TObjectPtr<UCastleHachimisV4Layout> Layout = nullptr;

	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	TArray<FCastlePCGRoofQuarterPiece> Pieces;
};

UCLASS(EditInlineNew)
class MOVING_CASTLE_API UCastlePCGRoofQuarterRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category="建筑PCG")
	TMap<FName, FCastlePCGRoofQuarterForm> Forms;

	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Structure; }
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override;
	bool ValidateKits(FString& Error) const;
};

namespace CastlePCGRoofQuarters
{
	/** 导入与运行时校验共用正式造型集合及语义键。 */
	TArray<FName> FormKeys();
	FName FormKey(const UCastleHachimisV4Layout& Layout);
	bool RequiresFarmSplit(FName Key);
	const TCHAR* StateName(ECastlePCGRoofQuarterState State);
}
