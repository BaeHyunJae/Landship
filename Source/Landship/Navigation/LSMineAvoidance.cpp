#include "Navigation/LSMineAvoidance.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSMineHazardSubsystem.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Engine/World.h"
#include <limits>

namespace
{
bool CapsuleDimensions(const APawn* Pawn, FVector& Offset, float& Radius, float& HalfHeight)
{
	const UCapsuleComponent* Capsule = IsValid(Pawn) ? Pawn->FindComponentByClass<UCapsuleComponent>() : nullptr;
	if (!Capsule || !Capsule->GetUpVector().Equals(FVector::UpVector, .001))
		return false;
	Offset = Capsule->GetComponentLocation() - Pawn->GetNavAgentLocation();
	Radius = Capsule->GetScaledCapsuleRadius();
	HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	return Radius > 0 && HalfHeight >= Radius;
}

double AxisDistanceSquared(const FVector& Point, const FVector& Center, double AxisHalfHeight)
{
	const double DZ = FMath::Max(0.0, FMath::Abs(Point.Z - Center.Z) - AxisHalfHeight);
	return FVector::DistSquared2D(Point, Center) + DZ * DZ;
}

bool SegmentSafeAtOffset(const FVector& Start, const FVector& End, const FVector& Offset, float Radius,
						 float HalfHeight, const TArray<FLSMineSphere>& Mines, float Margin = 1)
{
	for (const FLSMineSphere& Mine : Mines)
		if (!LSMineAvoidance::IsCapsuleSegmentSafe(Start + Offset, End + Offset, Radius, HalfHeight, Mine, Margin))
			return false;
	return true;
}

bool SafePath(const FNavPathSharedPtr& Path, const TArray<FLSMineSphere>& Mines, const FVector& NavOffset, float Radius,
			  float HalfHeight)
{
	if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial() || Path->GetPathPoints().IsEmpty())
		return false;
	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	for (int32 Index = 0; Index < Points.Num(); ++Index)
		if (!SegmentSafeAtOffset(Points[FMath::Max(0, Index - 1)].Location, Points[Index].Location, NavOffset, Radius,
								 HalfHeight, Mines))
			return false;
	return true;
}

double PathLength(const FNavPathSharedPtr& Path)
{
	double Length = 0;
	for (int32 Index = 1; Index < Path->GetPathPoints().Num(); ++Index)
		Length += FVector::Distance(Path->GetPathPoints()[Index - 1].Location, Path->GetPathPoints()[Index].Location);
	return Length;
}
} // namespace

TArray<FLSMineSphere> LSMineAvoidance::Snapshot(const UWorld* World)
{
	TArray<FLSMineSphere> Result;
	const ULSMineHazardSubsystem* Hazards = World ? World->GetSubsystem<ULSMineHazardSubsystem>() : nullptr;
	if (Hazards)
		for (const TWeakObjectPtr<ALSMine>& Weak : Hazards->GetMines())
			if (const ALSMine* Mine = Weak.Get(); Mine && Mine->IsHazardActive())
				Result.Add({Mine->GetTriggerCenter(), Mine->GetTriggerRadius()});
	return Result;
}

bool LSMineAvoidance::CapsuleSegmentTouchesSphere(const FVector& StartCenter, const FVector& EndCenter,
												  float CapsuleRadius, float CapsuleHalfHeight,
												  const FLSMineSphere& Sphere, float Margin)
{
	const double AxisHalfHeight = FMath::Max(0.0f, CapsuleHalfHeight - CapsuleRadius);
	// 캡슐 축의 전체 이동면에서 최솟값을 찾아 경사면의 수직 이동도 검사한다.
	double Low = 0, High = 1;
	for (int32 Index = 0; Index < 48; ++Index)
	{
		const double Left = (2 * Low + High) / 3, Right = (Low + 2 * High) / 3;
		if (AxisDistanceSquared(Sphere.Center, FMath::Lerp(StartCenter, EndCenter, Left), AxisHalfHeight) <=
			AxisDistanceSquared(Sphere.Center, FMath::Lerp(StartCenter, EndCenter, Right), AxisHalfHeight))
			High = Right;
		else
			Low = Left;
	}
	const double Distance = FMath::Min3(
		AxisDistanceSquared(Sphere.Center, StartCenter, AxisHalfHeight),
		AxisDistanceSquared(Sphere.Center, EndCenter, AxisHalfHeight),
		AxisDistanceSquared(Sphere.Center, FMath::Lerp(StartCenter, EndCenter, (Low + High) / 2), AxisHalfHeight));
	return Distance <= FMath::Square(static_cast<double>(Sphere.Radius + CapsuleRadius + Margin)) + .0001;
}

