#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSOutpost.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSEnemyDecisionPolicy.h"
#include "Maps/LSBattlefieldEncounterCoordinator.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSCollisionChannels.h"
#include "Controls/LSOrderComponent.h"
#include "Troops/LSUnit.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Logging/StructuredLog.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

ULSEnemyBehaviorComponent::ULSEnemyBehaviorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

FString ULSEnemyBehaviorComponent::GetConfigurationError(bool bCheckPlacement, const FVector& Placement) const
{
	const struct { const TCHAR* Name; float Value; float Minimum; } Values[] = {
		{TEXT("DecisionInterval"), DecisionInterval, 0.01f},
		{TEXT("SupportAcceptanceRadius"), SupportAcceptanceRadius, 0},
		{TEXT("ChaseRadius"), ChaseRadius, 0},
		{TEXT("RecoveryAdvanceDistance"), RecoveryAdvanceDistance, 1},
		{TEXT("BoundaryAllowance"), BoundaryAllowance, 0},
		{TEXT("ReturnAreaRadius"), ReturnAreaRadius, 1},
		{TEXT("PendingSupportLifetime"), PendingSupportLifetime, 0.01f},
		{TEXT("ArrivalTolerance"), ArrivalTolerance, 1},
		{TEXT("RepathDistance"), RepathDistance, 1},
		{TEXT("FailedPathRetryInterval"), FailedPathRetryInterval, 0.01f},
		{TEXT("MovementTimeout"), MovementTimeout, 0.1f}
	};
	for (const auto& Value : Values)
	{
		if (!FMath::IsFinite(Value.Value) || Value.Value < Value.Minimum)
		{
			return FString::Printf(TEXT("%s must be finite and at least %g"), Value.Name, Value.Minimum);
		}
	}
	if (Role != ELSEnemyRole::Guard && Role != ELSEnemyRole::Assault) { return TEXT("Invalid enemy role"); }
	if (bStartRegionallyDormant && Role != ELSEnemyRole::Assault) { return TEXT("Regional dormancy requires Assault role"); }
	if (GuardAnchor && (Role != ELSEnemyRole::Guard || GuardedOutpost || GuardAnchor->FindComponentByClass<ULSHealthComponent>() ||
		GuardAnchor->GetWorld() != GetWorld() || !FMath::IsFinite(GuardAnchorRadius) || GuardAnchorRadius <= 0))
	{
		return TEXT("Guard anchor requires Guard role, no guarded outpost, same-world noncombat actor and positive radius");
	}
	if (bCheckPlacement && !IsStationary() && Role == ELSEnemyRole::Guard && GuardAnchor)
	{
		if (Placement.ContainsNaN() || GuardAnchor->GetActorLocation().ContainsNaN() ||
			FVector::Dist2D(Placement, GuardAnchor->GetActorLocation()) > GuardAnchorRadius)
			return TEXT("Guard initial placement must be inside its anchor radius");
	}
	if (bCheckPlacement && !IsStationary() && Role == ELSEnemyRole::Guard && !GuardAnchor)
	{
		if (!IsValid(GuardedOutpost) || GuardedOutpost->GetWorld() != GetWorld())
		{
			return TEXT("Guard requires an explicit same-world outpost");
		}
		const float Radius = GuardedOutpost->GetDefenseRadius();
		if (!FMath::IsFinite(Radius) || Radius < 0 || Placement.ContainsNaN() || GuardedOutpost->GetActorLocation().ContainsNaN() ||
		    FVector::Dist2D(Placement, GuardedOutpost->GetActorLocation()) > Radius)
		{
			return TEXT("Guard initial placement must be inside its outpost defense radius");
		}
	}
	return FString();
}

bool ULSEnemyBehaviorComponent::IsSameFaction(const AActor* Other) const
{
	const ILSFactionInterface* OwnFaction = Cast<ILSFactionInterface>(GetOwner());
	const ILSFactionInterface* OtherFaction = Cast<ILSFactionInterface>(Other);
	return OwnFaction && OtherFaction && OwnFaction->GetFaction() == OtherFaction->GetFaction();
}

#if WITH_EDITOR
EDataValidationResult ULSEnemyBehaviorComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const AActor* Owner = GetOwner();
	const bool bCheckPlacement = Owner && !IsTemplate() && !Owner->IsTemplate();
	FString Error = GetConfigurationError(bCheckPlacement, Owner ? Owner->GetActorLocation() : FVector::ZeroVector);
	if (Error.IsEmpty() && bCheckPlacement && bStartRegionallyDormant)
		Error = ALSBattlefieldEncounterCoordinator::GetDormantMembershipError(Owner);
	if (!Error.IsEmpty())
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: %s"), *GetPathName(), *Error)));
		return EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

void ULSEnemyBehaviorComponent::BeginPlay()
{
	Super::BeginPlay();
	InitialPosition = GetOwner()->GetActorLocation();
	const FString ConfigurationError = GetConfigurationError(true, InitialPosition);
	bConfigurationValid = ConfigurationError.IsEmpty();
	if (!bConfigurationValid)
	{
		LastStopReason = ConfigurationError;
	}
	Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	Health = GetOwner()->FindComponentByClass<ULSHealthComponent>();
	if (ULSOrderComponent* Orders = GetOwner()->FindComponentByClass<ULSOrderComponent>())
	{
		Orders->SetComponentTickEnabled(false);
	}
	if (Attack.IsValid())
	{
		Attack->SetAttackTargetResolver(FLSResolveAttackTarget::CreateUObject(this, &ThisClass::ResolveAttackTarget));
		Attack->OnAttackExecutedEvent.AddDynamic(this, &ThisClass::OnAttackExecuted);
	}
	if (Health.IsValid())
	{
		Health->OnDamagedEvent.AddDynamic(this, &ThisClass::OnDamaged);
		Health->OnDiedEvent.AddDynamic(this, &ThisClass::OnDamaged);
	}
	if (IsRegionallyDormant())
	{
		State = ELSEnemyState::RegionallyDormant;
		StopMovement();
		if (Attack.IsValid()) Attack->StopAttack();
		const FString MembershipError = ALSBattlefieldEncounterCoordinator::GetDormantMembershipError(GetOwner());
		if (!MembershipError.IsEmpty())
		{
			LastStopReason = MembershipError;
			UE_LOG(LogTemp, Error, TEXT("%s"), *MembershipError);
		}
	}
	if (LSBattleOutcome::AreActionsStopped(GetWorld())) EndBattleActions();
}

bool ULSEnemyBehaviorComponent::ActivateRegion()
{
	if (!IsRegionallyDormant() || Role != ELSEnemyRole::Assault || ShouldEndBattleActions() ||
		State == ELSEnemyState::Dead || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner())) return false;
	bRegionActivated = true;
	State = ELSEnemyState::WaitingForSetup;
	NextDecision = 0;
	return true;
}

