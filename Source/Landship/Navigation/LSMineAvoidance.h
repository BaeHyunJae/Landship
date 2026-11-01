#pragma once

#include "CoreMinimal.h"
#include "NavigationPath.h"

class APawn;
class ALSMine;

enum class ELSMinePathResult : uint8
{
	SafePath,
	HazardBlocked,
	NavigationFailed,
	SearchLimit,
	UnsupportedBody
};

struct LANDSHIP_API FLSMineSphere
{
	FVector Center = FVector::ZeroVector;
	float Radius = 0;
};

struct LANDSHIP_API FLSMineSafePath
{
	ELSMinePathResult Result = ELSMinePathResult::NavigationFailed;
	FNavPathSharedPtr Path;
	FVector Destination = FVector::ZeroVector;
	double Length = 0;
};

namespace LSMineAvoidance
{
LANDSHIP_API TArray<FLSMineSphere> Snapshot(const UWorld* World);
/** 수직 캡슐의 전체 이동 구간을 검사하며 경계 접촉도 포함한다. */
LANDSHIP_API bool CapsuleSegmentTouchesSphere(const FVector& StartCenter, const FVector& EndCenter, float CapsuleRadius,
											  float CapsuleHalfHeight, const FLSMineSphere& Sphere, float Margin = 0);
/** 실제 접촉은 거부한다. 안전 여유 안쪽의 비접촉 출발점에서는 여유를 줄이지 않는 이동만 허용한다. */
LANDSHIP_API bool IsCapsuleSegmentSafe(const FVector& StartCenter, const FVector& EndCenter, float CapsuleRadius,
									   float CapsuleHalfHeight, const FLSMineSphere& Sphere, float Margin = 1);
LANDSHIP_API bool IsSegmentSafe(const APawn* Pawn, const FVector& StartNav, const FVector& EndNav,
								const TArray<FLSMineSphere>& Mines, float Margin = 1);
/** 경로를 막는 지뢰와 서로 영향을 주는 근처 지뢰만 후보 생성에 포함한다. 전체 안전 검사를 대체하지 않는다. */
LANDSHIP_API bool GatherRelevantMineIndices(const TArray<FVector>& BodyCenters, float CapsuleRadius,
	float CapsuleHalfHeight, const TArray<FLSMineSphere>& Mines, int32 MaxMines, TArray<int32>& OutIndices);
/** 탐색량이 제한된다. SearchLimit은 기하학적으로 경로가 없다는 뜻이 아니다. */
LANDSHIP_API FLSMineSafePath BuildSafePath(APawn* Pawn, const FVector& Goal, const TArray<FLSMineSphere>& Mines);
LANDSHIP_API FLSMineSafePath FindDisarmPath(APawn* Pawn, const ALSMine* Mine);
/** 후보가 없으면 현재 캡슐, 있으면 투영된 내비게이션 위치의 캡슐 표면까지 작업 차폐를 검사한다. */
LANDSHIP_API bool HasDisarmLine(const APawn* Pawn, const ALSMine* Mine, const FVector* CandidateNavLocation = nullptr);
/** 접촉 구 중심부터 실제 수직 캡슐 표면까지의 거리다. 미지원 몸체는 무한대를 반환한다. */
LANDSHIP_API double GetDisarmDistance(const APawn* Pawn, const ALSMine* Mine);
} // namespace LSMineAvoidance
