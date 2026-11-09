#include "Troops/LSMortarComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSCastleProjectile.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Engine/World.h"

bool ULSMortarComponent::IsConfigurationValid() const
{
	return Super::IsConfigurationValid() && FMath::IsFinite(SetupSeconds) && SetupSeconds > 0 &&
	       FMath::IsFinite(BlastRadius) && BlastRadius > 0 && FMath::IsFinite(ShellSpeed) && ShellSpeed > 0 &&
	       FMath::IsFinite(ArcHeight) && ArcHeight >= 0;
}

FLSCombatDirective ULSMortarComponent::EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents)
{
	FLSCombatDirective Result = Super::EvaluateEngagement(Target, Opponents);
	Result.bSuppressBasicAttack = true;
	ALSTroopUnit* Unit = GetTroop();
	FLSUnitStatRow Stats;
	if (!IsBattleEnabled() || IsStaggered() || !Unit || !Unit->TryGetStats(Stats))
	{
		CancelSpecialAction();
		return Result;
	}
	Result.ApproachRange = Stats.AttackRange;
	if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target) ||
	    ULSCombatTargetLibrary::GetAttackDistance(Unit, Target) > Stats.AttackRange)
	{
		CancelSpecialAction();
		return Result;
	}
	Result.bHoldPosition = true;
	if (!Unit->GetVelocity().IsNearlyZero())
	{
		CancelSpecialAction();
		return Result;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextFireTime)
		return Result;
	if (SetupTarget.Get() != Target)
	{
		SetupTarget = Target;
		SetupCompleteTime = Now + SetupSeconds / FieldAttackSpeed;
		return Result;
	}
	if (Now >= SetupCompleteTime)
	{
		const FVector Facing = (Target->GetActorLocation() - Unit->GetActorLocation()).GetSafeNormal2D();
		if (!Facing.IsNearlyZero())
			Unit->SetActorRotation(Facing.Rotation());
		FVector Aim = Target->GetActorLocation();
		Aim.Z -= Target->GetSimpleCollisionHalfHeight();
		// 재시도에도 간격을 적용해 생성에 실패했을 때 매 판단마다 포탄을 요청하지 않는다.
		NextFireTime = Now + Stats.AttackInterval / FieldAttackSpeed;
		CancelSpecialAction();
		Fire(Aim, Stats);
	}
	return Result;
}

void ULSMortarComponent::CancelSpecialAction()
{
	SetupTarget.Reset();
	SetupCompleteTime = 0;
}

void ULSMortarComponent::ResetForNewLife()
{
	Super::ResetForNewLife();
	CancelSpecialAction();
	NextFireTime = 0;
	FieldAttackSpeed = 1.0f;
}

void ULSMortarComponent::SetFieldAttackSpeed(float Multiplier)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const double Ratio = FieldAttackSpeed / Multiplier;
	NextFireTime = Now + FMath::Max(0.0, NextFireTime - Now) * Ratio;
	SetupCompleteTime = Now + FMath::Max(0.0, SetupCompleteTime - Now) * Ratio;
	FieldAttackSpeed = Multiplier;
}

bool ULSMortarComponent::Fire(const FVector& Aim, const FLSUnitStatRow& Stats)
{
	ALSTroopUnit* Unit = GetTroop();
	FLSCastleWeaponSettings Settings;
	Settings.Damage = Stats.AttackPower;
	Settings.Cooldown = Stats.AttackInterval;
	Settings.Range = Stats.AttackRange;
	Settings.Radius = BlastRadius;
	Settings.ProjectileSpeed = ShellSpeed;
	Settings.ArcHeight = ArcHeight;
	Settings.ShellRadius = 12;
	Settings.KnockbackSpeed = 0;
	Settings.KnockbackLift = 0;
	const FTransform Transform(Unit->GetActorRotation(), Unit->GetActorLocation() + FVector(0, 0, 62));
	auto* Shell = GetWorld()->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(),
	    Transform, Unit, Unit, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Shell)
		return false;
	const auto Request = ULSHealthComponent::MakeDamageRequest(Settings.Damage, Unit, Shell);
	if (!Shell->Launch(ELSCastleWeapon::Cannon, Settings, Request, Aim, true))
	{
		Shell->Destroy();
		return false;
	}
	Shell->FinishSpawning(Transform);
	return true;
}
