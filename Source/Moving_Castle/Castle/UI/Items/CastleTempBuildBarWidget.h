#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleTempBuildBarWidget.generated.h"

class UCastleBackpackComponent;
class UTextBlock;

/** 叠牌控件读取临时栏张数与队首，只通过库存事件刷新。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleTempBuildBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "移动城堡|道具")
	bool BindBackpack(UCastleBackpackComponent* InBackpack);

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StackCount;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> FrontCard;

private:
	TWeakObjectPtr<UCastleBackpackComponent> Backpack;

	UFUNCTION()
	void RefreshCards();
};
