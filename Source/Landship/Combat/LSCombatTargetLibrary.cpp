#include "Combat/LSCombatTargetLibrary.h"

#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "GameFramework/Actor.h"
#include "Obstacles/LSPathObstacle.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSObstacleGeometry.h"
#include <limits>

double ULSCombatTargetLibrary::GetAttackDistance(const AActor* Source, const AActor* Target)
{
	return IsValid(Source) ? GetAttackDistanceAtLocation(Source, Target, Source->GetActorLocation())
	                       : std::numeric_limits<double>::infinity();
}

double ULSCombatTargetLibrary::GetAttackDistanceAtLocation(const AActor* Source, const AActor* Target,
                                                           const FVector& SourceLocation)
{
	if (!IsValid(Source) || !IsValid(Target) || SourceLocation.ContainsNaN() ||
	    Target->GetActorLocation().ContainsNaN())
		return std::numeric_limits<double>::infinity();
	if (const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(Target))
		return LSObstacleGeometry::GetAttackDistance(Source, Obstacle, SourceLocation);
	float SourceRadius, SourceHeight, TargetRadius, TargetHeight;
	Source->GetSimpleCollisionCylinder(SourceRadius, SourceHeight);
	Target->GetSimpleCollisionCylinder(TargetRadius, TargetHeight);
	const double Horizontal =
	    FMath::Max(0.0, FVector::Dist2D(SourceLocation, Target->GetActorLocation()) - SourceRadius - TargetRadius);
	const double Vertical =
	    FMath::Max(0.0, FMath::Abs(SourceLocation.Z - Target->GetActorLocation().Z) - SourceHeight - TargetHeight);
	return FMath::Sqrt(Horizontal * Horizontal + Vertical * Vertical);
}

bool ULSCombatTargetLibrary::CanAutoAttackTarget(const AActor* Source, const AActor* Target)
{
	if (!IsValid(Source) || !IsValid(Target) || Source == Target)
	{
		return false;
	}
	if (const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(Target); Obstacle && !Obstacle->IsObstacleActive())
		return false;
	if (const ALSMine* Mine = Cast<ALSMine>(Target); Mine && !Mine->IsArmed())
		return false;
	const ILSFactionInterface* SourceFaction = Cast<ILSFactionInterface>(Source);
	const ILSFactionInterface* TargetFaction = Cast<ILSFactionInterface>(Target);
	if (!SourceFaction || !TargetFaction || !LSFaction::IsValid(SourceFaction->GetFaction()) ||
	    !LSFaction::IsValid(TargetFaction->GetFaction()) || SourceFaction->GetFaction() == TargetFaction->GetFaction())
	{
		return false;
	}
	return IsCombatAlive(Source) && IsCombatAlive(Target);
}

bool ULSCombatTargetLibrary::IsCombatAlive(const AActor* Target)
{
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return false;
	}
	const ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot Snapshot;
	return Health && Health->TryGetHealth(Snapshot) && !Snapshot.bIsDead;
}
