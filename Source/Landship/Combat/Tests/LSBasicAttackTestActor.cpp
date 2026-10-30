#include "Combat/Tests/LSBasicAttackTestActor.h"

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
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
	HealthComponent = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
}

void ALSBasicAttackTestActor::SetInitialFaction(ELSFaction InFaction)
{
	if (!ensureAlwaysMsgf(!bFactionLocked, TEXT("Test faction is locked")))
	{
		return;
	}
	Faction = InFaction;
}

void ALSBasicAttackTestActor::BeginPlay()
{
	bFactionLocked = true;
	HealthComponent->InitializeHealth(
	    ELSCombatStatsInitResult::Success, 100.0f,
	    FLSPrepareCombatDeath::CreateUObject(this, &ALSBasicAttackTestActor::PrepareForDeath), FString());
	HealthComponent->OnDamagedEvent.AddDynamic(this, &ALSBasicAttackTestActor::ObserveDamage);
	HealthComponent->OnDiedEvent.AddDynamic(this, &ALSBasicAttackTestActor::ObserveDamage);
	Super::BeginPlay();
}

float ALSBasicAttackTestActor::GetCurrentHealth() const
{
	FLSHealthSnapshot Snapshot;
	return HealthComponent->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : 0.0f;
}

FLSApplyDamageResult ALSBasicAttackTestActor::ApplyTestDamage(float Amount, AActor* Source)
{
	return HealthComponent->ApplyDamage(ULSHealthComponent::MakeDamageRequest(Amount, Source, Source));
}

void ALSBasicAttackTestActor::PrepareForDeath()
{
	Attack->StopAttack();
}

void ALSBasicAttackTestActor::ObserveDamage(const FLSDamageResult& Damage)
{
	++HitsReceived;
}

void ALSBasicAttackTestActor::ObserveAttack(AActor* Target, float AppliedDamage)
{
	++Notifications;
	if (bReenterOnNotification)
	{
		bReentrantAccepted |= Attack->StartAttack(Target);
	}
}
