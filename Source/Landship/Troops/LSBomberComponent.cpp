#include "Troops/LSBomberComponent.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Troops/LSTroopUnit.h"

bool ULSBomberComponent::IsConfigurationValid() const
{
	return Super::IsConfigurationValid() && FMath::IsFinite(BlastRadius) && BlastRadius > 0;
}

void ULSBomberComponent::ResetForNewLife()
{
	Super::ResetForNewLife();
	bSpent = false;
}

FLSCombatDirective ULSBomberComponent::EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents)
{
	FLSCombatDirective Result = Super::EvaluateEngagement(Target, Opponents);
	Result.bSuppressBasicAttack = true;
	Result.bUseMeleeApproach = true;
	ALSTroopUnit* Unit = GetTroop();
	FLSUnitStatRow Stats;
	if (bSpent || !IsBattleEnabled() || IsStaggered() || !ULSCombatTargetLibrary::IsCombatAlive(Unit) ||
	    !Unit->TryGetStats(Stats))
	{
		return Result;
	}
	Result.ApproachRange = Stats.AttackRange;
	const FVector Origin = Unit->GetActorLocation();
	const double BlastReach = BlastRadius - Unit->GetSimpleCollisionRadius();
	UWorld* World = GetWorld();
	const auto IsOccluded = [World, Unit, &Origin](const ALSUnit* Opponent)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBomberBlast), false, Unit);
		Query.AddIgnoredActor(Opponent);
		return World->LineTraceTestByChannel(Origin, Opponent->GetActorLocation(),
		                                     LSCollisionChannels::ExplosionOcclusion, Query);
	};
	TArray<TWeakObjectPtr<ALSUnit>, TInlineAllocator<16>> Victims;
	bool bContact = false;
	for (ALSUnit* Opponent : Opponents)
	{
		if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Opponent))
			continue;
		const double Gap = ULSCombatTargetLibrary::GetAttackDistance(Unit, Opponent);
		if (!bContact && Gap <= Stats.AttackRange && !IsOccluded(Opponent))
			bContact = true;
		if (Gap <= BlastReach)
			Victims.Add(Opponent);
	}
	if (!bContact)
		return Result;
	Victims.RemoveAll([&IsOccluded](const TWeakObjectPtr<ALSUnit>& Candidate)
	{
		return !Candidate.IsValid() || IsOccluded(Candidate.Get());
	});

	auto* OwnHealth = Unit->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot State;
	if (!OwnHealth || !OwnHealth->TryGetHealth(State))
		return Result;

	bSpent = true;
	auto Blast = ULSHealthComponent::MakeDamageRequest(Stats.AttackPower, Unit, Unit);
	Blast.bHasAttackOrigin = true;
	Blast.AttackOrigin = Origin;
	auto Consumption = ULSHealthComponent::MakeDamageRequest(State.CurrentHealth, nullptr, Unit);
	Consumption.Policy = ELSDamagePolicy::EnvironmentalHazard;
	Consumption.bIgnoreArmor = true;
	// 폭발에 소모되는 처리는 오폭 설정과 무관하다. 사망 알림으로 재진입해도 폭발은 한 번이다.
	OwnHealth->ApplyDamage(Consumption);
	if (ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		bSpent = false;
		return Result;
	}
	if (!IsValid(World) || World->bIsTearingDown)
		return Result;
	DrawDebugSphere(World, Origin, BlastRadius, 20, FColor::Orange, false, 0.3f, 0, 3.0f);
	for (const auto& Victim : Victims)
	{
		ALSUnit* Opponent = Victim.Get();
		if (!ULSCombatTargetLibrary::IsCombatAlive(Opponent) || Opponent->GetFaction() == Blast.SourceFaction)
			continue;
		if (!IsValid(Blast.SourceActor) || Blast.SourceActor->IsActorBeingDestroyed())
		{
			Blast.SourceActor = nullptr;
			Blast.DamageCauser = nullptr;
		}
		if (auto* Health = Opponent->FindComponentByClass<ULSHealthComponent>())
		{
			Blast.bHasImpactLocation = true;
			Blast.ImpactLocation = Opponent->GetActorLocation();
			Health->ApplyDamage(Blast);
		}
	}
	Result.bHoldPosition = true;
	return Result;
}