bool LSMineAvoidance::IsSegmentSafe(const APawn* Pawn, const FVector& StartNav, const FVector& EndNav,
									const TArray<FLSMineSphere>& Mines, float Margin)
{
	FVector Offset;
	float Radius, HalfHeight;
	if (!CapsuleDimensions(Pawn, Offset, Radius, HalfHeight))
		return false;
	return SegmentSafeAtOffset(StartNav, EndNav, Offset, Radius, HalfHeight, Mines, Margin);
}

bool LSMineAvoidance::IsCapsuleSegmentSafe(const FVector& StartCenter, const FVector& EndCenter, float CapsuleRadius,
										   float CapsuleHalfHeight, const FLSMineSphere& Sphere, float Margin)
{
	if (!CapsuleSegmentTouchesSphere(StartCenter, EndCenter, CapsuleRadius, CapsuleHalfHeight, Sphere, Margin))
		return true;
	if (CapsuleSegmentTouchesSphere(StartCenter, EndCenter, CapsuleRadius, CapsuleHalfHeight, Sphere))
		return false;
	const double AxisHalfHeight = FMath::Max(0.0f, CapsuleHalfHeight - CapsuleRadius);
	const double InitialDistance = AxisDistanceSquared(Sphere.Center, StartCenter, AxisHalfHeight);
	if (InitialDistance > FMath::Square(static_cast<double>(Sphere.Radius + CapsuleRadius + Margin)) + .0001)
		return false;
	const FVector Delta = EndCenter - StartCenter;
	const FVector FromSphere = StartCenter - Sphere.Center;
	const double VerticalGap = FMath::Sign(FromSphere.Z) * FMath::Max(0.0, FMath::Abs(FromSphere.Z) - AxisHalfHeight);
	const double InitialDerivative = FromSphere.X * Delta.X + FromSphere.Y * Delta.Y + VerticalGap * Delta.Z;
	// 축 거리의 제곱은 볼록 함수이므로 출발 미분이 음수가 아니면 경로 내내 여유가 줄지 않는다.
	return InitialDerivative >= -1.e-8 &&
		   AxisDistanceSquared(Sphere.Center, EndCenter, AxisHalfHeight) >= InitialDistance - 1.e-8;
}

bool LSMineAvoidance::GatherRelevantMineIndices(const TArray<FVector>& BodyCenters, float CapsuleRadius,
	float CapsuleHalfHeight, const TArray<FLSMineSphere>& Mines, int32 MaxMines, TArray<int32>& OutIndices)
{
	OutIndices.Reset();
	TArray<bool> Included;
	Included.Init(false, Mines.Num());
	for (int32 MineIndex = 0; MineIndex < Mines.Num(); ++MineIndex)
	{
		bool bBlocksBaseline = false;
		for (int32 PointIndex = 0; PointIndex < BodyCenters.Num(); ++PointIndex)
			if (!IsCapsuleSegmentSafe(BodyCenters[FMath::Max(0, PointIndex - 1)], BodyCenters[PointIndex],
				CapsuleRadius, CapsuleHalfHeight, Mines[MineIndex]))
			{
				bBlocksBaseline = true;
				break;
			}
		if (bBlocksBaseline)
		{
			if (OutIndices.Num() >= MaxMines)
				return false;
			Included[MineIndex] = true;
			OutIndices.Add(MineIndex);
		}
	}
	// 후보 링끼리 맞닿으면 우회 중 이웃 지뢰를 만날 수 있으므로 연결된 영역까지 확장한다.
	// 높이와 무관한 최대 수평 반경은 후보 선택을 보수적으로 넓힐 뿐, 안전 검사에는 쓰지 않는다.
	auto NeighborhoodRadius = [CapsuleRadius](const FLSMineSphere& Mine)
	{
		return (Mine.Radius + CapsuleRadius + 3) / FMath::Cos(PI / 16) + 2;
	};
	for (int32 QueueIndex = 0; QueueIndex < OutIndices.Num(); ++QueueIndex)
	{
		const FLSMineSphere& Current = Mines[OutIndices[QueueIndex]];
		for (int32 MineIndex = 0; MineIndex < Mines.Num(); ++MineIndex)
		{
			if (Included[MineIndex] || FVector::DistSquared2D(Current.Center, Mines[MineIndex].Center) >
				FMath::Square(NeighborhoodRadius(Current) + NeighborhoodRadius(Mines[MineIndex])))
				continue;
			if (OutIndices.Num() >= MaxMines)
				return false;
			Included[MineIndex] = true;
			OutIndices.Add(MineIndex);
		}
	}
	return true;
}

