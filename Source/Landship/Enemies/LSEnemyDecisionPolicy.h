#pragma once

#include "CoreMinimal.h"

/** 월드나 액터 수명과 독립적으로 검증할 수 있는 적 판단 규칙이다. */
namespace LSEnemyDecisionPolicy
{
struct FLSEnemyTargetScore
{
	float Health = 0.0f;
	double DistanceSquared = 0.0;
	uint32 StableId = 0;
	bool bCurrent = false;
};

inline bool IsBetterTarget(const FLSEnemyTargetScore& Candidate, const FLSEnemyTargetScore& Best)
{
	if (Candidate.Health != Best.Health)
	{
		return Candidate.Health < Best.Health;
	}
	if (Candidate.bCurrent != Best.bCurrent)
	{
		return Candidate.bCurrent;
	}
	if (Candidate.DistanceSquared != Best.DistanceSquared)
	{
		return Candidate.DistanceSquared < Best.DistanceSquared;
	}
	return Candidate.StableId < Best.StableId;
}

struct FLSEnemyRouteSample
{
	float Progress = 0.0f;
	float Length = 0.0f;
	float DistanceFromRoute = TNumericLimits<float>::Max();
	bool bValid = false;
};

/** 수평 경로에서 현재 위치를 투영한다. 이동량을 누적하지 않아 왕복과 회피를 전진으로 세지 않는다. */
inline FLSEnemyRouteSample SampleRoute(TConstArrayView<FVector> Route, FVector Location)
{
	FLSEnemyRouteSample Result;
	if (Route.IsEmpty())
	{
		return Result;
	}
	Result.bValid = true;
	if (Route.Num() == 1)
	{
		Result.DistanceFromRoute = static_cast<float>(FVector::Dist2D(Route[0], Location));
		return Result;
	}
	double ClosestDistanceSquared = TNumericLimits<double>::Max();
	double AlongRoute = 0.0;
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		const FVector Start(Route[Index - 1].X, Route[Index - 1].Y, 0.0);
		const FVector Segment(Route[Index].X - Start.X, Route[Index].Y - Start.Y, 0.0);
		const FVector Offset(Location.X - Start.X, Location.Y - Start.Y, 0.0);
		const double SegmentLengthSquared = Segment.SizeSquared();
		const double SegmentLength = FMath::Sqrt(SegmentLengthSquared);
		const double Fraction = SegmentLengthSquared > 0.0
			? FMath::Clamp(FVector::DotProduct(Offset, Segment) / SegmentLengthSquared, 0.0, 1.0) : 0.0;
		const double DistanceSquared = (Offset - Segment * Fraction).SizeSquared();
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			Result.Progress = static_cast<float>(AlongRoute + SegmentLength * Fraction);
		}
		AlongRoute += SegmentLength;
	}
	Result.Length = static_cast<float>(AlongRoute);
	Result.DistanceFromRoute = static_cast<float>(FMath::Sqrt(ClosestDistanceSquared));
	return Result;
}

/** 실제 진행 거리 또는 확인된 전략 공격 위치 도달로 회복을 마친다. 경로 실패는 도달이 아니다. */
inline bool IsRecoveryComplete(float ActualProgress, float RequiredAdvanceDistance, bool bAtStrategicAttackPosition)
{
	return ActualProgress >= RequiredAdvanceDistance || bAtStrategicAttackPosition;
}
}
