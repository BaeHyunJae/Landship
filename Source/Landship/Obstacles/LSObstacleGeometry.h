#pragma once

#include "CoreMinimal.h"

class ALSPathObstacle;
class UPrimitiveComponent;

namespace LSObstacleGeometry
{
/** 구의 원판 또는 회전 상자의 실제 XY 투영까지의 거리다. 미지원 몸체는 무한대를 반환한다. */
LANDSHIP_API double GetPlanarBodyDistance(const UPrimitiveComponent* Body, const FVector& Point);
/** 실제 단순 충돌만 조회하며 액터 경계로 대체하지 않는다. */
LANDSHIP_API bool GetClosestBodyPoint(const UPrimitiveComponent* Body, const FVector& Point, FVector& OutPoint,
									  double& OutDistance);
LANDSHIP_API double GetAttackDistance(const AActor* Source, const ALSPathObstacle* Target,
									  const FVector& SourceLocation);
LANDSHIP_API TArray<FVector> GetAttackApproachCandidates(const AActor* Source, const ALSPathObstacle* Target,
														 float Range);
} // namespace LSObstacleGeometry