bool ULSEnemyBehaviorComponent::ShouldEndBattleActions() const
{
	if (bBattleActionsEnded) return true;
	// 판정 관리 전장은 같은 수집 구간의 후속 타격을 살리고, 독립 적 월드는 기존 종료 계약을 따른다.
	if (LSBattleOutcome::IsManaged(GetWorld())) return LSBattleOutcome::AreActionsStopped(GetWorld());
	const ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	return Battlefield && Battlefield->IsBattleEndedForEnemies();
}

void ULSEnemyBehaviorComponent::EndBattleActions()
{
	if (bBattleActionsEnded) return;
	bBattleActionsEnded = true;
	if (State != ELSEnemyState::Dead) State = ELSEnemyState::Ended;
	LastStopReason = TEXT("Battle ended");
	Inbox.Reset();
	ActiveSupport = FLSEnemySupportRequest();
	bHasActiveSupport = false;
	bEngagementLatched = false;
	bRecovering = false;
	Target.Reset();
	RecoveryRoute.Reset();
	if (Attack.IsValid()) Attack->EndBattleActions();
	StopMovement();
}

void ULSEnemyBehaviorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopMovement();
	if (BoundController.IsValid())
	{
		BoundController->GetPathFollowingComponent()->OnRequestFinished.RemoveAll(this);
		BoundController.Reset();
	}
	if (Attack.IsValid())
	{
		Attack->SetAttackTargetResolver(FLSResolveAttackTarget());
		Attack->OnAttackExecutedEvent.RemoveAll(this);
		Attack->StopAttack();
	}
	if (Health.IsValid())
	{
		Health->OnDamagedEvent.RemoveAll(this);
		Health->OnDiedEvent.RemoveAll(this);
	}
	State = ELSEnemyState::Dead;
	Super::EndPlay(Reason);
}

bool ULSEnemyBehaviorComponent::IsStationary() const
{
	return GetOwner()->IsA<ALSOutpost>();
}

AActor* ULSEnemyBehaviorComponent::GetTarget() const
{
	return Attack.IsValid() && Attack->GetAttackTarget() ? Attack->GetAttackTarget() : Target.Get();
}

bool ULSEnemyBehaviorComponent::CanAttack() const
{
	const ALSOutpost* Outpost = Cast<ALSOutpost>(GetOwner());
	return !IsRegionallyDormant() && bConfigurationValid && State != ELSEnemyState::Ended && State != ELSEnemyState::Dead && Attack.IsValid() &&
	       Attack->IsConfigured() && (!Outpost || Outpost->IsArmed()) &&
	       ULSCombatTargetLibrary::IsCombatAlive(GetOwner());
}

void ULSEnemyBehaviorComponent::TickComponent(float Delta, ELevelTick TickType,
                                             FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Delta, TickType, TickFunction);
	if (MovementRequestId != FAIRequestID::InvalidRequest &&
	    FVector::Dist2D(GetOwner()->GetActorLocation(), LastMovementProgressPosition) > ArrivalTolerance)
	{
		MovementStart = GetWorld()->GetTimeSeconds();
		LastMovementProgressPosition = GetOwner()->GetActorLocation();
	}
	if (State == ELSEnemyState::Ended || State == ELSEnemyState::Dead)
	{
		return;
	}
	if (ShouldEndBattleActions())
	{
		EndBattleActions();
		return;
	}
	FLSHealthSnapshot Snapshot;
	if (Health.IsValid() && Health->TryGetHealth(Snapshot) && Snapshot.bIsDead)
	{
		State = ELSEnemyState::Dead;
		StopMovement();
		if (Attack.IsValid()) { Attack->StopAttack(); }
		return;
	}
	if (IsRegionallyDormant())
	{
		State = ELSEnemyState::RegionallyDormant;
		StopMovement();
		if (Attack.IsValid()) Attack->StopAttack();
		return;
	}
	if (!bConfigurationValid)
	{
		// 일반 SpawnActor 직후 첫 판단 전에 참조를 지정하는 초기화도 허용한다.
		LastStopReason = GetConfigurationError(true, InitialPosition);
		bConfigurationValid = LastStopReason.IsEmpty();
		if (!bConfigurationValid)
		{
			if (!bConfigurationWarningSent)
			{
				bConfigurationWarningSent = true;
				UE_LOGFMT(LogTemp, Warning, "[{Function}] {Owner}: {Reason}",
					("Function", __FUNCTION__), ("Owner", GetOwner()->GetPathName()), ("Reason", LastStopReason));
			}
			return;
		}
	}
	// 실제 중심을 매 프레임 측정한다. 여유 구간에서도 새 추적 경로를 만들지 않는다.
	FVector Center = FVector::ZeroVector;
	float Radius = 0;
	if (!IsStationary() && GetMovementDomain(Center, Radius))
	{
		const double Distance = FVector::Dist2D(GetOwner()->GetActorLocation(), Center);
		const bool bReturning = State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked;
		if (Distance > Radius + BoundaryAllowance)
		{
			if (Role == ELSEnemyRole::Guard && !bReturning) { BeginReturn(TEXT("Guard boundary")); }
			else if (Role == ELSEnemyRole::Assault && bEngagementLatched && !bRecovering) { BeginRecovery(); }
		}
		// 경계 지점의 부동소수점 왕복 오차를 실제 영역 이탈로 취급하지 않는다.
		else if (Distance - Radius > UE_KINDA_SMALL_NUMBER && !bReturning && State != ELSEnemyState::ReenteringDomain)
		{
			UE_LOGFMT(LogTemp, Verbose,
				"[{Function}] ENEMY_BOUNDARY_ENTRY distance={Distance} excess={Excess} goal={Goal} velocity={Velocity}",
				("Function", __FUNCTION__), ("Distance", FString::Printf(TEXT("%.17g"), Distance)),
				("Excess", FString::Printf(TEXT("%.17g"), Distance - Radius)),
				("Goal", MovementGoal.ToString()), ("Velocity", GetOwner()->GetVelocity().ToString()));
			StateBeforeReentry = State;
			bMovementFailedBeforeReentry = bMovementFailed;
			StopMovement();
			State = ELSEnemyState::ReenteringDomain;
			LastStopReason = TEXT("Soft boundary reentry");
		}
		else if (Distance - Radius <= UE_KINDA_SMALL_NUMBER && State == ELSEnemyState::ReenteringDomain)
		{
			StopMovement();
			State = StateBeforeReentry;
			bMovementFailed = bMovementFailedBeforeReentry;
		}
	}
	const AActor* CurrentTarget = Target.Get();
	const bool bTargetLost = CurrentTarget && !ULSCombatTargetLibrary::IsCombatAlive(CurrentTarget);
	const bool bEnteredRange = CurrentTarget && Attack.IsValid() && !Attack->GetAttackTarget() &&
		ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), CurrentTarget) <= Attack->GetAttackSettings().Range &&
		Attack->HasClearAttackPathFromLocation(GetOwner()->GetActorLocation(), CurrentTarget);
	const bool bTargetMoved = CurrentTarget &&
		FVector::DistSquared(CurrentTarget->GetActorLocation(), LastDecisionTargetLocation) >= FMath::Square(RepathDistance);
	if (GetWorld()->GetTimeSeconds() >= NextDecision || bTargetLost || bEnteredRange || bTargetMoved)
	{
		NextDecision = GetWorld()->GetTimeSeconds() + FMath::Max(0.01f, DecisionInterval);
		Decide();
		if (Target.IsValid()) LastDecisionTargetLocation = Target->GetActorLocation();
	}
}

