#include "Combat/LSBasicAttackComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Combat/LSBasicProjectile.h"

#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Combat/LSBasicAttackTuningSubsystem.h"
#include "Obstacles/LSPathObstacle.h"

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
	float Interval = 0;
	if (!ResolveEffectiveInterval(NewSettings.Interval, Interval))
	{
		if ((GetWorld() && GetWorld()->HasBegunPlay()) || HasBegunPlay())
		{
			UE_LOG(LogTemp, Warning,
			       TEXT("Basic attack configuration rejected: unavailable tuning or invalid effective interval: %s"),
			       *GetPathName());
			return false;
		}
		// 플레이 시작 전에는 원본만 준비한다. 월드 배율이 확정된 BeginPlay에서 실행 가능 여부를 정한다.
		Settings = NewSettings;
		bHasPendingSettings = true;
		bConfigured = false;
		EffectiveInterval = 0;
		return true;
	}
	Settings = NewSettings;
	EffectiveInterval = Interval;
	bConfigured = true;
	bHasPendingSettings = false;
	if (!bBattleActionsEnded && HasBegunPlay() && GetWorld()->GetTimerManager().IsTimerActive(AttackTimer))
	{
		const float Period = FMath::IsFinite(CheckInterval) ? FMath::Max(0.01f, CheckInterval) : 0.05f;
		// 같은 검사 주기로 재설정할 때 다음 검사 시각까지 밀어내지 않는다.
		if (GetWorld()->GetTimerManager().GetTimerRate(AttackTimer) != FMath::Min(Period, EffectiveInterval))
			StartAttackTimer();
	}
	return true;
}

bool ULSBasicAttackComponent::ResolveEffectiveInterval(float BaseInterval, float& OutInterval) const
{
	const ULSBasicAttackTuningSubsystem* Tuning =
	    GetWorld() ? GetWorld()->GetSubsystem<ULSBasicAttackTuningSubsystem>() : nullptr;
	float Multiplier = 0;
	if (!Tuning || !Tuning->TryGetBasicAttackSpeedMultiplier(Multiplier))
		return false;
	const float Interval = BaseInterval / Multiplier;
	if (!FMath::IsFinite(Interval) || Interval <= 0)
		return false;
	OutInterval = Interval;
	return true;
}

void ULSBasicAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bHasPendingSettings)
	{
		bHasPendingSettings = false;
		bConfigured = ResolveEffectiveInterval(Settings.Interval, EffectiveInterval);
		if (!bConfigured)
			UE_LOG(LogTemp, Warning,
			       TEXT("Basic attack initialization rejected: unavailable tuning or invalid effective interval: %s"),
			       *GetPathName());
	}
	if (!bBattleActionsEnded && bConfigured && bAutoAttackOnBeginPlay)
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
	return ULSCombatTargetLibrary::CanAutoAttackTarget(Owner, Target) &&
	       ULSVisionSubsystem::PassesFriendlySight(Owner, Target);
}

bool ULSBasicAttackComponent::StartAttack(AActor* Target)
{
	if (bBattleActionsEnded || bResolvingAttack || !HasBegunPlay() || !IsActive() || !bConfigured || !IsValidCombatPair(Target))
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
	if (bBattleActionsEnded)
	{
		StopAttack();
		return;
	}
	const float Period = FMath::IsFinite(CheckInterval) ? FMath::Max(0.01f, CheckInterval) : 0.05f;
	FTimerManagerTimerParameters Parameters;
	Parameters.bLoop = true;
	Parameters.bMaxOncePerFrame = true;
	GetWorld()->GetTimerManager().SetTimer(AttackTimer, this, &ULSBasicAttackComponent::UpdateAttack,
	                                       FMath::Min(Period, EffectiveInterval), Parameters);
}

