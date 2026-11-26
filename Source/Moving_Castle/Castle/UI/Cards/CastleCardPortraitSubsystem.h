// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CastleGameplayTypes.h"
#include "CoreMinimal.h"
#include "PreviewScene.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleCardPortraitSubsystem.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/** 明确区分排队、成像完成和失败。等待不是资源缺失时的替代画面。 */
enum class ECastleCardPortraitState : uint8
{
	Pending,
	Ready,
	Failed
};

/** 单种等级模型的静止成像；由卡片和世界缓存共享，不包含拖动视角与箔光。 */
UCLASS()
class MOVING_CASTLE_API UCastleCardPortrait : public UObject
{
	GENERATED_BODY()

public:
	ECastleCardPortraitState GetState() const { return State; }
	UTextureRenderTarget2D* GetTexture() const { return Texture; }
	const FText& GetFailureReason() const { return FailureReason; }

private:
	friend class UCastleCardPortraitSubsystem;

	/** 持有定义直到取景完成，模型从真实的等级类生成。 */
	UPROPERTY(Transient)
	FCastleBuildingDescriptor Descriptor;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Texture;

	float CellSize = 0.0f;
	float LayerHeight = 0.0f;
	ECastleCardPortraitState State = ECastleCardPortraitState::Pending;
	FText FailureReason;
};

/**
 * 仅供建筑牌摄影使用的世界服务。摄影场景不进入战斗世界，不运行 BeginPlay，也不拍世界背景。
 * 一台相机按需写入独立的静止纹理与一张互动纹理，由可见商店或独立卡牌驱动。
 */
UCLASS()
class MOVING_CASTLE_API UCastleCardPortraitSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 以正式棋盘的尺寸取景，同定义、等级、属性色和尺度的卡共享静止照片。 */
	UCastleCardPortrait* RequestPortrait(const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board,
		FText& OutFailureReason);

	/** 商店显式声明可见性；收起时停止相机，缓存的静止图可在再次打开时复用。 */
	void SetShopVisible(UObject* Shop, bool bVisible);

	/** 独立卡只在挂载期间驱动它自己的拍摄请求，关闭时不会继续处理商店的积压请求。 */
	void SetStandalonePortraitVisible(UObject* Consumer, UCastleCardPortrait* Portrait, bool bVisible);

	/** 当前持牌者独占动态纹理。变更倾角只排队，不在输入事件里阻塞渲染。 */
	void SetDynamicView(UObject* Consumer, UCastleCardPortrait* Portrait, const FVector2D& Tilt);
	void ClearDynamicView(const UObject* Consumer);
	/** 同一纹理可连续写入；成像编号让牌面知道何时必须重新合成。 */
	UTextureRenderTarget2D* GetDynamicTexture(const UObject* Consumer, uint64* OutCaptureNumber = nullptr) const;
	const FText* GetDynamicFailure(const UObject* Consumer) const;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	bool CreateStudio(FText& OutFailureReason);
	bool StagePortrait(UCastleCardPortrait* Portrait, FText& OutFailureReason);
	bool CaptureInto(UTextureRenderTarget2D* Target, const FVector2D& Tilt, FText& OutFailureReason);
	void FailPortrait(UCastleCardPortrait* Portrait, const FText& Reason) const;
	void DropStage();
	void ResetDynamicView();
	int32 FindPendingCaptureIndex() const;
	bool HasDynamicCaptureRequest() const;

	/** 同层模型的一次静止成像与入队状态共享，但拖动视角写进另外一张纹理。 */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UCastleCardPortrait>> Portraits;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCastleCardPortrait>> PendingPortraits;

	TUniquePtr<FPreviewScene> Studio;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Camera;

	UPROPERTY(Transient)
	TObjectPtr<ACastleBuildingBase> StagedModel;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCardPortrait> StagedPortrait;

	/** 取景基于模型实际世界包围盒，不用碰撞、功能几何或 UI 标签确定镜头。 */
	FVector StageCenter = FVector::ZeroVector;
	float StageDistance = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> DynamicTexture;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCardPortrait> DynamicPortrait;

	TWeakObjectPtr<UObject> VisibleShop;
	TMap<TWeakObjectPtr<UObject>, TWeakObjectPtr<UCastleCardPortrait>> VisibleStandalonePortraits;
	TWeakObjectPtr<UObject> DynamicConsumer;
	FVector2D DynamicTilt = FVector2D::ZeroVector;
	FText DynamicFailure;
	uint64 DynamicCaptureNumber = 0;
	bool bDynamicNeedsCapture = false;
	bool bDynamicReady = false;
	double NextCaptureAt = 0.0;
	double GeometryWaitStartedAt = 0.0;
};