AActor* ULSEnemyBehaviorComponent::ResolveAttackTarget() const
{
	if (!CanAttack() || ShouldEndBattleActions()) { return nullptr; }
	FVector Unused;
	return SelectTarget(Attack->GetAttackSettings().Range, false, Unused);
}

AActor* ULSEnemyBehaviorComponent::SelectTarget(float Radius, bool bRequirePath, FVector& OutGoal, const AActor* RequiredOpponent) const
{
	if (IsRegionallyDormant()) return nullptr;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	TArray<FOverlapResult> Hits;
	const FVector Here = GetOwner()->GetActorLocation();
	float SourceRadius, SourceHeight;
	GetOwner()->GetSimpleCollisionCylinder(SourceRadius, SourceHeight);
	// 감지는 중심 반경을 유지하고 타격 후보만 양측 충돌 외곽까지 포함한다.
	const FCollisionShape SearchShape = bRequirePath
		? FCollisionShape::MakeSphere(FMath::Max(0.01f, Radius))
		: FCollisionShape::MakeBox(FVector(Radius + SourceRadius + 1, Radius + SourceRadius + 1, Radius + SourceHeight + 1));
	GetWorld()->OverlapMultiByObjectType(Hits, Here, FQuat::Identity, Objects,
	    SearchShape,
	    FCollisionQueryParams(SCENE_QUERY_STAT(LSEnemyTargets), false, GetOwner()));
	AActor* Best = nullptr;
	LSEnemyDecisionPolicy::FLSEnemyTargetScore BestScore;
	FLSPlayerCastleState CastleState;
	const ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	const bool bHasCastle = Battlefield && Battlefield->TryGetPlayerCastleState(CastleState);
	const AActor* StrategicCastle = bRequirePath && Role == ELSEnemyRole::Assault && bHasCastle ? CastleState.Castle.Get() : nullptr;
	TArray<AActor*> Candidates;
	for (const FOverlapResult& Hit : Hits) { Candidates.AddUnique(Hit.GetActor()); }
	// 큰 구형 캡슐의 지면 근처는 원통 외곽과 다르므로 물리 겹침 질의가 성을 놓칠 수 있다.
	if (bHasCastle) { Candidates.AddUnique(CastleState.Castle.Get()); }
	const FVector ProposedCenter = Here;
	const FVector* ProvisionalCenter = bRequirePath && Role == ELSEnemyRole::Assault && !bEngagementLatched ? &ProposedCenter : nullptr;
	for (AActor* Candidate : Candidates)
	{
		if (bRequirePath && Candidate == StrategicCastle) { continue; }
		if (RequiredOpponent && Candidate != RequiredOpponent) { continue; }
		if (!ULSCombatTargetLibrary::CanAutoAttackTarget(GetOwner(), Candidate)) { continue; }
		if (!bRequirePath && !Attack->HasClearAttackPathFromLocation(Here, Candidate)) { continue; }
		const double Distance = FVector::DistSquared(Here, Candidate->GetActorLocation());
		if (bRequirePath ? Distance > FMath::Square(static_cast<double>(Radius))
			: ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Candidate) > Radius) { continue; }
		FLSHealthSnapshot Snapshot;
		const ULSHealthComponent* CandidateHealth = Candidate->FindComponentByClass<ULSHealthComponent>();
		if (!CandidateHealth || !CandidateHealth->TryGetHealth(Snapshot)) { continue; }
		FVector Goal = FVector::ZeroVector;
		if (bRequirePath && !FindAttackPosition(Candidate->GetActorLocation(), Goal, ProvisionalCenter, Candidate)) { continue; }
		const AActor* Current = Attack.IsValid() ? Attack->GetAttackTarget() : Target.Get();
		const LSEnemyDecisionPolicy::FLSEnemyTargetScore Score{Snapshot.CurrentHealth, Distance, Candidate->GetUniqueID(), Candidate == Current};
		if (!Best || LSEnemyDecisionPolicy::IsBetterTarget(Score, BestScore))
		{
			Best = Candidate;
			BestScore = Score;
			OutGoal = Goal;
		}
	}
	return Best;
}

bool ULSEnemyBehaviorComponent::GetMovementDomain(FVector& Center, float& Radius, const FVector* ProvisionalChaseCenter) const
{
	if (Role == ELSEnemyRole::Guard && IsValid(GuardAnchor))
	{
		Center = GuardAnchor->GetActorLocation();
		Radius = GuardAnchorRadius;
		return true;
	}
	if (Role == ELSEnemyRole::Guard && IsValid(GuardedOutpost))
	{
		Center = GuardedOutpost->GetActorLocation();
		Radius = GuardedOutpost->GetDefenseRadius();
		return true;
	}
	if (Role == ELSEnemyRole::Assault && bEngagementLatched && !bRecovering)
	{
		Center = EngagementCenter;
		Radius = ChaseRadius;
		return true;
	}
	if (Role == ELSEnemyRole::Assault && ProvisionalChaseCenter && !bRecovering)
	{
		Center = *ProvisionalChaseCenter;
		Radius = ChaseRadius;
		return true;
	}
	return false;
}

bool ULSEnemyBehaviorComponent::ResolvePath(const FVector& Desired, FVector& OutGoal, bool bConstrain, const FVector* ProvisionalChaseCenter) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* NavData = Pawn && Nav
		? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	FNavLocation Point;
	if (!NavData || !Nav->ProjectPointToNavigation(Desired, Point, FVector(50, 50, 300), NavData)) { return false; }
	FVector Center;
	float Radius = 0;
	const bool bDomain = bConstrain && GetMovementDomain(Center, Radius, ProvisionalChaseCenter);
	if (bDomain && FVector::Dist2D(Point.Location, Center) > Radius) { return false; }
	// 현재 수평 위치의 Nav 투영은 새 경로가 필요 없다. 한 점짜리 경로는 엔진에서 invalid다.
	if (FVector::DistSquared2D(Pawn->GetNavAgentLocation(), Point.Location) <= KINDA_SMALL_NUMBER)
	{
		OutGoal = Point.Location;
		return true;
	}
	UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Pawn->GetNavAgentLocation(), Point.Location, GetOwner());
	if (!Path || !Path->IsValid() || Path->IsPartial()) { return false; }
	float PreviousRadius = bDomain ? FVector::Dist2D(Pawn->GetNavAgentLocation(), Center) : 0;
	const bool bReturning = State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked || State == ELSEnemyState::ReenteringDomain;
	for (int32 Index = 0; bDomain && Index < Path->PathPoints.Num(); ++Index)
	{
		const float PointRadius = FVector::Dist2D(Path->PathPoints[Index], Center);
		// 실제 이탈 뒤에는 바깥쪽 꼭짓점이 더 멀어지지 않는 복귀 경로만 허용한다.
		if (PointRadius > Radius && !(bReturning && PointRadius <= PreviousRadius + KINDA_SMALL_NUMBER) &&
		    !(Index == 0 && PointRadius <= Radius + BoundaryAllowance)) { return false; }
		PreviousRadius = PointRadius;
	}
	OutGoal = Point.Location;
	return true;
}