bool ULSBasicAttackComponent::StartAutoAttack()
{
	if (bBattleActionsEnded || bResolvingAttack || !HasBegunPlay() || !IsActive() || !bConfigured)
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
	float SourceRadius, SourceHeight;
	GetOwner()->GetSimpleCollisionCylinder(SourceRadius, SourceHeight);
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(
	    Hits, GetOwner()->GetActorLocation(), FQuat::Identity, Objects,
	    FCollisionShape::MakeBox(FVector(Settings.Range + SourceRadius + 1, Settings.Range + SourceRadius + 1,
	                                     Settings.Range + SourceHeight + 1)),
	    Query);
	AActor* Nearest = nullptr;
	double NearestDistance = Settings.Range;
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(Candidate);
			Obstacle && !Obstacle->CanBeAutomaticallyAcquired())
			continue;
		if (!IsValidCombatPair(Candidate))
		{
			continue;
		}
		const double Distance = ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Candidate);
		if (Distance < NearestDistance ||
		    (Distance == NearestDistance && (!Nearest || Candidate->GetUniqueID() < Nearest->GetUniqueID())))
		{
			Nearest = Candidate;
			NearestDistance = Distance;
		}
	}
	return Nearest;
}

void ULSBasicAttackComponent::SetAttackTargetResolver(FLSResolveAttackTarget ResolverEvent)
{
	AttackTargetResolverEvent = MoveTemp(ResolverEvent);
}

void ULSBasicAttackComponent::UpdateAttack()
{
	if (bBattleActionsEnded)
	{
		StopAttack();
		return;
	}
	if (bResolvingAttack)
	{
		return;
	}
	if (!AttackTargetResolverEvent.IsBound() && bAutoSearching && IsActive() && IsValid(GetOwner()) &&
	    !GetOwner()->IsActorBeingDestroyed() && ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
	{
		// 현재 교전이 유효하면 더 가까운 적이 나타나도 타격 대상을 유지한다.
		if (!IsValidCombatPair(AttackTarget.Get()) ||
		    ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), AttackTarget.Get()) > Settings.Range)
		{
			AttackTarget = FindNearestTarget();
		}
		if (!AttackTarget.IsValid())
		{
			return;
		}
	}
	if (!IsActive() || !bConfigured || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
	{
		StopAttack();
		return;
	}
	TGuardValue<bool> ResolvingGuard(bResolvingAttack, true);
	if (AttackTargetResolverEvent.IsBound())
	{
		if (GetCooldownRemaining() > 0.0f)
		{
			return;
		}
		const uint64 Revision = StopRevision;
		// 콜백이 자기 바인딩을 바꾸어도 실행 중인 델리게이트는 유지한다.
		const FLSResolveAttackTarget ResolverEvent = AttackTargetResolverEvent;
		AActor* ResolvedTarget = ResolverEvent.Execute();
		if (!IsValid(this) || Revision != StopRevision || !IsActive() || !GetWorld() || GetWorld()->bIsTearingDown ||
		    !IsValidCombatPair(ResolvedTarget))
		{
			return;
		}
		AttackTarget = ResolvedTarget;
	}
	AActor* Target = AttackTarget.Get();
	if (!IsValidCombatPair(Target))
	{
		StopAttack();
		return;
	}
	if (GetCooldownRemaining() > 0.0f || ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Target) > Settings.Range)
	{
		return;
	}
	NextAttackTime = GetWorld()->GetTimeSeconds() + EffectiveInterval;
	FLSAttackExecution Execution;
	Execution.Source = GetOwner();
	Execution.Target = Target;
	Execution.SourceLocation = GetOwner()->GetActorLocation();
	Execution.TargetLocation = Target->GetActorLocation();
	Execution.bProjectile = ProjectileSettings.ProjectileClass != nullptr;
	if (Execution.bProjectile)
	{
		FireProjectile(Target, Execution);
		return;
	}
	ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
	if (!Health)
	{
		StopAttack();
		return;
	}
	Execution.ExecutionId = ++LastExecutionId;
	OnAttackExecutedEvent.Broadcast(Execution);
	// 알림에서 액터를 제거할 수 있으므로 확정된 시도라도 피해 적용 전 다시 검증한다.
	if (!IsValid(this) || !GetWorld() || GetWorld()->bIsTearingDown || !IsValidCombatPair(Target) || !IsValid(Health))
	{
		return;
	}
	const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(Settings.Damage, GetOwner(), GetOwner());
	const float AppliedDamage = Health->ApplyDamage(Request).AppliedDamage;
	if (IsValid(this) && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetWorld() &&
	    !GetWorld()->bIsTearingDown && FMath::IsFinite(AppliedDamage) && AppliedDamage > 0.0f)
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
	++StopRevision;
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
	AttackTargetResolverEvent.Unbind();
	Super::EndPlay(EndPlayReason);
}

