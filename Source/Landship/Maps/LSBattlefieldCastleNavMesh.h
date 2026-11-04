#pragma once

#include "NavMesh/RecastNavMesh.h"
#include "LSBattlefieldCastleNavMesh.generated.h"

/** Castle-only contour precision, configured before navigation data registration. */
UCLASS(Config = Engine, defaultconfig)
class LANDSHIP_API ALSBattlefieldCastleNavMesh : public ARecastNavMesh
{
	GENERATED_BODY()

public:
	ALSBattlefieldCastleNavMesh(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