bool ULSEnemyBehaviorComponent::GetNavigationOriginHeight(double& OutHeight) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* NavData = Pawn && Nav
		? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	FNavLocation Projected;
	if (!NavData || !Nav->ProjectPointToNavigation(Pawn->GetNavAgentLocation(), Projected, FVector(150, 150, 300), NavData))
	{
		return false;
	}
	OutHeight = GetOwner()->GetActorLocation().Z - Projected.Location.Z;
	return true;
}

bool ULSEnemyBehaviorComponent::FindAttackPosition(const FVector& OpponentLocation, FVector& OutGoal, const FVector* ProvisionalChaseCenter, const AActor* Opponent) const
{
	if (IsRegionallyDormant() || !Attack.IsValid()) { return false; }
	const float Range = Attack->GetAttackSettings().Range;
	const FVector Here = GetOwner()->GetActorLocation();
	// 저장된 지원 위치는 점으로 조사하고, 실제 표적은 공통 타격 판정과 같은 외곽 거리를 쓴다.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) { return false; }
	double Height = 0;
	if (!GetNavigationOriginHeight(Height)) { return false; }
	const auto AttackDistance = [&](const FVector& Location)
	{
		return Opponent ? ULSCombatTargetLibrary::GetAttackDistanceAtLocation(GetOwner(), Opponent, Location)
			: FVector::Dist(Location, OpponentLocation);
	};
	const auto CanAttackFrom = [&](const FVector& Location)
	{
		return AttackDistance(Location) <= Range && (!Opponent || Attack->HasClearAttackPathFromLocation(Location, Opponent));
	};
	float SourceRadius, SourceHeight, TargetRadius = 0, TargetHeight = 0;
	GetOwner()->GetSimpleCollisionCylinder(SourceRadius, SourceHeight);
	if (Opponent) { Opponent->GetSimpleCollisionCylinder(TargetRadius, TargetHeight); }
	const FVector ApproachCenter = OpponentLocation - FVector(0, 0, Opponent ? TargetHeight : 0);
	if (CanAttackFrom(Here) && ResolvePath(Pawn->GetNavAgentLocation(), OutGoal, true, ProvisionalChaseCenter))
	{
		return true;
	}
	FVector CenterGoal;
	if (ResolvePath(ApproachCenter, CenterGoal, true, ProvisionalChaseCenter) &&
		CanAttackFrom(CenterGoal + FVector(0, 0, Height)))
	{
		// 이동 표적을 향해 계속 접근하고 실제 사거리 진입은 판단/타격 단계에서 확인한다.
		OutGoal = CenterGoal;
		return true;
	}
	const FVector TowardOwner = (Here - OpponentLocation).GetSafeNormal2D();
	FVector Center;
	float Radius = 0;
	if (GetMovementDomain(Center, Radius, ProvisionalChaseCenter))
	{
		const FVector ToOpponent = (OpponentLocation - Center).GetSafeNormal2D();
		// 도착 여유만큼 안쪽을 먼저 쓰되 사거리가 닿는 합법적인 경계 지점도 허용한다.
		for (const float Inset : {ArrivalTolerance, 0.0f})
		{
			const FVector Edge = Center + ToOpponent * FMath::Min(FMath::Max(0.0f, Radius - Inset), static_cast<float>(FVector::Dist2D(Center, OpponentLocation)));
			FVector Goal;
			if (ResolvePath(Edge, Goal, true, ProvisionalChaseCenter) && CanAttackFrom(Goal + FVector(0, 0, Height)))
			{
				OutGoal = Goal;
				return true;
			}
		}
	}
	for (int32 Index = 0; Index < 17; ++Index)
	{
		const float Angle = Index == 0 ? 0.0f : ((Index % 2 ? 1.0f : -1.0f) * ((Index + 1) / 2) * 22.5f);
		const FVector Direction = TowardOwner.IsNearlyZero() ? FVector::ForwardVector : TowardOwner;
		const float ApproachRadius = Range * 0.9f + (Opponent ? SourceRadius + TargetRadius : 0);
		const FVector Desired = ApproachCenter + Direction.RotateAngleAxis(Angle, FVector::UpVector) * ApproachRadius;
		FVector Goal;
		if (ResolvePath(Desired, Goal, true, ProvisionalChaseCenter) && CanAttackFrom(Goal + FVector(0, 0, Height)))
		{
			OutGoal = Goal;
			return true;
		}
	}
	return false;
}