FLSMineSafePath LSMineAvoidance::BuildSafePath(APawn* Pawn, const FVector& Goal, const TArray<FLSMineSphere>& Mines)
{
	FLSMineSafePath Result;
	FVector Offset;
	float Radius, HalfHeight;
	if (!CapsuleDimensions(Pawn, Offset, Radius, HalfHeight))
	{
		Result.Result = ELSMinePathResult::UnsupportedBody;
		return Result;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());
	const ANavigationData* Data =
		Nav ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	if (!Data || Goal.ContainsNaN())
		return Result;
	FNavLocation ProjectedOrigin;
	if (!Nav->ProjectPointToNavigation(Pawn->GetNavAgentLocation(), ProjectedOrigin, FVector(150, 150, 300), Data))
		return Result;
	// 내비게이션 표면은 실제 발바닥보다 높을 수 있다. 경로 좌표에 발바닥 기준 높이를 더하면 몸체를 과대 예측한다.
	FVector NavOffset = Offset;
	NavOffset.Z = Pawn->GetNavAgentLocation().Z + Offset.Z - ProjectedOrigin.Location.Z;
	FNavLocation Projected;
	if (!Nav->ProjectPointToNavigation(Goal, Projected, FVector(150, 150, 300), Data))
		return Result;
	const FVector Start(Pawn->GetNavAgentLocation().X, Pawn->GetNavAgentLocation().Y, ProjectedOrigin.Location.Z);
	Result.Destination = Projected.Location;
	auto Query = [&](const FVector& From, const FVector& To)
	{
		FPathFindingQuery Request(Pawn, *Data, From, To);
		Request.SetAllowPartialPaths(false);
		return Nav->FindPathSync(Pawn->GetNavAgentPropertiesRef(), Request).Path;
	};
	const FNavPathSharedPtr Baseline = Query(Start, Projected.Location);
	if (!Baseline.IsValid() || !Baseline->IsValid() || Baseline->IsPartial())
		return Result;
	if (SafePath(Baseline, Mines, NavOffset, Radius, HalfHeight))
	{
		Result.Result = ELSMinePathResult::SafePath;
		Result.Path = Baseline;
		Result.Length = PathLength(Baseline);
		return Result;
	}
	Result.Result = ELSMinePathResult::HazardBlocked;
	if (!SegmentSafeAtOffset(Start, Start, NavOffset, Radius, HalfHeight, Mines) ||
		!SegmentSafeAtOffset(Projected.Location, Projected.Location, NavOffset, Radius, HalfHeight, Mines))
		return Result;
	constexpr int32 Samples = 16, MaxVertices = 130, MaxQueries = 2048;
	TArray<FVector> BaselineBodyCenters;
	for (const FNavPathPoint& Point : Baseline->GetPathPoints())
		BaselineBodyCenters.Add(Point.Location + NavOffset);
	TArray<int32> RelevantMines;
	if (!GatherRelevantMineIndices(BaselineBodyCenters, Radius, HalfHeight, Mines,
		(MaxVertices - 2) / Samples, RelevantMines))
	{
		Result.Result = ELSMinePathResult::SearchLimit;
		return Result;
	}
	TArray<FVector> Vertices{Start, Projected.Location};
	for (int32 MineIndex : RelevantMines)
	{
		const FLSMineSphere& Mine = Mines[MineIndex];
		// 외접 다각형으로 표본 사이 선분이 이 높이의 접촉 경계를 자르지 않게 한다.
		const double AxisGap =
			FMath::Max(0.0, FMath::Abs(Start.Z + NavOffset.Z - Mine.Center.Z) - (HalfHeight - Radius));
		const double Expanded = Mine.Radius + Radius + 3;
		const double RingRadius =
			FMath::Sqrt(FMath::Max(0.0, Expanded * Expanded - AxisGap * AxisGap)) / FMath::Cos(PI / Samples) + 2;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double Angle = 2 * PI * Index / Samples;
			FVector Candidate(Mine.Center.X + FMath::Cos(Angle) * RingRadius,
							  Mine.Center.Y + FMath::Sin(Angle) * RingRadius, Start.Z);
			FNavLocation Point;
			if (Nav->ProjectPointToNavigation(Candidate, Point, FVector(30, 30, 300), Data) &&
				SegmentSafeAtOffset(Point.Location, Point.Location, NavOffset, Radius, HalfHeight, Mines))
				Vertices.Add(Point.Location);
		}
	}
	TArray<double> Costs;
	Costs.Init(std::numeric_limits<double>::infinity(), Vertices.Num());
	Costs[0] = 0;
	TArray<bool> Visited;
	Visited.Init(false, Vertices.Num());
	TArray<int32> Previous;
	Previous.Init(INDEX_NONE, Vertices.Num());
	TArray<FNavPathSharedPtr> Incoming;
	Incoming.SetNum(Vertices.Num());
	int32 Queries = 0;
	for (int32 Step = 0; Step < Vertices.Num(); ++Step)
	{
		int32 Current = INDEX_NONE;
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
			if (!Visited[Index] && FMath::IsFinite(Costs[Index]) &&
				(Current == INDEX_NONE || Costs[Index] < Costs[Current]))
				Current = Index;
		if (Current == INDEX_NONE)
			return Result;
		if (Current == 1)
			break;
		Visited[Current] = true;
		for (int32 Next = 1; Next < Vertices.Num(); ++Next)
		{
			if (Visited[Next] || Costs[Current] + FVector::Distance(Vertices[Current], Vertices[Next]) >= Costs[Next])
				continue;
			if (++Queries > MaxQueries)
			{
				Result.Result = ELSMinePathResult::SearchLimit;
				return Result;
			}
			const FNavPathSharedPtr Edge = Query(Vertices[Current], Vertices[Next]);
			if (!SafePath(Edge, Mines, NavOffset, Radius, HalfHeight))
				continue;
			const double Cost = Costs[Current] + PathLength(Edge);
			if (Cost < Costs[Next])
			{
				Costs[Next] = Cost;
				Previous[Next] = Current;
				Incoming[Next] = Edge;
			}
		}
	}
	if (Previous[1] == INDEX_NONE)
		return Result;
	TArray<int32> Chain;
	for (int32 Index = 1; Index != 0; Index = Previous[Index])
		Chain.Insert(Index, 0);
	TArray<FVector> Points{Start};
	for (int32 Index : Chain)
		for (const FNavPathPoint& Point : Incoming[Index]->GetPathPoints())
			if (!Points.Last().Equals(Point.Location, .01))
				Points.Add(Point.Location);
	Result.Path = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(Points);
	Result.Path->MarkReady();
	if (!SafePath(Result.Path, Mines, NavOffset, Radius, HalfHeight))
	{
		Result.Path.Reset();
		return Result;
	}
	Result.Result = ELSMinePathResult::SafePath;
	Result.Length = PathLength(Result.Path);
	return Result;
}

