#include "Match/LSMeleeApproachPlanner.h"
#include "Troops/LSUnit.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "Engine/World.h"

namespace
{
bool CanOccupy(ALSUnit* Unit, ALSUnit* Target, const FVector& Ground, float Clearance)
{
	float Radius, Height;
	Unit->GetSimpleCollisionCylinder(Radius, Height);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSMeleeSlot), false, Unit);
	Query.AddIgnoredActor(Target);
	return !Unit->GetWorld()->OverlapAnyTestByObjectType(
	    Ground + FVector(0, 0, Height + Clearance + 2), FQuat::Identity, Objects,
	    FCollisionShape::MakeCapsule(Radius + Clearance, Height + Clearance), Query);
}

bool FindBodyAhead(ALSUnit* Unit, ALSUnit* Target, const FVector& End, FHitResult& Hit)
{
	float Radius, Height;
	Unit->GetSimpleCollisionCylinder(Radius, Height);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSMeleeLocalRoute), false, Unit);
	Query.AddIgnoredActor(Target);
	const FVector Start = Unit->GetActorLocation();
	const FVector Direction = (End - Start).GetSafeNormal2D();
	// 이미 접촉한 몸체에서 벗어나는 방향은 막지 않는다. 목표 자리의 여유와 실제 이동 통과 반경은 구분한다.
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		if (!Unit->GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects,
		                                               FCollisionShape::MakeSphere(Radius), Query))
		{
			return false;
		}
		if (Hit.bStartPenetrating && IsValid(Hit.GetActor()) &&
		    FVector::DotProduct((Hit.GetActor()->GetActorLocation() - Start).GetSafeNormal2D(), Direction) <= 0)
		{
			Query.AddIgnoredActor(Hit.GetActor());
			continue;
		}
		return true;
	}
	return true;
}

bool FindCompletePath(ALSUnit* Unit, UNavigationSystemV1* Nav, const FVector& Goal, FVector& OutFirstStep)
{
	const ANavigationData* Data =
	    Nav->GetNavDataForProps(Unit->GetCharacterMovement()->NavAgentProps, Unit->GetNavAgentLocation());
	if (!Data)
	{
		return false;
	}
	FPathFindingQuery Query(Unit, *Data, Unit->GetNavAgentLocation(), Goal);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult Result = Nav->FindPathSync(Query);
	if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
	{
		return false;
	}
	const auto& Points = Result.Path->GetPathPoints();
	OutFirstStep = Points.Num() > 1 ? Points[1].Location : Goal;
	return true;
}
} // namespace

bool FLSMeleeApproachSettings::IsValid() const
{
	const float Positive[] = {StuckSeconds,  ProgressDistance, TargetMoveDistance,
	                          RetryInterval, BlockedSeconds,   LocalLookAhead};
	for (float Value : Positive)
	{
		if (!FMath::IsFinite(Value) || Value <= 0)
		{
			return false;
		}
	}
	return SlotCount >= 8 && SlotCount <= 32 && MaxTargetCandidates >= 1 && MaxTargetCandidates <= 8 &&
	       MaxPathChecks >= 1 && MaxPathChecks <= 8 && FMath::IsFinite(Clearance) && Clearance >= 0;
}

void FLSMeleeApproachPlanner::Reset()
{
	Reservations.Reset();
}
void FLSMeleeApproachPlanner::Release(ALSUnit* Unit)
{
	Reservations.Remove(Unit);
}
void FLSMeleeApproachPlanner::RejectDestination(ALSUnit* Unit, double Now, const FLSMeleeApproachSettings& Settings)
{
	if (FLSReservation* State = Reservations.Find(Unit))
	{
		State->BlockedPoint = State->Destination;
		State->AvoidPointUntil = Now + Settings.BlockedSeconds;
		State->bReserved = false;
		State->NextPlanTime = Now + Settings.RetryInterval;
	}
}

void FLSMeleeApproachPlanner::Prune()
{
	for (auto It = Reservations.CreateIterator(); It; ++It)
	{
		if (!ULSCombatTargetLibrary::IsCombatAlive(It.Key().Get()))
		{
			It.RemoveCurrent();
		}
		else if (!ULSCombatTargetLibrary::CanAutoAttackTarget(It.Key().Get(), It.Value().Target.Get()))
		{
			It.Value().bReserved = false;
			It.Value().Target.Reset();
		}
	}
}

bool FLSMeleeApproachPlanner::IsReserved(ALSUnit* Unit, const FVector& Goal, float Radius, float Clearance) const
{
	for (const auto& Pair : Reservations)
	{
		ALSUnit* Other = Pair.Key.Get();
		if (!Other || Other == Unit || Other->GetFaction() != Unit->GetFaction() || !Pair.Value.bReserved)
		{
			continue;
		}
		float OtherRadius, OtherHeight;
		Other->GetSimpleCollisionCylinder(OtherRadius, OtherHeight);
		const double Minimum = FMath::Square(Radius + OtherRadius + Clearance);
		if (FVector::DistSquared2D(Goal, Pair.Value.Goal) < Minimum ||
		    (Pair.Value.bDetouring && FVector::DistSquared2D(Goal, Pair.Value.Destination) < Minimum))
		{
			return true;
		}
	}
	return false;
}