bool ULSEnemyBehaviorComponent::Move(const FVector& Goal, const FVector* AttackPosition, const AActor* AttackTarget)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		// 전략 목표인 성의 근접 추격도 공격 접근이다. 회피가 도주하는 성과의 간격을 유지하지 않게 한다.
		const bool bAttackPursuit = AttackPosition && FVector::Dist2D(GetOwner()->GetActorLocation(), *AttackPosition) <= DetectionRadius;
		// 충돌 검사를 통과한 좁은 복귀 지점은 RVO의 팽창 반경 대신 실제 캡슐로 접근한다.
		const bool bPreciseReturn = State == ELSEnemyState::Returning &&
			FVector::Dist2D(GetOwner()->GetActorLocation(), InitialPosition) <=
			ReturnAreaRadius + Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		Character->GetCharacterMovement()->SetAvoidanceEnabled(State != ELSEnemyState::Engaging && !bAttackPursuit && !bPreciseReturn);
	}
	float Acceptance = ArrivalTolerance;
	if (AttackPosition && Pawn && Attack.IsValid())
	{
		// 도착 오차를 더한 위치에서도 실제 타격 사거리를 벗어나지 않게 한다.
		double Height = 0;
		if (!GetNavigationOriginHeight(Height)) { return false; }
		const FVector StandingLocation = Goal + FVector(0, 0, Height);
		const double StandingDistance = AttackTarget
			? ULSCombatTargetLibrary::GetAttackDistanceAtLocation(GetOwner(), AttackTarget, StandingLocation)
			: FVector::Dist(StandingLocation, *AttackPosition);
		const double RemainingRange = Attack->GetAttackSettings().Range - StandingDistance;
		if (RemainingRange < 0) { return false; }
		Acceptance = FMath::Min(Acceptance, static_cast<float>(FMath::Max(0.0, RemainingRange - 1.0)));
	}
	const bool bReturningHome = State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked;
	const bool bReturning = bReturningHome || State == ELSEnemyState::ReenteringDomain;
	if (bReturning)
	{
		// 대체 지점 가까이에 멈춰도 실제 중심이 복귀 영역 안에 있어야 한다.
		const auto LimitArrival = [&](double Clearance)
		{
			if (Clearance < 0) { return false; }
			Acceptance = FMath::Min(Acceptance, static_cast<float>(FMath::Max(0.0, Clearance - 1.0)));
			return true;
		};
		if (bReturningHome && !LimitArrival(ReturnAreaRadius - FVector::Dist2D(Goal, InitialPosition))) { return false; }
		FVector Center;
		float Radius = 0;
		if (GetMovementDomain(Center, Radius) && !LimitArrival(Radius - FVector::Dist2D(Goal, Center))) { return false; }
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (MovementRequestId != FAIRequestID::InvalidRequest && Acceptance >= MovementAcceptance - 0.1f &&
	    FVector::DistSquared(Goal, MovementGoal) < FMath::Square(RepathDistance))
	{
		if (Now - MovementStart <= MovementTimeout) { return true; }
		StopMovement();
		RecordMovementFailure();
		NextMoveAttempt = Now + FailedPathRetryInterval;
		LastStopReason = TEXT("Movement stalled without position progress");
		return false;
	}
	if (Now < NextMoveAttempt) { return false; }
	const FIntVector ConditionsKey(FMath::RoundToInt(Goal.X), FMath::RoundToInt(Goal.Y), FMath::RoundToInt(Goal.Z));
	const uint32 Conditions = bReturning ? GetRouteConditions(Goal) : 0;
	if (bReturning)
	{
		if (const uint32* Failed = FailedReturnConditions.Find(ConditionsKey); Failed && *Failed == Conditions) { return false; }
	}
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	if (!Controller) { return false; }
	if (BoundController.Get() != Controller)
	{
		if (BoundController.IsValid()) { BoundController->GetPathFollowingComponent()->OnRequestFinished.RemoveAll(this); }
		BoundController = Controller;
		Controller->GetPathFollowingComponent()->OnRequestFinished.AddUObject(this, &ThisClass::OnMovementFinished);
	}
	MovementRequestId = FAIRequestID::InvalidRequest;
	bMovementFailed = false;
	FAIMoveRequest Request(Goal);
	Request.SetAcceptanceRadius(Acceptance);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetAllowPartialPath(false);
	Request.SetProjectGoalLocation(false);
	FNavPathSharedPtr ActualPath;
	const FPathFollowingRequestResult Result = Controller->MoveTo(Request, &ActualPath);
	// 경로 무효화 뒤에는 새 경로를 검사한 다음 다시 제출한다.
	if (ActualPath.IsValid()) { ActualPath->EnableRecalculationOnInvalidation(false); }
	MovementRequestId = Result.Code == EPathFollowingRequestResult::RequestSuccessful ? Result.MoveId : FAIRequestID::InvalidRequest;
	MovementGoal = Goal;
	MovementAcceptance = Acceptance;
	MovementStart = Now;
	LastMovementProgressPosition = GetOwner()->GetActorLocation();
	MovementConditionKey = ConditionsKey;
	MovementConditions = Conditions;
	FVector DomainCenter;
	float DomainRadius;
	if (ActualPath.IsValid() && GetMovementDomain(DomainCenter, DomainRadius))
	{
		float PreviousRadius = FVector::Dist2D(GetOwner()->GetActorLocation(), DomainCenter);
		const TArray<FNavPathPoint>& Points = ActualPath->GetPathPoints();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const float PointRadius = FVector::Dist2D(Points[Index].Location, DomainCenter);
			if (PointRadius > DomainRadius && !(bReturning && PointRadius <= PreviousRadius + KINDA_SMALL_NUMBER) &&
			    !(Index == 0 && PointRadius <= DomainRadius + BoundaryAllowance))
			{
				StopMovement();
				RecordMovementFailure();
				NextMoveAttempt = Now + FailedPathRetryInterval;
				LastStopReason = TEXT("Actual movement path leaves domain");
				return false;
			}
			PreviousRadius = PointRadius;
		}
	}
	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		RecordMovementFailure();
		NextMoveAttempt = Now + FailedPathRetryInterval;
		LastStopReason = TEXT("Move request failed");
		return false;
	}
	return true;
}

void ULSEnemyBehaviorComponent::StopMovement()
{
	MovementRequestId = FAIRequestID::InvalidRequest;
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr) { Controller->StopMovement(); }
	if (State == ELSEnemyState::GuardIdle || State == ELSEnemyState::ReturnBlocked)
	{
		// 정지한 수비병도 다른 병력의 회피 대상에 남아야 한다.
		if (ACharacter* Character = Cast<ACharacter>(Pawn)) { Character->GetCharacterMovement()->SetAvoidanceEnabled(true); }
	}
}

void ULSEnemyBehaviorComponent::OnMovementFinished(FAIRequestID Id, const FPathFollowingResult& Result)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld())) return;
	if (MovementRequestId == FAIRequestID::InvalidRequest || Id != MovementRequestId) { return; }
	MovementRequestId = FAIRequestID::InvalidRequest;
	const bool bPathInvalidated = Result.HasFlag(FPathFollowingResultFlags::InvalidPath);
	bMovementFailed = !Result.IsSuccess() && !bPathInvalidated;
	NextDecision = 0;
	if (bPathInvalidated)
	{
		// 무효화는 막힌 경로가 아니다. 다음 행동 틱의 기존 역할·경로 검사를 즉시 다시 수행한다.
		NextMoveAttempt = GetWorld()->GetTimeSeconds();
		LastStopReason = TEXT("Path invalidated; revalidating");
	}
	else if (bMovementFailed)
	{
		RecordMovementFailure();
		NextMoveAttempt = GetWorld()->GetTimeSeconds() + FailedPathRetryInterval;
		LastStopReason = TEXT("Path following failed");
	}
}

void ULSEnemyBehaviorComponent::RecordMovementFailure()
{
	bMovementFailed = true;
	if (State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked)
	{
		FailedReturnConditions.Add(MovementConditionKey, MovementConditions);
	}
}

uint32 ULSEnemyBehaviorComponent::GetRouteConditions(const FVector& Goal) const
{
	uint32 Hash = 0;
	FBox RouteBounds(ForceInit);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	UNavigationPath* Path = Nav && Pawn ? Nav->FindPathToLocationSynchronously(GetWorld(), Pawn->GetNavAgentLocation(), Goal, GetOwner()) : nullptr;
	if (Path)
	{
		for (const FVector& Point : Path->PathPoints)
		{
			RouteBounds += Point;
			Hash = HashCombine(Hash, GetTypeHash(FIntVector(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y), FMath::RoundToInt(Point.Z))));
		}
	}
	RouteBounds += Goal;
	RouteBounds = RouteBounds.ExpandBy(FVector(100, 100, 300));
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It == GetOwner() || !It->GetActorEnableCollision() || !RouteBounds.Intersect(It->GetComponentsBoundingBox())) { continue; }
		Hash = HashCombine(Hash, GetTypeHash(It->GetUniqueID()));
		Hash = HashCombine(Hash, GetTypeHash(It->GetActorLocation()));
		Hash = HashCombine(Hash, GetTypeHash(It->GetActorRotation().Vector()));
		Hash = HashCombine(Hash, GetTypeHash(It->GetActorScale3D()));
	}
	return Hash;
}

