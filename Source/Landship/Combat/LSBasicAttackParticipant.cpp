#include "Combat/LSBasicAttackParticipant.h"

#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "GameFramework/Actor.h"

bool ILSBasicAttackParticipant::IsCombatAlive_Implementation() const
{
	const AActor* Owner = Cast<AActor>(this);
	const ULSHealthComponent* Health = IsValid(Owner) ? Owner->FindComponentByClass<ULSHealthComponent>() : nullptr;
	FLSHealthSnapshot Snapshot;
	return Health && !Owner->IsActorBeingDestroyed() && Health->TryGetHealth(Snapshot) && !Snapshot.bIsDead;
}

bool ILSBasicAttackParticipant::CanReceiveBasicAttack_Implementation(AActor* Attacker) const
{
	return ULSCombatTargetLibrary::CanAutoAttackTarget(Attacker, Cast<AActor>(this));
}

float ILSBasicAttackParticipant::ReceiveBasicAttack_Implementation(AActor* Attacker, float Damage)
{
	AActor* Owner = Cast<AActor>(this);
	ULSHealthComponent* Health = IsValid(Owner) ? Owner->FindComponentByClass<ULSHealthComponent>() : nullptr;
	if (!Health)
	{
		return 0.0f;
	}
	// 피해 정책과 실제 감소량은 공통 체력 구현을 따른다.
	const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(Damage, Attacker, Attacker);
	return Health->ApplyDamage(Request).AppliedDamage;
}

bool ILSBasicAttackParticipant::GetCombatHealth_Implementation(float& Current, float& Maximum) const
{
	const AActor* Owner = Cast<AActor>(this);
	const ULSHealthComponent* Health = IsValid(Owner) ? Owner->FindComponentByClass<ULSHealthComponent>() : nullptr;
	FLSHealthSnapshot Snapshot;
	if (!Health || !Health->TryGetHealth(Snapshot))
	{
		return false;
	}
	Current = Snapshot.CurrentHealth;
	Maximum = Snapshot.MaxHealth;
	return true;
}
