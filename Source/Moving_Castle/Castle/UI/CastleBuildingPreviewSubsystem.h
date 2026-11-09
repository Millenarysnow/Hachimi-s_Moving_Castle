// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingPreviewSubsystem.generated.h"

class AActor;
class ACastleBuildingBase;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * 同一游戏世界的动态建筑预览。
 * 相同 Data Asset 和等级共用一个旋转模型与法线捕获纹理，卡片自行应用颜色描边。
 * 只更新当前可见卡片使用的预览，避免打开过商店后持续渲染已经离开屏幕的模型。
 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingPreviewSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 显式加载专用 UI 描边材质；缺失时报告错误，不退回普通实心缩略图。 */
	UCastleBuildingPreviewSubsystem();

	/** 从卡牌的共享定义和等级创建实际模型；模型未配置时仍使用定义允许的占位立方体。 */
	UTextureRenderTarget2D* GetPreview(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason);

	/** 可见卡片每帧登记使用，背包与商店中的同一模型不会重复捕获。 */
	void MarkPreviewUsed(const FCastleBuildingDescriptor& Descriptor);

	/** 取得法线转彩色描边的 UI 材质，各张卡片创建独立的颜色实例。 */
	UMaterialInterface* GetOutlineMaterial() const { return OutlineMaterial; }

	/** 以 20 帧/秒更新活跃预览，相机绕模型中心每秒旋转 14 度。 */
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	/** 世界结束时清理模型、捕获组件与纹理，停止 PIE 不留下预览对象。 */
	virtual void Deinitialize() override;

protected:
	/** 仅支持游戏与 PIE，不在编辑器世界创建运行时模型。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	/** 每个共享模型的旋转状态、资源准备状态和最近一次可见使用时间。 */
	struct FPreviewEntry
	{
		TWeakObjectPtr<ACastleBuildingBase> Model;
		TWeakObjectPtr<USceneCaptureComponent2D> Capture;
		FVector Center = FVector::ZeroVector;
		float Distance = 0.0f;
		float RotationDegrees = -32.0f;
		double LastUsedAt = 0.0;
		float ResourceWaitSeconds = 0.0f;
		bool bReady = false;
		bool bFailed = false;
	};

	/** 几何预览只按资产和等级去重，卡牌颜色在 UI 材质中应用。 */
	TMap<FString, FPreviewEntry> Entries;

	/** 纹理与 Actor 保持 UObject 强引用；旋转状态中的弱引用不承担保活职责。 */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTextureRenderTarget2D>> Textures;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PreviewActors;

	/** 此材质将法线变化转为带柔光的轮廓，模型面内部保持透明。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OutlineMaterial;

	/** 限制捕获频率，但旋转角度按累计真实时间推进，不依赖游戏帧率。 */
	float CaptureAccumulator = 0.0f;
};
