#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleGuideDataAsset.generated.h"

class UTexture2D;

/** 一本百科中的图文条目；逻辑以稳定 ID 引用，贴图和说明由数据资产维护。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleGuideEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "百科")
	FName EntryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "百科")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "百科", meta = (MultiLine = "true"))
	FText Description;

	/** 正式插图交付前可以留空，由界面折叠插图控件。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "百科")
	TObjectPtr<UTexture2D> Illustration = nullptr;

	/** 基础条目从开局即可在百科查看，其余条目经教程说明解锁。 */
	UPROPERTY(EditAnywhere, Category = "百科") bool bAvailableFromStart = false;
};

/** 图文百科校验条目身份与文案，示意图可等正式美术到位后配置。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleGuideDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "百科")
	TArray<FCastleGuideEntry> Entries;

	bool ValidateEntries(FText& OutFailureReason) const;
	const FCastleGuideEntry* FindEntry(FName EntryId) const;
};
