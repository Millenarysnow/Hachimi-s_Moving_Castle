#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"

class UStaticMesh;

namespace HachimisV4MeshValidation
{
	/** 对照作者 LOD0 与实际渲染/SDF 输出；不修改资源或替换异常边界。 */
	bool Verify(UStaticMesh& Mesh, FString& Reason);
}
#endif