void ULSEnemyBehaviorComponent::BeginReturn(const FString& Reason)
{
	StopMovement();
	State = ELSEnemyState::Returning;
	LastStopReason = Reason;
	Target.Reset();
	bHasActiveSupport = false;
	bEngagementLatched = false;
	FailedReturnConditions.Reset();
}

void ULSEnemyBehaviorComponent::ReturnHome()
{
	const FVector Here = GetOwner()->GetActorLocation();
	FVector Center;
	float Radius = 0;
	const bool bInDomain = GetMovementDomain(Center, Radius) && FVector::Dist2D(Here, Center) <= Radius;
	if (FVector::Dist2D(Here, InitialPosition) <= ReturnAreaRadius && bInDomain)
	{
		State = ELSEnemyState::GuardIdle;
		StopMovement();
		return;
	}
	if (bMovementFailed)
	{
		State = ELSEnemyState::ReturnBlocked;
		bMovementFailed = false;
		StopMovement();
	}
	if (GetWorld()->GetTimeSeconds() < NextMoveAttempt) { return; }
	for (int32 Index = 0; Index < 25; ++Index)
	{
		const float Distance = Index == 0 ? 0.0f : ReturnAreaRadius * (Index <= 8 ? 0.5f : 0.9f);
		const float Angle = Index <= 8 ? Index * 45.0f : (Index - 9) * 22.5f;
		const FVector Desired = InitialPosition + FVector(FMath::Cos(FMath::DegreesToRadians(Angle)),
		    FMath::Sin(FMath::DegreesToRadians(Angle)), 0) * Distance;
		FVector Goal;
		if (!ResolvePath(Desired, Goal) || FVector::Dist2D(Goal, InitialPosition) > ReturnAreaRadius) { continue; }
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		if (Character)
		{
			const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
			Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
			const FVector CapsuleCenter = Goal + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight());
			if (GetWorld()->OverlapAnyTestByObjectType(CapsuleCenter, FQuat::Identity, Objects,
			    FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() * 0.95f),
			    FCollisionQueryParams(SCENE_QUERY_STAT(LSEnemyReturn), false, GetOwner()))) { continue; }
		}
		State = ELSEnemyState::Returning;
		if (Move(Goal)) { return; }
		if (GetWorld()->GetTimeSeconds() < NextMoveAttempt) { break; }
	}
	State = ELSEnemyState::ReturnBlocked;
	StopMovement();
	LastStopReason = TEXT("No reachable return point");
	NextMoveAttempt = GetWorld()->GetTimeSeconds() + FailedPathRetryInterval;
}

void ULSEnemyBehaviorComponent::BeginRecovery()
{
	StopMovement();
	bRecovering = true;
	bEngagementLatched = false;
	bHasActiveSupport = false;
	Target.Reset();
	RecoveryProgress = 0;
	RecoveryProgressBase = 0;
	RecoveryRoute.Reset();
	State = ELSEnemyState::MarchRecovery;
	LastStopReason = TEXT("Chase boundary");
}

void ULSEnemyBehaviorComponent::ReenterDomain()
{
	FVector Center;
	float Radius = 0;
	if (!GetMovementDomain(Center, Radius)) { return; }
	if (GetWorld()->GetTimeSeconds() < NextMoveAttempt) { return; }
	const FVector Direction = (GetOwner()->GetActorLocation() - Center).GetSafeNormal2D();
	const float InwardRadius = FMath::Max(0.0f, Radius - FMath::Max(1.0f, ArrivalTolerance * 2));
	for (int32 Index = 0; Index < 18; ++Index)
	{
		const float Angle = Index == 0 ? 0.0f : (Index % 2 ? 1.0f : -1.0f) * ((Index + 1) / 2) * 22.5f;
		const FVector Desired = Index == 17 ? Center : Center + Direction.RotateAngleAxis(Angle, FVector::UpVector) * InwardRadius;
		FVector Goal;
		if (ResolvePath(Desired, Goal) && Move(Goal)) { return; }
		if (GetWorld()->GetTimeSeconds() < NextMoveAttempt) { return; }
	}
	StopMovement();
	LastStopReason = TEXT("No reachable inward domain point");
	NextMoveAttempt = GetWorld()->GetTimeSeconds() + FailedPathRetryInterval;
}

void ULSEnemyBehaviorComponent::March()
{
	ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	FLSPlayerCastleState Castle;
	if (!Battlefield || !Battlefield->TryGetPlayerCastleState(Castle))
	{
		StopMovement();
		State = ELSEnemyState::WaitingForSetup;
		return;
	}
	FVector Goal;
	if (!FindAttackPosition(Castle.Location, Goal, nullptr, Castle.Castle.Get()))
	{
		StopMovement();
		State = ELSEnemyState::WaitingForPath;
		LastStopReason = TEXT("Castle path unavailable");
		return;
	}
	if (bRecovering)
	{
		// 경로상의 순진행 위치만 센다. 왕복 회피 거리는 누적하지 않는다.
		const FVector Here = GetOwner()->GetActorLocation();
		const auto PreviousSample = LSEnemyDecisionPolicy::SampleRoute(RecoveryRoute, Here);
		if (PreviousSample.bValid && PreviousSample.DistanceFromRoute <= FMath::Max(BoundaryAllowance, ArrivalTolerance))
		{
			RecoveryProgress = FMath::Max(RecoveryProgress, RecoveryProgressBase + PreviousSample.Progress);
		}
		if (RecoveryRoute.IsEmpty() || FVector::Dist2D(Goal, RecoveryGoal) >= RepathDistance)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			const APawn* Pawn = Cast<APawn>(GetOwner());
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Pawn->GetNavAgentLocation(), Goal, GetOwner());
			if (!Path || !Path->IsValid() || Path->IsPartial())
			{
				StopMovement();
				State = ELSEnemyState::WaitingForPath;
				LastStopReason = TEXT("Recovery path unavailable");
				return;
			}
			RecoveryProgressBase = RecoveryProgress;
			RecoveryRoute = Path->PathPoints;
			RecoveryGoal = Goal;
		}
		// 실제 전략 공격 위치에 도착하면 짧은 경로의 목적을 달성한 것이다.
		const bool bAtCastleAttackPosition = ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Castle.Castle.Get()) <= Attack->GetAttackSettings().Range &&
			Attack->HasClearAttackPathFromLocation(Here, Castle.Castle.Get()) && FVector::Dist2D(Here, Goal) <= ArrivalTolerance;
		if (LSEnemyDecisionPolicy::IsRecoveryComplete(RecoveryProgress, RecoveryAdvanceDistance, bAtCastleAttackPosition))
		{
			bRecovering = false;
		}
	}
	State = bRecovering ? ELSEnemyState::MarchRecovery : ELSEnemyState::Marching;
	Move(Goal, &Castle.Location, Castle.Castle.Get());
}

