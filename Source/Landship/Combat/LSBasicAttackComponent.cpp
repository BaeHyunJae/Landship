#include "Combat/LSBasicAttackComponent.h"

#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

ULSBasicAttackComponent::ULSBasicAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

bool ULSBasicAttackComponent::ConfigureAttack(const FLSBasicAttackSettings& NewSettings)
{
	if (bResolvingAttack || !FMath::IsFinite(NewSettings.Damage) || NewSettings.Damage < 0.0f ||
	    !FMath::IsFinite(NewSettings.Interval) || NewSettings.Interval <= 0.0f || !FMath::IsFinite(NewSettings.Range) ||
	    NewSettings.Range < 0.0f)
	{
		return false;
	}
	Settings = NewSettings;
	bConfigured = true;
	return true;
}

void ULSBasicAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bConfigured && bAutoAttackOnBeginPlay)
	{
		// 다른 진영·체력 컴포넌트의 BeginPlay가 끝난 뒤 처음 판정한다.
		bAutoSearching = true;
		StartAttackTimer();
	}
}

bool ULSBasicAttackComponent::IsValidCombatPair(AActor* Target) const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || !IsValid(Target) || Target->IsActorBeingDestroyed() ||
	    Target == Owner || Target->GetWorld() != GetWorld())
	{
		return false;
	}
	return ULSCombatTargetLibrary::CanAutoAttackTarget(Owner, Target);
}

bool ULSBasicAttackComponent::StartAttack(AActor* Target)
{
	if (bResolvingAttack || !HasBegunPlay() || !IsActive() || !bConfigured || !IsValidCombatPair(Target))
	{
		return false;
	}
	AttackTarget = Target;
	bAutoSearching = false;
	StartAttackTimer();
	UpdateAttack();
	return true;
}

void ULSBasicAttackComponent::StartAttackTimer()
{
	const float Period = FMath::IsFinite(CheckInterval) ? FMath::Max(0.01f, CheckInterval) : 0.05f;
	GetWorld()->GetTimerManager().SetTimer(AttackTimer, this, &ULSBasicAttackComponent::UpdateAttack,
	                                       FMath::Max(0.01f, FMath::Min(Period, Settings.Interval)), true);
}

bool ULSBasicAttackComponent::StartAutoAttack()
{
	if (bResolvingAttack || !HasBegunPlay() || !IsActive() || !bConfigured)
	{
		return false;
	}
	bAutoSearching = true;
	AttackTarget.Reset();
	StartAttackTimer();
	UpdateAttack();
	return true;
}

AActor* ULSBasicAttackComponent::FindNearestTarget() const
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBasicAttack), false, GetOwner());
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, GetOwner()->GetActorLocation(), FQuat::Identity, Objects,
	                                     FCollisionShape::MakeSphere(FMath::Max(Settings.Range, 0.01f)), Query);
	AActor* Nearest = nullptr;
	double NearestDistance = FMath::Square(static_cast<double>(Settings.Range));
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!IsValidCombatPair(Candidate))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance < NearestDistance ||
		    (Distance == NearestDistance && (!Nearest || Candidate->GetUniqueID() < Nearest->GetUniqueID())))
		{
			Nearest = Candidate;
			NearestDistance = Distance;
		}
	}
	return Nearest;
}

void ULSBasicAttackComponent::UpdateAttack()
{
	if (bAutoSearching && IsActive() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
	    ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
	{
		// 현재 교전이 유효하면 더 가까운 적이 나타나도 타격 대상을 유지한다.
		if (!IsValidCombatPair(AttackTarget.Get()) ||
		    FVector::DistSquared(GetOwner()->GetActorLocation(), AttackTarget->GetActorLocation()) >
		        FMath::Square(static_cast<double>(Settings.Range)))
		{
			AttackTarget = FindNearestTarget();
		}
		if (!AttackTarget.IsValid())
		{
			return;
		}
	}
	AActor* Target = AttackTarget.Get();
	if (!IsActive() || !bConfigured || !IsValidCombatPair(Target))
	{
		StopAttack();
		return;
	}
	if (bResolvingAttack || GetCooldownRemaining() > 0.0f ||
	    FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()) >
	        FMath::Square(static_cast<double>(Settings.Range)))
	{
		return;
	}
	// 피해 처리나 알림에서 다시 명령을 내려도 같은 시점에 중복 공격할 수 없게 한다.
	TGuardValue<bool> ResolvingGuard(bResolvingAttack, true);
	NextAttackTime = GetWorld()->GetTimeSeconds() + Settings.Interval;
	ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
	if (!Health)
	{
		StopAttack();
		return;
	}
	const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(Settings.Damage, GetOwner(), GetOwner());
	const float AppliedDamage = Health->ApplyDamage(Request).AppliedDamage;
	if (IsValid(this) && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
	    FMath::IsFinite(AppliedDamage) && AppliedDamage > 0.0f)
	{
		OnAttackPerformed.Broadcast(Target, AppliedDamage);
	}
}

float ULSBasicAttackComponent::GetCooldownRemaining() const
{
	return GetWorld() ? static_cast<float>(FMath::Max(0.0, NextAttackTime - GetWorld()->GetTimeSeconds())) : 0.0f;
}

void ULSBasicAttackComponent::StopAttack()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AttackTimer);
	}
	AttackTarget.Reset();
	bAutoSearching = false;
}

void ULSBasicAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAttack();
	Super::EndPlay(EndPlayReason);
}

void ULSBasicAttackComponent::Deactivate()
{
	StopAttack();
	Super::Deactivate();
}
