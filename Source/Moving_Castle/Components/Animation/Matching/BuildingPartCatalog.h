#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BuildingPartCatalog.generated.h"

class UStaticMesh;

/** 导入单位、枢轴位置不同的网格也可以具有相同的几何身份。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FBuildingPartGeometry
{
	GENERATED_BODY()

	/** 对去平移、去统一单位缩放后的三角形几何生成签名，不依赖文件名或导出序号。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="几何身份")
	FString GeometryId;

	/** 原网格几何尺寸，用于把毫米导入的目标零件正确还原到厘米来源零件的显示尺寸。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="几何身份")
	FVector LocalSize = FVector::ZeroVector;
};

/**
 * 编辑器离线生成、运行时只读的零件几何目录。
 * 不要求发布版本保留静态网格 CPU 顶点，也不会在每次放置时扫描模型顶点。
 * 相同几何只说明可复用的形状；语义身份可通过组件 MergeIdentity=... 标签显式指定。
 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UBuildingPartCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 软引用只标识网格，不因加载目录而把所有等级的美术资产同时加载进内存。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="零件目录")
	TMap<TSoftObjectPtr<UStaticMesh>, FBuildingPartGeometry> Entries;

	/** 找不到目录项时返回空指针，由调用方仅使用已经明确相同的资产身份。 */
	const FBuildingPartGeometry* FindGeometry(const UStaticMesh* Mesh) const;

#if WITH_EDITOR
	/** 原生编辑器工具读取源三角形，完整生成成功后才替换目录；不修改输入网格资产。 */
	UFUNCTION(BlueprintCallable, Category="零件目录|编辑器")
	bool RebuildFromMeshes(const TArray<UStaticMesh*>& Meshes, FString& OutError);
#endif
};