double LSMineAvoidance::GetDisarmDistance(const APawn* Pawn, const ALSMine* Mine)
{
	FVector Offset;
	float Radius, HalfHeight;
	if (!IsValid(Mine) || !CapsuleDimensions(Pawn, Offset, Radius, HalfHeight))
		return std::numeric_limits<double>::infinity();
	return FMath::Max(0.0, FMath::Sqrt(AxisDistanceSquared(Mine->GetTriggerCenter(),
														   Pawn->GetNavAgentLocation() + Offset, HalfHeight - Radius)) -
							   Radius);
}

bool LSMineAvoidance::HasDisarmLine(const APawn* Pawn, const ALSMine* Mine, const FVector* CandidateNavLocation)
{
	FVector Offset;
	float Radius, HalfHeight;
	if (!IsValid(Mine) || !CapsuleDimensions(Pawn, Offset, Radius, HalfHeight) || !Pawn->GetWorld())
		return false;
	FVector Center = Pawn->GetNavAgentLocation() + Offset;
	if (CandidateNavLocation)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());
		const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
		FNavLocation ProjectedOrigin;
		if (!Data || !Nav->ProjectPointToNavigation(Pawn->GetNavAgentLocation(), ProjectedOrigin, FVector(150, 150, 300), Data))
			return false;
		Center = *CandidateNavLocation + Offset;
		Center.Z = CandidateNavLocation->Z + Pawn->GetNavAgentLocation().Z + Offset.Z - ProjectedOrigin.Location.Z;
	}
	const FVector MineCenter = Mine->GetTriggerCenter();
	const FVector Axis(0, 0, HalfHeight - Radius);
	const FVector AxisPoint = FMath::ClosestPointOnSegment(MineCenter, Center - Axis, Center + Axis);
	const FVector ToMine = MineCenter - AxisPoint;
	const FVector Surface = ToMine.SizeSquared() <= FMath::Square(Radius) ? MineCenter : AxisPoint + ToMine.GetSafeNormal() * Radius;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSMineDisarmLine), false, Pawn);
	Query.AddIgnoredActor(Mine);
	return !Pawn->GetWorld()->LineTraceTestByChannel(MineCenter, Surface, LSCollisionChannels::ExplosionOcclusion, Query);
}

