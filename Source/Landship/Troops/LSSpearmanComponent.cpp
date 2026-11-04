#include "Troops/LSSpearmanComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Engine/World.h"

#include "Troops/LSCavalryComponent.h"

bool FLSSpearmanBraceSettings::IsValid() const
{
	const float Positive[] = {BraceDistance,   BraceSetupSeconds, FrontalHalfAngle,
	                          GuardMultiplier, CounterMultiplier, RecoilSeconds};
	for (float Value : Positive)
	{
		if (!FMath::IsFinite(Value) || Value <= 0)
		{
			return false;
		}
	}
	return GuardMultiplier < 1 && FrontalHalfAngle < 90;
}
bool ULSSpearmanComponent::IsConfigurationValid() const
{
	return Super::IsConfigurationValid() && GetTroop()->GetTroopType() == ELSTroopType::Spearman && Settings.IsValid();
}
FLSCombatDirective ULSSpearmanComponent::EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents)
{
	FLSCombatDirective Result = Super::EvaluateEngagement(Target, Opponents);
	if (!IsBattleEnabled() || IsStaggered())
	{
		return Result;
	}
	if (!bGuardSpent)
	{
		for (ALSUnit* Opponent : Opponents)
		{
			if (TryBrace(Cast<ALSTroopUnit>(Opponent)))
			{
				break;
			}
		}
	}
	if (IsBracing())
	{
		Result.Target = GetBraceThreat();
		Result.bHoldPosition = true;
	}
	return Result;
}
FLSChargeDefenseResult ULSSpearmanComponent::ResolveChargeDefense(ALSTroopUnit* Attacker)
{
	FLSChargeDefenseResult Result;
	if (IsValid(Attacker) && ConsumeGuard(Attacker))
	{
		Result.bGuarded = true;
		Result.EffectScale = Settings.GuardMultiplier;
		Result.CounterDamage = GetTroop()->BasicAttack->GetAttackSettings().Damage * Settings.CounterMultiplier;
		Result.RecoilSeconds = Settings.RecoilSeconds;
	}
	return Result;
}
void ULSSpearmanComponent::CancelSpecialAction()
{
	StopBracing();
}
FString ULSSpearmanComponent::GetRestingLabel() const
{
	return bGuardSpent ? TEXT("GUARD USED") : TEXT("");
}
void ULSSpearmanComponent::TickSpecialAction(float DeltaTime)
{
	const ULSCavalryComponent* ThreatSkill =
	    BraceThreat.IsValid() ? Cast<ULSCavalryComponent>(BraceThreat->CombatBehavior) : nullptr;
	if (IsBracing() && (!ULSCombatTargetLibrary::CanAutoAttackTarget(GetTroop(), BraceThreat.Get()) || !ThreatSkill ||
	                    !ThreatSkill->IsCharging()))
	{
		StopBracing();
	}
}
AActor* ULSSpearmanComponent::GetBraceThreat() const
{
	return BraceThreat.Get();
}
bool ULSSpearmanComponent::TryBrace(ALSTroopUnit* Threat)
{
	ALSTroopUnit* Unit = GetTroop();
	const ULSCavalryComponent* ThreatSkill =
	    IsValid(Threat) ? Cast<ULSCavalryComponent>(Threat->CombatBehavior) : nullptr;
	if (!IsBattleEnabled() || !Unit || bGuardSpent || IsStaggered() || Unit->GetTroopType() != ELSTroopType::Spearman ||
	    !ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Threat) || !ThreatSkill || !ThreatSkill->IsCharging() ||
	    ULSCombatTargetLibrary::GetAttackDistance(Unit, Threat) > Settings.BraceDistance)
	{
		return false;
	}
	if (IsBracing())
	{
		return true;
	}
	BraceThreat = Threat;
	BraceStarted = GetWorld()->GetTimeSeconds();
	Unit->SetActorRotation((Threat->GetActorLocation() - Unit->GetActorLocation()).GetSafeNormal2D().Rotation());
	Unit->SetTacticalLabel(TEXT("BRACE"));
	RefreshTick();
	return true;
}
void ULSSpearmanComponent::StopBracing()
{
	BraceThreat.Reset();
	if (ALSTroopUnit* Unit = Cast<ALSTroopUnit>(GetOwner()))
	{
		Unit->SetTacticalLabel(bGuardSpent ? TEXT("GUARD USED") : TEXT(""));
	}
}
bool ULSSpearmanComponent::ConsumeGuard(ALSTroopUnit* Attacker)
{
	ALSTroopUnit* Unit = Cast<ALSTroopUnit>(GetOwner());
	if (!IsBattleEnabled() || !Unit || bGuardSpent || IsStaggered() || !IsBracing() ||
	    GetWorld()->GetTimeSeconds() - BraceStarted < Settings.BraceSetupSeconds ||
	    Unit->GetVelocity().SizeSquared2D() > FMath::Square(10.0f))
	{
		return false;
	}
	const FVector Direction = (Attacker->GetActorLocation() - Unit->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(Unit->GetActorForwardVector(), Direction) <
	    FMath::Cos(FMath::DegreesToRadians(Settings.FrontalHalfAngle)))
	{
		return false;
	}
	bGuardSpent = true;
	StopBracing();
	return true;
}
