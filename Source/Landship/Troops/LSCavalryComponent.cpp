#include "Troops/LSCavalryComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

bool FLSCavalryChargeSettings::IsValid() const
{
	const float Positive[] = {StartDistance,    RunUpDistance,  SpeedMultiplier, ImpactReach,
	                          DamageMultiplier, StaggerSeconds, KnockbackSpeed,  TimeoutSeconds};
	for (float Value : Positive)
	{
		if (!FMath::IsFinite(Value) || Value <= 0)
		{
			return false;
		}
	}
	return SpeedMultiplier > 1 && StartDistance > RunUpDistance;
}

bool ULSCavalryComponent::IsConfigurationValid() const
{
	return Super::IsConfigurationValid() && GetTroop()->GetTroopType() == ELSTroopType::Cavalry && Settings.IsValid();
}

FLSCombatDirective ULSCavalryComponent::EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents)
{
	FLSCombatDirective Result = Super::EvaluateEngagement(Target, Opponents);
	if (IsBattleEnabled() && !IsStaggered())
	{
		TryStartCharge(Target);
		if (IsCharging())
		{
			Result.ApproachRange = Settings.ImpactReach * 0.25;
			Result.bSuppressBasicAttack = true;
		}
	}
	return Result;
}

void ULSCavalryComponent::CancelSpecialAction()
{
	StopCharge();
}

