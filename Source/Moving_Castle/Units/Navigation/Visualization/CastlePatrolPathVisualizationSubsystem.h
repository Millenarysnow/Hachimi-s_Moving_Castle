#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastlePatrolPathVisualizationSubsystem.generated.h"

class UCastleNavigationSubsystem;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** 当前游戏世界的一条巡逻线。只消费已发布的 Mass 共享路线，不参与寻路、路点编辑或单位移动。 */
UCLASS()
class MOVING_CASTLE_API UCastlePatrolPathVisualizationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 以后由游戏流程调用同一入口；开启后有可走路段才画线，关闭当场移除线条但不清路线。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool SetPathDrawingEnabled(bool bEnabled);

	/** 表示开关状态，不代表当前一定已有包含路段的有效闭环。 */
	UFUNCTION(BlueprintPure, Category="移动城堡|NPC导航")
	bool IsPathDrawingEnabled() const { return bDrawingEnabled; }

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	/** 路线版本变化才重建实例；失效发布也立即清除旧线，不在 Tick 中轮询。 */
	UFUNCTION()
	void HandleSharedPathChanged();

	bool RefreshDrawing();
	bool CreatePathSegments();
	bool RejectDrawing(const FString& Reason) const;
	void DestroyPathSegments();
	void StopDrawing();

	UPROPERTY(Transient)
	TObjectPtr<UCastleNavigationSubsystem> Navigation;

	/** 正式样式只在开关开启时加载并持有，Cook 依赖由 PAL_CastlePatrolLine 显式标记。 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LineMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LineMaterial;

	/** 每条共享路径只拥有一个临时实例组件，而不是每段或每个 NPC 各生成 Actor。 */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PathSegments;

	bool bDrawingEnabled = false;
};