bool FLSMeleeApproachPlanner::FindGoal(ALSUnit* Unit, ALSUnit* Target, UNavigationSystemV1* Nav, float AttackRange,
                                       const FLSMeleeApproachSettings& Settings, const FLSReservation& State,
                                       double Now, FVector& OutGoal, FVector& OutDestination) const
{
	float Radius, Height, TargetRadius, TargetHeight;
	Unit->GetSimpleCollisionCylinder(Radius, Height);
	Target->GetSimpleCollisionCylinder(TargetRadius, TargetHeight);
	const double Ring = Radius + TargetRadius + FMath::Max(static_cast<double>(Settings.Clearance), AttackRange * 0.65);
	FVector Direction = (Unit->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}
	TArray<FVector, TInlineAllocator<32>> Candidates;
	for (int32 Index = 0; Index < Settings.SlotCount; ++Index)
	{
		const float Angle = Index * 360.0f / Settings.SlotCount;
		const FVector Desired =
		    Target->GetNavAgentLocation() + Direction.RotateAngleAxis(Angle, FVector::UpVector) * Ring;
		FNavLocation Point;
		if (!Nav->ProjectPointToNavigation(Desired, Point, FVector(100, 100, 200)) ||
		    FVector::DistSquared2D(Desired, Point.Location) > FMath::Square(Settings.TargetMoveDistance))
		{
			continue;
		}
		const double Gap = FVector::Dist2D(Point.Location, Target->GetActorLocation()) - Radius - TargetRadius;
		if (Gap < Settings.Clearance || Gap > AttackRange ||
		    (Now < State.AvoidPointUntil &&
		     FVector::DistSquared2D(Point.Location, State.BlockedPoint) < FMath::Square(Radius * 1.5)) ||
		    IsReserved(Unit, Point.Location, Radius, Settings.Clearance) ||
		    !CanOccupy(Unit, Target, Point.Location, Settings.Clearance))
		{
			continue;
		}
		Candidates.Add(Point.Location);
	}
	Candidates.Sort(
	    [Unit](const FVector& A, const FVector& B)
	    {
		    return FVector::DistSquared2D(Unit->GetActorLocation(), A) <
		           FVector::DistSquared2D(Unit->GetActorLocation(), B);
	    });
	int32 RemainingPathChecks = Settings.MaxPathChecks;
	for (int32 Index = 0; Index < Candidates.Num() && RemainingPathChecks > 0; ++Index)
	{
		FVector FirstStep;
		--RemainingPathChecks;
		if (!FindCompletePath(Unit, Nav, Candidates[Index], FirstStep))
		{
			continue;
		}
		FVector Step = FirstStep - Unit->GetActorLocation();
		Step.Z = 0;
		const FVector LookEnd = Unit->GetActorLocation() + Step.GetClampedToMaxSize(Settings.LocalLookAhead);
		FHitResult Blocker;
		if (!FindBodyAhead(Unit, Target, LookEnd, Blocker))
		{
			OutGoal = Candidates[Index];
			OutDestination = OutGoal;
			return true;
		}
		ALSUnit* Friendly = Cast<ALSUnit>(Blocker.GetActor());
		// 적 전열을 자동 우회하지 않는다. 아군 혼잡을 피하는 국소 경유지만 만든다.
		if (!Friendly || Friendly->GetFaction() != Unit->GetFaction())
		{
			continue;
		}
		float OtherRadius, OtherHeight;
		Friendly->GetSimpleCollisionCylinder(OtherRadius, OtherHeight);
		const FVector Forward = Step.GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
		for (int32 SideIndex = 0; SideIndex < 4 && RemainingPathChecks > 0; ++SideIndex)
		{
			const double Sign = SideIndex % 2 == 0 ? 1 : -1;
			const double Back = SideIndex < 2 ? 0 : Radius;
			const FVector Desired = Unit->GetNavAgentLocation() +
			                        Side * Sign * (Radius + OtherRadius + Settings.Clearance) * 1.5 - Forward * Back;
			FNavLocation Detour;
			if (!Nav->ProjectPointToNavigation(Desired, Detour, FVector(100, 100, 200)) ||
			    FVector::DistSquared2D(Desired, Detour.Location) > FMath::Square(Settings.TargetMoveDistance) ||
			    (Now < State.AvoidPointUntil &&
			     FVector::DistSquared2D(Detour.Location, State.BlockedPoint) < FMath::Square(Radius)) ||
			    IsReserved(Unit, Detour.Location, Radius, Settings.Clearance) ||
			    !CanOccupy(Unit, Target, Detour.Location, Settings.Clearance))
			{
				continue;
			}
			FVector DetourStep;
			--RemainingPathChecks;
			if (!FindCompletePath(Unit, Nav, Detour.Location, DetourStep))
			{
				continue;
			}
			DetourStep.Z = Unit->GetActorLocation().Z;
			FHitResult DetourBlocker;
			if (FindBodyAhead(Unit, Target, DetourStep, DetourBlocker))
			{
				continue;
			}
			OutGoal = Candidates[Index];
			OutDestination = Detour.Location;
			return true;
		}
	}
	return false;
}