void ULSCavalryComponent::ResetForNewLife()
{
	Super::ResetForNewLife();
	ChargeTarget.Reset();
	PreviousLocation = FVector::ZeroVector;
	ChargeDirection = FVector::ZeroVector;
	ChargeStarted = 0;
	OriginalSpeed = 0;
	RunUp = 0;
	ImpactCount = 0;
	bChargeSpent = false;
	bLastImpactGuarded = false;
}
bool ULSCavalryComponent::TryStartCharge(AActor* Target)
{
	ALSTroopUnit* Unit = Cast<ALSTroopUnit>(GetOwner());
	if (!IsBattleEnabled() || !Unit || Unit->GetTroopType() != ELSTroopType::Cavalry || bChargeSpent || bCharging ||
	    IsStaggered() || Unit->GetCharacterMovement()->IsFalling() ||
	    !ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target) ||
	    ULSCombatTargetLibrary::GetAttackDistance(Unit, Target) < Settings.StartDistance)
	{
		return false;
	}
	OriginalSpeed = Unit->GetCharacterMovement()->MaxWalkSpeed;
	if (!FMath::IsFinite(OriginalSpeed) || OriginalSpeed <= 0)
	{
		return false;
	}
	ChargeTarget = Target;
	ChargeDirection = (Target->GetActorLocation() - Unit->GetActorLocation()).GetSafeNormal2D();
	PreviousLocation = Unit->GetActorLocation();
	ChargeStarted = GetWorld()->GetTimeSeconds();
	RunUp = 0;
	bCharging = true;
	Unit->BasicAttack->StopAttack();
	Unit->GetCharacterMovement()->MaxWalkSpeed = OriginalSpeed * Settings.SpeedMultiplier;
	Unit->SetTacticalLabel(TEXT("CHARGE"));
	Unit->SetLanceLowered(true);
	RefreshTick();
	return true;
}
void ULSCavalryComponent::StopCharge()
{
	if (bCharging)
	{
		if (ALSTroopUnit* Unit = Cast<ALSTroopUnit>(GetOwner()))
		{
			Unit->GetCharacterMovement()->MaxWalkSpeed = OriginalSpeed;
			Unit->SetLanceLowered(false);
			Unit->SetTacticalLabel(TEXT(""));
		}
	}
	bCharging = false;
	ChargeTarget.Reset();
}
void ULSCavalryComponent::Impact(AActor* Target)
{
	ALSTroopUnit* Unit = Cast<ALSTroopUnit>(GetOwner());
	ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
	if (!Unit || !Health)
	{
		return;
	}
	ALSTroopUnit* Defender = Cast<ALSTroopUnit>(Target);
	const FLSChargeDefenseResult Defense = Defender && Defender->CombatBehavior
	                                           ? Defender->CombatBehavior->ResolveChargeDefense(Unit)
	                                           : FLSChargeDefenseResult();
	bLastImpactGuarded = Defense.bGuarded;
	const float Reduction = Defense.EffectScale;
	const float Damage = Unit->BasicAttack->GetAttackSettings().Damage * Settings.DamageMultiplier * Reduction;
	const float Counter = Defense.CounterDamage;
	const float Recoil = Defense.RecoilSeconds;
	const FVector Direction = ChargeDirection;
	// 피해 알림에서 액터가 제거되거나 재진입해도 돌격을 두 번 확정하지 않는다.
	bChargeSpent = true;
	++ImpactCount;
	StopCharge();
	Unit->BasicAttack->DelayNextAttack(Unit->BasicAttack->GetEffectiveAttackInterval());
	const TWeakObjectPtr<ALSTroopUnit> Source(Unit);
	const TWeakObjectPtr<ALSTroopUnit> Receiver(Defender);
	const FLSDamageRequest CounterRequest = ULSHealthComponent::MakeDamageRequest(Counter, Defender, Defender);
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(Damage, Unit, Unit));
	if (!IsValid(this) || !Source.IsValid() || !IsBattleEnabled())
	{
		return;
	}
	if (Receiver.IsValid() && ULSCombatTargetLibrary::IsCombatAlive(Receiver.Get()) && Receiver->CombatBehavior)
	{
		Receiver->CombatBehavior->ApplyStagger(Settings.StaggerSeconds * Reduction,
		                                       Direction * Settings.KnockbackSpeed * Reduction);
	}
	if (Counter > 0 && ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		Unit->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(CounterRequest);
		if (!IsValid(this) || !Source.IsValid() || !IsBattleEnabled())
		{
			return;
		}
		ApplyStagger(Recoil, FVector::ZeroVector);
	}
	RefreshTick();
}
void ULSCavalryComponent::TickSpecialAction(float DeltaTime)
{
	ALSTroopUnit* Unit = GetTroop();
	if (bCharging)
	{
		const FVector Here = Unit->GetActorLocation();
		const FVector Step = Here - PreviousLocation;
		// 텔레포트·급회전은 도움닫기로 세지 않는다.
		if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, ChargeTarget.Get()) ||
		    Unit->GetCharacterMovement()->IsFalling() ||
		    GetWorld()->GetTimeSeconds() - ChargeStarted > Settings.TimeoutSeconds ||
		    (Step.Size2D() > OriginalSpeed * Settings.SpeedMultiplier * DeltaTime * 2 + 10) ||
		    (Step.Size2D() > 1 && FVector::DotProduct(Step.GetSafeNormal2D(), ChargeDirection) < 0.7))
		{
			StopCharge();
		}
		else
		{
			RunUp += Step.Size2D();
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
			Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(LSLanceCharge), false, Unit);
			TArray<FHitResult> Hits;
			float Radius, Height;
			Unit->GetSimpleCollisionCylinder(Radius, Height);
			bool bIgnored;
			do
			{
				Hits.Reset();
				bIgnored = false;
				GetWorld()->SweepMultiByObjectType(Hits, PreviousLocation,
				                                   Here + ChargeDirection * Settings.ImpactReach, FQuat::Identity,
				                                   Objects, FCollisionShape::MakeSphere(Radius), Query);
				for (const FHitResult& Hit : Hits)
				{
					if (IsValid(Hit.GetActor()) && !ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Hit.GetActor()))
					{
						Query.AddIgnoredActor(Hit.GetActor());
						bIgnored = true;
					}
				}
			} while (bIgnored);
			Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
			for (const FHitResult& Hit : Hits)
			{
				if (ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Hit.GetActor()))
				{
					if (RunUp >= Settings.RunUpDistance && Unit->GetVelocity().Size2D() >= OriginalSpeed)
					{
						Impact(Hit.GetActor());
					}
					else
					{
						StopCharge();
					}
					break;
				}
			}
			PreviousLocation = Here;
		}
	}
}
