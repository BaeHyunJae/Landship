#include "Combat/Tests/LSBasicAttackTestActor.h"

#include "Combat/LSBasicAttackComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Combat/LSCollisionChannels.h"

ALSBasicAttackTestActor::ALSBasicAttackTestActor()
{
	USphereComponent* Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Root"));
	RootComponent = Collision;
	Collision->SetSphereRadius(10.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(LSCollisionChannels::Unit);
	Collision->SetCollisionResponseToAllChannels(ECR_Overlap);
	Attack = CreateDefaultSubobject<ULSBasicAttackComponent>(TEXT("Attack"));
}

bool ALSBasicAttackTestActor::IsCombatAlive_Implementation() const
{
	return Health > 0.0f;
}

bool ALSBasicAttackTestActor::CanReceiveBasicAttack_Implementation(AActor* Attacker) const
{
	const ALSBasicAttackTestActor* Source = Cast<ALSBasicAttackTestActor>(Attacker);
	return Source && Source->Team != Team && !bImmune;
}

float ALSBasicAttackTestActor::ReceiveBasicAttack_Implementation(AActor* Attacker, float Damage)
{
	if (!IsCombatAlive_Implementation() || !CanReceiveBasicAttack_Implementation(Attacker))
	{
		return 0.0f;
	}
	const float Applied = FMath::Min(Health, Damage);
	Health -= Applied;
	++HitsReceived;
	return Applied;
}

void ALSBasicAttackTestActor::ObserveAttack(AActor* Target, float AppliedDamage)
{
	++Notifications;
	if (bReenterOnNotification)
	{
		bReentrantAccepted |= Attack->StartAttack(Target);
	}
}
