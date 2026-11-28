// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "CastleDirectorNodeActor.generated.h"

class USphereComponent;
class UNiagaraComponent;
class UWidgetComponent;
class UCastleDirectorNodeLabelWidget;
class UCastleDirectorConfigDataAsset;

/** 浮空节点拥有正式门户表现、触发球及标签，不执行奖励或战斗流程。 */
UCLASS()
class MOVING_CASTLE_API ACastleDirectorNodeActor : public AActor
{
	GENERATED_BODY()

public:
	ACastleDirectorNodeActor();
	/** 延迟生成期间写入视觉配置；完成生成后再显式激活碰撞。 */
	bool Configure(const UCastleDirectorConfigDataAsset& Config, int32 Step, ECastleDirectorNodeKind Kind);
	void ActivateNode();
	ECastleDirectorNodeKind GetKind() const { return NodeKind; }
	/** 节点屏幕空间标签实例与文字控件已创建并成功绑定。 */
	bool IsLabelReady() const { return bLabelReady; }

protected:
	virtual void BeginPlay() override;

private:
	/** 节点只通过此触发球接收 Pawn 重叠。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger;
	/** 圆形 Niagara 仅提供表现，与交互和导航无关。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Portal;
	/** 标签独立于世界空间的门户表现，以屏幕空间显示中文关号和节点类型。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Label;
	/** 从正式配置读取的标签 WBP 类，用于诊断实例化和绑定失败。 */
	TSubclassOf<UCastleDirectorNodeLabelWidget> LabelWidgetClass;
	/** 当前节点对应的关卡编号，标签文字在控件绑定后由 C++ 生成。 */
	int32 StepNumber = 0;
	/** 标签控件实例完成绑定和样式设置后才允许节点批次提交。 */
	bool bLabelReady = false;
	TWeakObjectPtr<APawn> BlockedInitialPawn;
	ECastleDirectorNodeKind NodeKind = ECastleDirectorNodeKind::Normal;
	bool bActive = false;
	/** 检查屏幕空间 WBP 及必需的 LabelText 绑定，并写入节点文字和颜色。 */
	bool InitializeLabelWidget();

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