FLSMeleeApproachResult FLSMeleeApproachPlanner::Plan(ALSUnit* Unit, ALSUnit* PreferredTarget,
                                                     const TArray<ALSUnit*>& Opponents, double Now, float AttackRange,
                                                     const FLSMeleeApproachSettings& Settings)
{
	FLSMeleeApproachResult Result;
	Result.Target = PreferredTarget;
	if (!ULSCombatTargetLibrary::IsCombatAlive(Unit) || !Settings.IsValid() || !FMath::IsFinite(Now) ||
	    !FMath::IsFinite(AttackRange) || AttackRange <= 0)
	{
		Release(Unit);
		return Result;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Unit->GetWorld());
	if (!Nav)
	{
		Release(Unit);
		return Result;
	}
	FLSReservation& State = Reservations.FindOrAdd(Unit);
	if (ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, PreferredTarget) &&
	    ULSCombatTargetLibrary::GetAttackDistance(Unit, PreferredTarget) <= AttackRange)
	{
		State.Target = PreferredTarget;
		State.Goal = Unit->GetNavAgentLocation();
		State.Destination = State.Goal;
		State.TargetLocation = PreferredTarget->GetActorLocation();
		State.bReserved = true;
		State.bDetouring = false;
		State.LastProgressTime = Now;
		State.BestDistance = 0;
		Result.bCanApproach = true;
		Result.Destination = State.Destination;
		return Result;
	}
	const double Distance = FVector::Dist2D(Unit->GetActorLocation(), State.Destination);
	if (State.bReserved && Distance + Settings.ProgressDistance < State.BestDistance)
	{
		State.BestDistance = Distance;
		State.LastProgressTime = Now;
	}
	const bool bStuck = State.bReserved && Now - State.LastProgressTime >= Settings.StuckSeconds;
	if (bStuck)
	{
		State.BlockedPoint = State.Destination;
		State.AvoidPointUntil = Now + Settings.BlockedSeconds;
		State.bReserved = false;
	}
	if (Now < State.NextPlanTime && !bStuck)
	{
		Result.Target = ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, State.Target.Get()) ? State.Target.Get()
		                                                                                      : PreferredTarget;
		Result.bCanApproach = State.bReserved;
		Result.Destination = State.Destination;
		return Result;
	}
	State.NextPlanTime = Now + Settings.RetryInterval;
	if (State.bReserved && State.Target.Get() == PreferredTarget &&
	    FVector::DistSquared2D(State.TargetLocation, PreferredTarget->GetActorLocation()) <
	        FMath::Square(Settings.TargetMoveDistance) &&
	    (!State.bDetouring || Distance > Settings.ProgressDistance) &&
	    CanOccupy(Unit, PreferredTarget, State.Goal, Settings.Clearance))
	{
		Result.bCanApproach = true;
		Result.Destination = State.Destination;
		return Result;
	}
	State.bReserved = false;
	TArray<ALSUnit*, TInlineAllocator<16>> Targets;
	for (ALSUnit* Candidate : Opponents)
	{
		if (ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Candidate))
		{
			Targets.Add(Candidate);
		}
	}
	Targets.Sort(
	    [Unit](const ALSUnit& A, const ALSUnit& B)
	    {
		    return FVector::DistSquared2D(Unit->GetActorLocation(), A.GetActorLocation()) <
		           FVector::DistSquared2D(Unit->GetActorLocation(), B.GetActorLocation());
	    });
	// 현재 표적을 먼저 유지하되 접근 자리를 확보하지 못할 때만 가까운 대안을 검사한다.
	Targets.Remove(PreferredTarget);
	if (ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, PreferredTarget))
	{
		Targets.Insert(PreferredTarget, 0);
	}
	for (int32 Index = 0; Index < FMath::Min(Targets.Num(), Settings.MaxTargetCandidates); ++Index)
	{
		FVector Goal, Destination;
		if (!FindGoal(Unit, Targets[Index], Nav, AttackRange, Settings, State, Now, Goal, Destination))
		{
			continue;
		}
		const bool bNewDestination =
		    !State.Target.IsValid() || State.Target.Get() != Targets[Index] ||
		    FVector::DistSquared2D(State.Destination, Destination) > FMath::Square(Settings.ProgressDistance);
		State.Target = Targets[Index];
		State.Goal = Goal;
		State.Destination = Destination;
		State.TargetLocation = Targets[Index]->GetActorLocation();
		State.bReserved = true;
		State.bDetouring = !Goal.Equals(Destination, 1);
		if (bNewDestination || bStuck)
		{
			State.LastProgressTime = Now;
			State.BestDistance = FVector::Dist2D(Unit->GetActorLocation(), Destination);
		}
		Result.Target = Targets[Index];
		Result.Destination = Destination;
		Result.bCanApproach = true;
		return Result;
	}
	return Result;
}
