#include "Navigation/LSCastlePathGeometry.h"
#include "Castle/LSCastle.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "NavMesh/NavMeshPath.h"

void LSCastlePathGeometry::OffsetCorners(const APawn* Pawn, const FNavPathSharedPtr& Path)
{
	const ALSCastle* Castle = Cast<ALSCastle>(Pawn);
	const auto* Movement = Castle ? Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent()) : nullptr;
	if (!Movement || !Movement->bTurnBeforeForward || !Path.IsValid() || !Path->IsValid() || Path->IsPartial())
		return;
	if (FNavMeshPath* MeshPath = Path->CastPath<FNavMeshPath>())
	{
		// 실제 몸체를 가로지를 수 있는 모서리 선분에 기하 여유를 준다. Nav 크기·목표점은 유지한다.
		// 엔진은 포털 반폭으로 제한하며 100점을 넘는 경로에는 적용하지 않는다.
		constexpr double CornerClearance = 50.0;
		MeshPath->OffsetFromCorners(CornerClearance);
	}
}
