#include "Combat/LSCombatTargetLibrary.h"

#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "GameFramework/Actor.h"

bool ULSCombatTargetLibrary::CanAutoAttackTarget(const AActor* Source, const AActor* Target)
{
	if (!IsValid(Source) || !IsValid(Target) || Source == Target)
	{
		return false;
	}
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
