#include "Maps/LSBattlefieldCastleNavMesh.h"

ALSBattlefieldCastleNavMesh::ALSBattlefieldCastleNavMesh(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCellSize(ENavigationDataResolution::Default, 10.0f);
	MaxSimplificationError = 0.5f;
}
