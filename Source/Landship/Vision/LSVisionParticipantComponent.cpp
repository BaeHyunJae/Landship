#include "Vision/LSVisionParticipantComponent.h"

#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSFactionInterface.h"
#include "Vision/LSVisionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

ULSVisionParticipantComponent::ULSVisionParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULSVisionParticipantComponent::SetSightRadius(float Radius)
{
	if (!ensureMsgf(FMath::IsFinite(Radius) && Radius >= 0.0f,
					TEXT("A finite nonnegative sight radius is required")))
	{
		return;
	}
	SightRadius = Radius;
	if (bRegistered)
	{
		GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
	}
}

bool ULSVisionParticipantComponent::IsFriendly() const
{
	const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(GetOwner());
	return Faction && Faction->GetFaction() == ELSFaction::Ally;
}

bool ULSVisionParticipantComponent::IsLivingCombatant() const
{
	return ULSCombatTargetLibrary::IsCombatAlive(GetOwner());
}

bool ULSVisionParticipantComponent::IsEnemyDiscoveryCandidate() const
{
	const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(GetOwner());
	return bTriggersEnemyDiscovery && Faction && Faction->GetFaction() == ELSFaction::Enemy &&
		   IsLivingCombatant();
}

bool ULSVisionParticipantComponent::ShouldConceal() const
{
	return bConcealedOutsideSight && !IsFriendly();
}

void ULSVisionParticipantComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	bOriginalHidden = Owner->IsHidden();
	if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
	{
		if (Vision->IsVisionEnabled() && ShouldConceal())
		{
			ApplySightVisibility(false);
		}
		bRegistered = true;
		Vision->RegisterParticipant(this);
	}
}

void ULSVisionParticipantComponent::ApplySightVisibility(bool bVisible)
{
	AActor* Owner = GetOwner();
	if (!ShouldConceal() || !IsValid(Owner) || Owner->IsActorBeingDestroyed())
	{
		return;
	}
	if (bVisible)
	{
		if (!bHiddenBySight)
		{
			return;
		}
		Owner->SetActorHiddenInGame(bOriginalHidden);
		for (const auto& Entry : ChildOriginalHidden)
		{
			if (Entry.Key.IsValid() && !Entry.Key->IsActorBeingDestroyed())
			{
				Entry.Key->SetActorHiddenInGame(Entry.Value);
			}
		}
		ChildOriginalHidden.Reset();
		bHiddenBySight = false;
		return;
	}
	if (!bHiddenBySight)
	{
		bOriginalHidden = Owner->IsHidden();
		bHiddenBySight = true;
	}
	Owner->SetActorHiddenInGame(true);
	TArray<AActor*> Children;
	GetAttachedVisualActors(Children);
	for (AActor* Child : Children)
	{
		if (!IsValid(Child) || Child->IsActorBeingDestroyed())
		{
			continue;
		}
		const TWeakObjectPtr<AActor> Key(Child);
		if (!ChildOriginalHidden.Contains(Key))
		{
			ChildOriginalHidden.Add(Key, Child->IsHidden());
		}
		Child->SetActorHiddenInGame(true);
	}
}

void ULSVisionParticipantComponent::GetAttachedVisualActors(TArray<AActor*>& OutActors) const
{
	GetOwner()->GetAllChildActors(OutActors, true);
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached, true, true);
	for (AActor* Actor : Attached)
	{
		OutActors.AddUnique(Actor);
	}
}

void ULSVisionParticipantComponent::NotifyCombatDeath()
{
	if (bRegistered)
	{
		if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
		{
			Vision->RequestRefresh();
		}
	}
}

void ULSVisionParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered && GetWorld())
	{
		if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
		{
			bRegistered = false;
			Vision->UnregisterParticipant(this);
		}
	}
	if (bHiddenBySight && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed())
	{
		GetOwner()->SetActorHiddenInGame(bOriginalHidden);
	}
	for (const auto& Entry : ChildOriginalHidden)
	{
		if (Entry.Key.IsValid() && !Entry.Key->IsActorBeingDestroyed())
		{
			Entry.Key->SetActorHiddenInGame(Entry.Value);
		}
	}
	ChildOriginalHidden.Reset();
	Super::EndPlay(EndPlayReason);
}
