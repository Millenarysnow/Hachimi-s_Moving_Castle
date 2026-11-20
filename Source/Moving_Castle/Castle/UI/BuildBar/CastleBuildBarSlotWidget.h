// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CastleBuildBarSlotWidget.generated.h"

class UCastleBuildingPreviewWidget;
class UImage;
class USizeBox;
class UTextBlock;
struct FCastleBuildingDescriptor;

/** 建造栏单个槽位。WBP 保存切图、状态层位置和样式，C++ 只填入真实卡牌并切换状态。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleBuildBarSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 检查 WBP 绑定与切图资源；失效返回失败，不创建替代控件或图片。 */
	bool ValidatePresentation() const;

	/** 有牌时更新正式模型、数字和等级；无牌是正常状态，清除旧模型并显示半透明空槽。 */
	bool SetCard(const FCastleBuildingDescriptor* Card);

	/** 选中状态由控制器提供；只修改圆底和黄点，不重新生成建筑预览。 */
	void SetSelected(bool bSelected);

private:
	/** 常态与空槽各有一张原画切图，不通过调亮同一张图模拟。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> NormalCircle;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> EmptyCircle;

	/** 选中原画已经包含内底、金环和外发光；状态点独立放在圆的下方。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SelectedCircle;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SelectionDot;

	/** 白色槽位上沿用旧卡片的深色衬底，让既有亮色描边仍然清晰；空槽时隐藏。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> PreviewBackdrop;

	/** 预览区保持正方形；独立的数字与等级避免和父级 WBP 的快捷键徽章混淆。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> PreviewArea;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CardStats;

	/** 只在本槽首次有牌时创建，之后重复使用同一控件与共享的模型捕获。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleBuildingPreviewWidget> Preview;

	/** 决定空槽、常态和选中三张互斥圆底中的哪张可见。 */
	bool bHasCard = false;
};
