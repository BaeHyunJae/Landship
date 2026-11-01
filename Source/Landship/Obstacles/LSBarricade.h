#pragma once
#include "Obstacles/LSPathObstacle.h"
#include "LSBarricade.generated.h"
UCLASS(Blueprintable)
class LANDSHIP_API ALSBarricade : public ALSPathObstacle
{
	GENERATED_BODY()
  public:
	ALSBarricade();
};