bool ULSEnemyBehaviorComponent::TryEngage(AActor* Candidate, FVector Goal, const AActor* StrategicCastle)
{
	if (!Candidate || !CanAttack()) { return false; }
	if (Role == ELSEnemyRole::Assault && Candidate == StrategicCastle)
	{
		// 성 자체의 이동은 전략 목표를 향한 진격이며 다른 병력에 의한 유인이 아니다.
		bEngagementLatched = false;
		Target = Candidate;
		March();
		return true;
	}
	if (Role == ELSEnemyRole::Assault && !bEngagementLatched)
	{
		const FVector PreviousCenter = EngagementCenter;
		EngagementCenter = GetOwner()->GetActorLocation();
		bEngagementLatched = true;
		// 새 추적 기준점을 적용한 뒤 경로를 다시 검증한다.
		if (!FindAttackPosition(Candidate->GetActorLocation(), Goal, nullptr, Candidate))
		{
			bEngagementLatched = false;
			EngagementCenter = PreviousCenter;
			return false;
		}
	}
	Target = Candidate;
	State = ELSEnemyState::Engaging;
	const FVector OpponentLocation = Candidate->GetActorLocation();
	Move(Goal, &OpponentLocation, Candidate);
	return true;
}

void ULSEnemyBehaviorComponent::Decide()
{
	if (IsRegionallyDormant() || !Health.IsValid() || !Attack.IsValid()) { return; }
	FLSHealthSnapshot Snapshot;
	if (!Health->TryGetHealth(Snapshot) || !Attack->IsConfigured()) { return; }
	if (ULSOrderComponent* Orders = GetOwner()->FindComponentByClass<ULSOrderComponent>(); Orders && Orders->IsComponentTickEnabled())
	{
		Orders->SetComponentTickEnabled(false);
		LastStopReason = TEXT("Disabled competing Orders tick");
	}
	if (!IsStationary())
	{
		FLSUnitStatRow Stats;
		const ALSUnit* Unit = Cast<ALSUnit>(GetOwner());
		if (!Unit || !Unit->TryGetStats(Stats)) { return; }
		DetectionRadius = Stats.DetectionRange;
		if (Role == ELSEnemyRole::Guard && !IsValid(GuardAnchor) && (!IsValid(GuardedOutpost) || GuardedOutpost->GetWorld() != GetWorld()))
		{
			State = ELSEnemyState::WaitingForSetup;
			StopMovement();
			Attack->StopAttack();
			LastStopReason = TEXT("Guard requires explicit same-world outpost");
			return;
		}
	}
	if (State == ELSEnemyState::WaitingForSetup)
	{
		State = IsStationary() || Role == ELSEnemyRole::Guard ? ELSEnemyState::GuardIdle : ELSEnemyState::Marching;
	}
	Inbox.RemoveAll([this](const FLSEnemySupportRequest& Request)
	{
		return GetWorld()->GetTimeSeconds() - Request.Time > PendingSupportLifetime;
	});
	FLSPlayerCastleState CastleState;
	const ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	const AActor* StrategicCastle = Battlefield && Battlefield->TryGetPlayerCastleState(CastleState)
		? CastleState.Castle.Get() : nullptr;
	AActor* InRange = ResolveAttackTarget();
	if (State == ELSEnemyState::ReenteringDomain)
	{
		if (InRange) { Attack->StartAttack(InRange); }
		else { Attack->StopAttack(); }
		ReenterDomain();
		return;
	}
	if (InRange)
	{
		Target = InRange;
		// 복귀와 진격 회복 상태는 사거리 전투로 해제하지 않는다.
		if (State != ELSEnemyState::Returning && State != ELSEnemyState::ReturnBlocked && !bRecovering)
		{
			StopMovement();
			if (!IsStationary() && Role == ELSEnemyRole::Assault && InRange == StrategicCastle)
			{
				bEngagementLatched = false;
			}
			else if (!IsStationary() && Role == ELSEnemyRole::Assault && !bEngagementLatched)
			{
				EngagementCenter = GetOwner()->GetActorLocation();
				bEngagementLatched = true;
			}
			State = ELSEnemyState::Engaging;
		}
		Attack->StartAttack(InRange);
		if (State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked) { ReturnHome(); }
		else if (bRecovering) { March(); }
		return;
	}
	Attack->StopAttack();
	if (!IsStationary() && Role == ELSEnemyRole::Assault && bEngagementLatched && Target.IsValid() &&
	    Target.Get() != StrategicCastle &&
	    FVector::Dist2D(Target->GetActorLocation(), EngagementCenter) > ChaseRadius)
	{
		BeginRecovery();
	}
	if (!IsStationary() && Role == ELSEnemyRole::Assault && bEngagementLatched && bHasActiveSupport &&
	    FVector::Dist2D(ActiveSupport.OpponentLocation, EngagementCenter) > ChaseRadius)
	{
		BeginRecovery();
	}
	Target.Reset();
	if (IsStationary()) { State = ELSEnemyState::GuardIdle; return; }
	if (State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked) { ReturnHome(); return; }
	if (bRecovering) { March(); return; }
	if (bMovementFailed)
	{
		bMovementFailed = false;
		bHasActiveSupport = false;
		if (Role == ELSEnemyRole::Guard) { BeginReturn(TEXT("Approach failed")); ReturnHome(); return; }
		bEngagementLatched = false;
		March();
		return;
	}
	FVector Goal = FVector::ZeroVector;
	bool bSupportSelected = SelectSupport();
	if (bSupportSelected)
	{
		if (Role == ELSEnemyRole::Assault && !bEngagementLatched)
		{
			EngagementCenter = GetOwner()->GetActorLocation();
			bEngagementLatched = true;
		}
		if (GetSupportGoal(ActiveSupport, Goal) && FVector::Dist2D(GetOwner()->GetActorLocation(), Goal) <= ArrivalTolerance)
		{
			// 도착한 판단에서 전체 로컬 후보를 다시 찾고 기존 추적 기준점을 유지한다.
			bHasActiveSupport = false;
			bSupportSelected = false;
			LastStopReason = FString::Printf(TEXT("Support arrived: %s"), *GetOwner()->GetActorLocation().ToString());
		}
	}
	AActor* Local = bSupportSelected
		? (ActiveSupport.Opponent.IsValid() ? SelectTarget(DetectionRadius, true, Goal, ActiveSupport.Opponent.Get()) : nullptr)
		: SelectTarget(DetectionRadius, true, Goal);
	if (TryEngage(Local, Goal, StrategicCastle)) { return; }
	if (bSupportSelected)
	{
		// 원격 상대의 현재 위치를 읽지 않고 실제 보고에 저장된 위치만 조사한다.
		State = ELSEnemyState::Supporting;
		if (GetSupportGoal(ActiveSupport, Goal))
		{
			if (GetWorld()->GetTimeSeconds() < NextMoveAttempt || Move(Goal, &ActiveSupport.OpponentLocation)) { return; }
		}
		bHasActiveSupport = false;
	}
	if (Role == ELSEnemyRole::Guard)
	{
		if (State == ELSEnemyState::Engaging || State == ELSEnemyState::Supporting)
		{
			BeginReturn(TEXT("Engagement ended"));
			ReturnHome();
		}
		else { State = ELSEnemyState::GuardIdle; StopMovement(); }
	}
	else
	{
		bEngagementLatched = false;
		March();
	}
}

