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
	if (!SourceFaction || !TargetFaction || !LSFaction::IsValid(SourceFaction->GetFaction())
		|| !LSFaction::IsValid(TargetFaction->GetFaction()) || SourceFaction->GetFaction() == TargetFaction->GetFaction())
	{
		return false;
	}
	const ULSHealthComponent* SourceHealth = Source->FindComponentByClass<ULSHealthComponent>();
	const ULSHealthComponent* TargetHealth = Target->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot SourceState;
	FLSHealthSnapshot TargetState;
	return SourceHealth && TargetHealth && SourceHealth->TryGetHealth(SourceState)
		&& TargetHealth->TryGetHealth(TargetState) && !SourceState.bIsDead && !TargetState.bIsDead;
}
