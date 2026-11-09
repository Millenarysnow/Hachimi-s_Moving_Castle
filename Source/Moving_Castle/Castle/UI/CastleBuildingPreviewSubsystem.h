// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingPreviewSubsystem.generated.h"

class AActor;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class ACastleBuildingModelActor;

/**
 * 同一游戏世界的建筑预览缓存。
 * 背包与商店相同卡牌共用一张渲染纹理；只在首次遇到定义/等级/颜色时捕获一次，
 * 不为每张卡片每帧渲染模型，也不在主视口暴露预览 Actor。
 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingPreviewSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 使用与放置系统相同的模型载体；无模型时捕获允许的占位立方体。 */
	UTextureRenderTarget2D* GetPreview(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason);

	/** 只在有新预览等待模型渲染资源时 Tick；资源就绪后捕获一次并停止等待。 */
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	/** 离开世界时清理全部捕获 Actor、模型和缓存引用。 */
	virtual void Deinitialize() override;

protected:
	/** 只在游戏和 PIE 世界创建，编辑器资产浏览不生成运行时预览。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	/** 弱引用记录异步加载后的首次捕获请求；Actor 生命周期由下面的数组持有。 */
	struct FPendingPreview
	{
		TWeakObjectPtr<ACastleBuildingModelActor> Model;
		TWeakObjectPtr<USceneCaptureComponent2D> Capture;
		double RequestedAt = 0.0;
	};

	/** 等待静态网格完成编译与渲染资源初始化，不以固定延迟猜测资源是否可用。 */
	TArray<FPendingPreview> PendingPreviews;

	/** 纹理缓存强引用，防止 UI 刷新后 GC 回收仍在使用的渲染目标。 */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTextureRenderTarget2D>> Textures;

	/** 捕获对象关闭 Tick 和连续捕获，保留到世界结束以保证渲染命令引用安全。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PreviewActors;
};