FLSMineSafePath LSMineAvoidance::FindDisarmPath(APawn* Pawn, const ALSMine* Mine)
{
	FLSMineSafePath Best;
	FVector Offset;
	float Radius, HalfHeight;
	if (!IsValid(Mine) || !CapsuleDimensions(Pawn, Offset, Radius, HalfHeight))
	{
		Best.Result = ELSMinePathResult::UnsupportedBody;
		return Best;
	}
	const TArray<FLSMineSphere> Mines = Snapshot(Pawn->GetWorld());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());
	const ANavigationData* Data =
		Nav ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	FNavLocation ProjectedOrigin;
	if (!Data ||
		!Nav->ProjectPointToNavigation(Pawn->GetNavAgentLocation(), ProjectedOrigin, FVector(150, 150, 300), Data))
		return Best;
	FVector NavOffset = Offset;
	NavOffset.Z = Pawn->GetNavAgentLocation().Z + Offset.Z - ProjectedOrigin.Location.Z;
	const double Distance = GetDisarmDistance(Pawn, Mine);
	if (Distance > Mine->GetTriggerRadius() + 2 && Distance < Mine->GetDisarmDistance() - 2 && HasDisarmLine(Pawn, Mine))
		return BuildSafePath(Pawn, Pawn->GetNavAgentLocation(), Mines);
	const FVector Center = Mine->GetTriggerCenter();
	const double Desired = (Mine->GetTriggerRadius() + Mine->GetDisarmDistance()) * .5;
	if (Desired <= Mine->GetTriggerRadius() + 2)
		return Best;
	const double AxisGap =
		FMath::Max(0.0, FMath::Abs(Pawn->GetNavAgentLocation().Z + Offset.Z - Center.Z) - (HalfHeight - Radius));
	const double Horizontal = FMath::Sqrt(FMath::Max(0.0, FMath::Square(Desired + Radius) - AxisGap * AxisGap));
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const double Angle = 2 * PI * Index / 16;
		FVector Goal(Center.X + Horizontal * FMath::Cos(Angle), Center.Y + Horizontal * FMath::Sin(Angle),
					 Pawn->GetNavAgentLocation().Z);
		FLSMineSafePath Candidate = BuildSafePath(Pawn, Goal, Mines);
		if (Candidate.Result != ELSMinePathResult::SafePath)
		{
			if (Best.Result != ELSMinePathResult::SafePath)
				Best.Result = Candidate.Result;
			continue;
		}
		const double Actual = FMath::Max(
			0.0,
			FMath::Sqrt(AxisDistanceSquared(Center, Candidate.Destination + NavOffset, HalfHeight - Radius)) - Radius);
		if (Actual <= Mine->GetTriggerRadius() + 2 || Actual >= Mine->GetDisarmDistance() - 2)
			continue;
		if (!HasDisarmLine(Pawn, Mine, &Candidate.Destination))
			continue;
		if (Best.Result != ELSMinePathResult::SafePath || Candidate.Length < Best.Length)
			Best = MoveTemp(Candidate);
	}
	return Best;
}
