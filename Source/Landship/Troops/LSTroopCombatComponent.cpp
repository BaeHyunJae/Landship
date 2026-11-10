#include "Troops/LSTroopCombatComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Controls/LSOrderComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

ULSTroopCombatComponent::ULSTroopCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

ALSTroopUnit* ULSTroopCombatComponent::GetTroop() const
{
	return Cast<ALSTroopUnit>(GetOwner());
}

bool ULSTroopCombatComponent::IsConfigurationValid() const
{
	return IsValid(GetTroop());
}

float ULSTroopCombatComponent::GetEngagementSearchRadius() const
{
	FLSUnitStatRow Stats;
	const auto* Troop = GetTroop();
	return Troop && Troop->TryGetStats(Stats) ? Stats.VisionRadius : 0.0f;
}

void ULSTroopCombatComponent::SetBattleEnabled(bool bEnabled)
{
	bBattleEnabled = bEnabled && IsConfigurationValid();
	if (!bBattleEnabled)
	{
		CancelSpecialAction();
		StaggerUntil = 0;
		if (ALSTroopUnit* Unit = GetTroop())
		{
			Unit->SetTacticalLabel(GetRestingLabel());
		}
	}
	RefreshTick();
}

bool ULSTroopCombatComponent::IsStaggered() const
{
	return bBattleEnabled && GetWorld() && GetWorld()->GetTimeSeconds() < StaggerUntil;
}

void ULSTroopCombatComponent::ResetForNewLife()
{
	SetBattleEnabled(false);
}

void ULSTroopCombatComponent::RefreshTick()
{
	SetComponentTickEnabled(bBattleEnabled && (HasSpecialAction() || IsStaggered()));
}
void ULSTroopCombatComponent::ApplyStagger(float Seconds, const FVector& Velocity)
{
	ALSTroopUnit* Unit = Cast<ALSTroopUnit>(GetOwner());
	if (!bBattleEnabled || !ULSCombatTargetLibrary::IsCombatAlive(Unit) || !FMath::IsFinite(Seconds) || Seconds <= 0 ||
	    Velocity.ContainsNaN())
	{
		return;
	}
	CancelSpecialAction();
	StaggerUntil = FMath::Max(StaggerUntil, GetWorld()->GetTimeSeconds() + Seconds);
	Unit->BasicAttack->StopAttack();
	Unit->BasicAttack->DelayNextAttack(Seconds);
	if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
	{
		AI->StopMovement();
	}
	Unit->GetCharacterMovement()->StopMovementImmediately();
	if (!Velocity.IsNearlyZero())
	{
		Unit->Orders->ApplyKnockback(Velocity);
	}
	Unit->SetTacticalLabel(TEXT("STAGGER"));
	RefreshTick();
}

FLSCombatDirective ULSTroopCombatComponent::EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents)
{
	FLSCombatDirective Result;
	Result.Target = Target;
	if (const ALSTroopUnit* Unit = GetTroop())
	{
		Result.ApproachRange = Unit->BasicAttack->GetAttackSettings().Range;
	}
	Result.bHoldPosition = IsStaggered();
	Result.bSuppressBasicAttack = IsStaggered();
	return Result;
}

FLSChargeDefenseResult ULSTroopCombatComponent::ResolveChargeDefense(ALSTroopUnit* Attacker)
{
	return {};
}

void ULSTroopCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	ALSTroopUnit* Unit = GetTroop();
	if (!bBattleEnabled || !ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		SetBattleEnabled(false);
		return;
	}
	TickSpecialAction(DeltaTime);
	// 병종 행동의 피해 콜백에서 소유자가 파괴될 수 있다.
	if (!IsValid(this) || !IsValid(Unit))
	{
		return;
	}
	if (!IsStaggered() && !HasSpecialAction())
	{
		Unit->SetTacticalLabel(GetRestingLabel());
	}
	RefreshTick();
}

void ULSTroopCombatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetBattleEnabled(false);
	Super::EndPlay(Reason);
}
