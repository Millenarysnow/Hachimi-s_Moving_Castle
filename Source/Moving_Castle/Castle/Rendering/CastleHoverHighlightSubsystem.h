#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleHoverHighlightSubsystem.generated.h"

class APostProcessVolume;
class UPrimitiveComponent;

/**
 * 管理单个游戏世界的悬停白边后处理体积及组件 Stencil 标记。
 *
 * 该子系统只负责渲染状态，不解析建筑、主塔或空岛的玩法规则。调用方提交
 * 当前目标的可见组件集合，子系统按位写入悬停标记，并在目标切换或世界结束
 * 时恢复每个组件进入高亮前的完整 CustomDepth 状态。
 */
UCLASS()
class MOVING_CASTLE_API UCastleHoverHighlightSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 仅在 Game 与 PIE 世界建立后处理体积，避免改动编辑器预览世界。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	/** 加载指定的后处理材质，并为当前世界创建无界临时体积。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 世界结束时精确还原组件，临时体积由世界回收。 */
	virtual void Deinitialize() override;

	/**
	 * 原子切换本帧高亮组件集合。
	 * 新集合中的组件必须属于当前世界且已经注册。输入无效时记录错误并保留旧
	 * 集合。重复提交相同集合不会重写渲染状态。
	 */
	void SetHighlightedPrimitives(const TArray<UPrimitiveComponent*>& Primitives);

	/** 清空当前集合并恢复所有仍然存活的组件状态。 */
	void ClearHighlight();

	/** 返回当前仍有效的高亮组件，仅供只读查询和自动化验收使用。 */
	TArray<UPrimitiveComponent*> GetHighlightedPrimitives() const;

private:
	/** 记录单个组件首次进入高亮前的原始渲染状态。 */
	struct FOriginalCustomDepthState
	{
		bool bRenderCustomDepth = false;
		int32 CustomDepthStencilValue = 0;
	};

	/** 精确恢复单个仍然有效的组件。 */
	void RestoreComponent(UPrimitiveComponent& Component, const FOriginalCustomDepthState& State) const;

	/** 无界体积只持有本功能材质，不覆盖曝光、色调、抗锯齿或镜头设置。 */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> HoverPostProcessVolume;

	/** 弱引用避免建筑拆除或动态交互组件销毁后留下悬空对象。 */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FOriginalCustomDepthState> OriginalComponentStates;

	/** 初始化失败时保留错误状态，不退回其它材质或渲染流程。 */
	bool bReady = false;
};