void ULSEnemyBehaviorComponent::EmitSupport(AActor* Opponent, const FVector& OwnLocation, const FVector& OpponentLocation)
{
	if (IsRegionallyDormant() || State == ELSEnemyState::Ended || ShouldEndBattleActions() || !IsValid(Opponent)) { return; }
	FLSEnemySupportRequest Request;
	Request.Requester = GetOwner();
	Request.Opponent = Opponent;
	Request.RequesterLocation = OwnLocation;
	Request.OpponentLocation = OpponentLocation;
	Request.Time = GetWorld()->GetTimeSeconds();
	Request.RequestId = ++LastRequestId;
	float MaximumAcceptanceRadius = 0;
	// 담당 거점의 전담 수비병만 거리와 무관하게 수신한다.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* RecipientActor = *It;
		if (RecipientActor == GetOwner()) { continue; }
		ULSEnemyBehaviorComponent* Recipient = RecipientActor->FindComponentByClass<ULSEnemyBehaviorComponent>();
		if (!Recipient || Recipient->IsStationary() || !IsSameFaction(RecipientActor)) { continue; }
		MaximumAcceptanceRadius = FMath::Max(MaximumAcceptanceRadius, Recipient->SupportAcceptanceRadius);
		if (Recipient->Role == ELSEnemyRole::Guard && !Recipient->GuardAnchor && Recipient->GuardedOutpost == GetOwner())
		{
			Recipient->ReceiveSupport(Request);
		}
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, OwnLocation, FQuat::Identity, Objects,
	    FCollisionShape::MakeSphere(FMath::Max(0.01f, MaximumAcceptanceRadius)),
	    FCollisionQueryParams(SCENE_QUERY_STAT(LSEnemySupport), false, GetOwner()));
	TSet<ULSEnemyBehaviorComponent*> Delivered;
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Actor = Hit.GetActor();
		ULSEnemyBehaviorComponent* Recipient = Actor ? Actor->FindComponentByClass<ULSEnemyBehaviorComponent>() : nullptr;
		if (!Recipient || Recipient == this || Recipient->IsStationary() || Delivered.Contains(Recipient) || !IsSameFaction(Actor)) { continue; }
		if (FVector::DistSquared(OwnLocation, Actor->GetActorLocation()) <= FMath::Square(Recipient->SupportAcceptanceRadius))
		{
			Delivered.Add(Recipient);
			Recipient->ReceiveSupport(Request);
		}
	}
}

void ULSEnemyBehaviorComponent::ReceiveSupport(const FLSEnemySupportRequest& Request)
{
	if (IsRegionallyDormant() || State == ELSEnemyState::Ended || State == ELSEnemyState::Dead || ShouldEndBattleActions()) { return; }
	if (Role == ELSEnemyRole::Assault)
	{
		FLSPlayerCastleState Castle;
		const ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
		// 이미 현재 위치로 진격하는 전략 목표를 과거 지원 위치 조사로 바꾸지 않는다.
		if (Battlefield && Battlefield->TryGetPlayerCastleState(Castle) && Request.Opponent == Castle.Castle) { return; }
	}
	if (bHasActiveSupport && ActiveSupport.Requester == Request.Requester && ActiveSupport.Opponent == Request.Opponent)
	{
		ActiveSupport = Request;
		return;
	}
	for (FLSEnemySupportRequest& Existing : Inbox)
	{
		if (Existing.Requester == Request.Requester && Existing.Opponent == Request.Opponent)
		{
			Existing = Request;
			return;
		}
	}
	Inbox.Add(Request);
}

bool ULSEnemyBehaviorComponent::GetSupportGoal(const FLSEnemySupportRequest& Request, FVector& OutGoal) const
{
	const FVector ProposedCenter = GetOwner()->GetActorLocation();
	const FVector* ProvisionalCenter = Role == ELSEnemyRole::Assault && !bEngagementLatched ? &ProposedCenter : nullptr;
	return FindAttackPosition(Request.OpponentLocation, OutGoal, ProvisionalCenter);
}

bool ULSEnemyBehaviorComponent::SelectSupport()
{
	int32 Best = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Inbox.Num(); ++Index)
	{
		const FLSEnemySupportRequest& Request = Inbox[Index];
		const bool bOwnOutpost = Role == ELSEnemyRole::Guard && !GuardAnchor && Request.Requester.Get() == GuardedOutpost;
		if (bHasActiveSupport && !bOwnOutpost) { continue; }
		FVector Goal;
		if (!GetSupportGoal(Request, Goal)) { continue; }
		const double Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), Request.RequesterLocation);
		const bool bBestOwn = Best != INDEX_NONE && Role == ELSEnemyRole::Guard && !GuardAnchor && Inbox[Best].Requester.Get() == GuardedOutpost;
		if (Best == INDEX_NONE || (bOwnOutpost && !bBestOwn) || (bOwnOutpost == bBestOwn && Distance < BestDistance))
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	if (Best != INDEX_NONE)
	{
		ActiveSupport = Inbox[Best];
		Inbox.RemoveAt(Best);
		bHasActiveSupport = true;
	}
	return bHasActiveSupport;
}

void ULSEnemyBehaviorComponent::OnAttackExecuted(const FLSAttackExecution& Execution)
{
	if (const ALSOutpost* Outpost = Cast<ALSOutpost>(GetOwner()); Outpost && !Outpost->IsArmed()) { return; }
	EmitSupport(Execution.Target.Get(), Execution.SourceLocation, Execution.TargetLocation);
}

void ULSEnemyBehaviorComponent::OnDamaged(const FLSDamageResult& Damage)
{
	if (!ULSCombatTargetLibrary::IsCombatAlive(Damage.SourceActor)) { return; }
	const ILSFactionInterface* SourceFaction = Cast<ILSFactionInterface>(Damage.SourceActor);
	const ILSFactionInterface* OwnFaction = Cast<ILSFactionInterface>(GetOwner());
	if (IsValid(Damage.SourceActor) && SourceFaction && OwnFaction && SourceFaction->GetFaction() != OwnFaction->GetFaction())
	{
		EmitSupport(Damage.SourceActor, GetOwner()->GetActorLocation(), Damage.SourceActor->GetActorLocation());
	}
}