void ULSBasicAttackComponent::EndBattleActions()
{
	bBattleActionsEnded = true;
	StopAttack();
}

void ULSBasicAttackComponent::Deactivate()
{
	StopAttack();
	Super::Deactivate();
}

bool ULSBasicAttackComponent::ConfigureProjectile(const FLSProjectileAttackSettings& NewSettings)
{
	if (bResolvingAttack || !FMath::IsFinite(NewSettings.Speed) || NewSettings.Speed <= 0 ||
	    !FMath::IsFinite(NewSettings.Lifetime) || NewSettings.Lifetime <= 0 || !FMath::IsFinite(NewSettings.Radius) ||
	    NewSettings.Radius <= 0 ||
	    (NewSettings.ProjectileClass &&
	     NewSettings.ProjectileClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)))
	{
		return false;
	}
	ProjectileSettings = NewSettings;
	return true;
}

void ULSBasicAttackComponent::FireProjectile(AActor* Target, const FLSAttackExecution& Execution)
{
	if (!GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	const FVector Origin = GetOwner()->GetActorLocation();
	const FVector Direction = Target->GetActorLocation() - Origin;
	const FTransform Transform(Direction.Rotation(), Origin);
	// 발사 후 공격자가 사망해도 진영과 피해량은 바뀌지 않는다.
	const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(Settings.Damage, GetOwner(), nullptr);
	ALSBasicProjectile* Projectile =
	    GetWorld()->SpawnActorDeferred<ALSBasicProjectile>(ProjectileSettings.ProjectileClass, Transform, nullptr,
	                                                       nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Projectile))
	{
		return;
	}
	if (!Projectile->Launch(Request, Target, Direction, ProjectileSettings.Speed, ProjectileSettings.Radius,
	                        ProjectileSettings.Lifetime, ProjectileSettings.Material))
	{
		Projectile->Destroy();
		return;
	}
	Projectile->OnDamageApplied.AddDynamic(this, &ULSBasicAttackComponent::OnProjectileDamage);
	Projectile->FinishSpawning(Transform);
	if (IsValid(this) && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetWorld() &&
	    !GetWorld()->bIsTearingDown && IsValid(Projectile))
	{
		FLSAttackExecution LaunchedExecution = Execution;
		LaunchedExecution.ExecutionId = ++LastExecutionId;
		OnAttackExecutedEvent.Broadcast(LaunchedExecution);
		if (IsValid(this) && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetWorld() &&
		    !GetWorld()->bIsTearingDown && IsValid(Projectile))
		{
			OnProjectileLaunched.Broadcast(Projectile);
		}
	}
}

void ULSBasicAttackComponent::OnProjectileDamage(AActor* Target, float AppliedDamage)
{
	if (GetWorld() && !GetWorld()->bIsTearingDown && ULSCombatTargetLibrary::IsCombatAlive(GetOwner()) &&
	    AppliedDamage > 0)
	{
		OnAttackPerformed.Broadcast(Target, AppliedDamage);
	}
}
