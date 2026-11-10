#include "CastleMassUnitConfig.h"

#include "CastleMassTraits.h"
#include "MassLODTrait.h"

UCastleMassEnemyConfig::UCastleMassEnemyConfig()
{
	Config.AddTrait(*CreateDefaultSubobject<UCastleMassEnemyTrait>(TEXT("Enemy")));
	Config.AddTrait(*CreateDefaultSubobject<UMassLODCollectorTrait>(TEXT("LODCollector")));
	Config.AddTrait(*CreateDefaultSubobject<UCastleMassHumanoidVisualizationTrait>(TEXT("HumanoidVisualization")));
}

UCastleMassNPCConfig::UCastleMassNPCConfig()
{
	Config.AddTrait(*CreateDefaultSubobject<UCastleMassNPCTrait>(TEXT("NPC")));
	Config.AddTrait(*CreateDefaultSubobject<UMassLODCollectorTrait>(TEXT("LODCollector")));
	Config.AddTrait(*CreateDefaultSubobject<UCastleMassHumanoidVisualizationTrait>(TEXT("HumanoidVisualization")));
}

UCastleMassProjectileConfig::UCastleMassProjectileConfig()
{
	Config.AddTrait(*CreateDefaultSubobject<UCastleMassProjectileTrait>(TEXT("Projectile")));
	Config.AddTrait(*CreateDefaultSubobject<UMassLODCollectorTrait>(TEXT("LODCollector")));
	UMassMovableVisualizationTrait* Visual = CreateDefaultSubobject<UMassMovableVisualizationTrait>(TEXT("ProjectileVisualization"));
	for (int32 LOD = 0; LOD < EMassLOD::Max; ++LOD)
	{
		Visual->Params.LODRepresentation[LOD] = LOD == EMassLOD::Off ? EMassRepresentationType::None : EMassRepresentationType::StaticMeshInstance;
		Visual->LODParams.LODMaxCount[LOD] = MAX_int32;
	}
	Visual->Params.bKeepLowResActors = false;
	Config.AddTrait(*Visual);
}
