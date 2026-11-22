#pragma once

#include "CastleBackpackComponent.h"
#include "CastleBackpackTestObserver.generated.h"

/** 自动化夹具记录每次动态委托发出时的库存快照，检查界面能观察到的事务边界。 */
UCLASS()
class UCastleBackpackTestObserver : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UCastleBackpackComponent> Backpack;
	TArray<FIntPoint> ObservedCounts;

	UFUNCTION()
	void Observe()
	{
		if (const UCastleBackpackComponent* Source = Backpack.Get())
		{
			ObservedCounts.Emplace(Source->GetItemCount(), Source->GetTempItemCount());
		}
	}
};
