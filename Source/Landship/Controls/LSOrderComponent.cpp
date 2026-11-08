#include "Controls/LSOrderComponent.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSPathObstacle.h"
#include "Obstacles/LSObstacleGeometry.h"
#include "Navigation/LSMineAvoidance.h"
#include "Navigation/LSMineAwareMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Controls/LSSelectableComponent.h"
#include "Troops/LSUnit.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSCastle.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSOutpost.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

namespace
{
bool IsCapsulePathClear(const TArray<FVector>& BodyCenters, float Radius, float Height,
	const FVector& Center, float OtherRadius, float OtherHeight)
{
	if (BodyCenters.Num() < 2) return false;
	const float CombinedRadius = Radius + OtherRadius;
	const float CombinedHeight = Height + OtherHeight;
	const double AxisHalfHeight = CombinedHeight - CombinedRadius;
	const double ContactDistanceSquared = FMath::Square(static_cast<double>(CombinedRadius));
	auto AxisDistanceSquared = [Center, AxisHalfHeight](const FVector& Point)
	{
		const double Gap = FMath::Max(0.0, FMath::Abs(Point.Z - Center.Z) - AxisHalfHeight);
		return FVector::DistSquared2D(Point, Center) + Gap * Gap;
	};
	// 처음 맞닿은 몸체에서는 거리가 줄지 않는 탈출만 허용하고, 벗어난 뒤의 재진입은 거부한다.
	for (int32 Index = 1; Index < BodyCenters.Num(); ++Index)
	{
		const FVector Start = BodyCenters[Index - 1], End = BodyCenters[Index];
		if (FMath::PointDistToSegmentSquared(FVector(Center.X, Center.Y, 0), FVector(Start.X, Start.Y, 0),
			FVector(End.X, End.Y, 0)) > ContactDistanceSquared + 0.0001) continue;
		if (!LSMineAvoidance::CapsuleSegmentTouchesSphere(Start, End, CombinedRadius, CombinedHeight, {Center, 0})) continue;
		const FVector Relative = Start - Center, Delta = End - Start;
		const double VerticalGap = FMath::Sign(Relative.Z) * FMath::Max(0.0, FMath::Abs(Relative.Z) - AxisHalfHeight);
		const double SeparationDerivative = Relative.X * Delta.X + Relative.Y * Delta.Y + VerticalGap * Delta.Z;
		if (AxisDistanceSquared(Start) > ContactDistanceSquared + 0.0001 || SeparationDerivative < -0.0001)
			return false;
	}
	return AxisDistanceSquared(BodyCenters.Last()) > ContactDistanceSquared + 0.0001;
}
} // namespace

ULSOrderComponent::ULSOrderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void ULSOrderComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ULSHealthComponent* Health = GetOwner()->FindComponentByClass<ULSHealthComponent>())
	{
		Health->OnDamagedEvent.AddDynamic(this, &ULSOrderComponent::OnOwnerDamaged);
		Health->OnDiedEvent.AddDynamic(this, &ULSOrderComponent::OnOwnerDamaged);
	}
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		Character->OnCharacterMovementUpdated.AddDynamic(this, &ULSOrderComponent::OnOwnerMovementUpdated);
	Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	if (Attack)
	{
		Attack->StopAttack();
	}
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		EndBattleActions();
}

void ULSOrderComponent::EndBattleActions()
{
	bKnockbackActive = false;
	if (bBattleActionsEnded) return;
	bBattleActionsEnded = true;
	CancelIdleYield();
	GetWorld()->GetTimerManager().ClearTimer(OrderUpdateTimer);
	CancelDisarm();
	bMineBlocked = false;
	SetMineAvoidance(false);
	PendingOrders.Reset();
	ActiveOrder = FLSOrder();
	SetEngagementTarget(nullptr);
	bHasEngagement = false;
	NextRepathTime = 0;
	StopMovement();
	if (Attack)
		Attack->EndBattleActions();
}

void ULSOrderComponent::SuspendForPool()
{
	++OrderLifeSerial;
	EndBattleActions();
	SetComponentTickEnabled(false);
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
		if (const AAIController* AI = Cast<AAIController>(Pawn->GetController()))
			if (UPathFollowingComponent* Path = AI->GetPathFollowingComponent())
				Path->OnRequestFinished.RemoveAll(this);
	MovementRequestId = FAIRequestID::InvalidRequest;
	bMovementCompleted = false;
	bLastMovementSucceeded = false;
	bKnockbackActive = false;
	KnockbackResumeAfter = 0;
	LastAttackRepathTime = -1;
	LastAttackTargetLocation = FVector::ZeroVector;
	bAttackApproaching = false;
	bUpdatingOrders = false;
	LastFailure = ELSOrderFailure::None;
	bYieldingToCastle = false;
	YieldDestination = FVector::ZeroVector;
	YieldCastleOrigin = FVector::ZeroVector;
	YieldCastleGoal = FVector::ZeroVector;
	YieldAttempts = 0;
	bYieldSpaceChanging = false;
	NextYieldTime = 0;
	BlockingBarricade.Reset();
}

void ULSOrderComponent::ResumeFromPool()
{
	bBattleActionsEnded = false;
	if (ULSHealthComponent* Health = GetOwner()->FindComponentByClass<ULSHealthComponent>())
	{
		Health->OnDamagedEvent.AddUniqueDynamic(this, &ULSOrderComponent::OnOwnerDamaged);
		Health->OnDiedEvent.AddUniqueDynamic(this, &ULSOrderComponent::OnOwnerDamaged);
	}
	SetComponentTickEnabled(true);
}

bool ULSOrderComponent::CanAttack() const
{
	if (const ALSUnit* Unit = Cast<ALSUnit>(GetOwner()); Unit && !Unit->SupportsBasicAttack())
		return false;
	const ULSBasicAttackComponent* Component =
	    GetOwner() ? GetOwner()->FindComponentByClass<ULSBasicAttackComponent>() : nullptr;
	return Component && Component->IsConfigured() && ULSCombatTargetLibrary::IsCombatAlive(GetOwner());
}

bool ULSOrderComponent::CanMove() const
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()) ||
	    !ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
	{
		return false;
	}
	FLSUnitStatRow UnitStats;
	if (const ALSUnit* Unit = Cast<ALSUnit>(GetOwner());
	    Unit && (!Unit->TryGetStats(UnitStats) || UnitStats.MovementSpeed <= 0))
	{
		return false;
	}
	FLSCastleStatRow CastleStats;
	if (const ALSCastle* Castle = Cast<ALSCastle>(GetOwner());
	    Castle && (!Castle->TryGetStats(CastleStats) || CastleStats.MovementSpeed <= 0))
	{
		return false;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->GetMovementComponent() && Cast<AAIController>(Pawn->GetController());
}

bool ULSOrderComponent::ResolveDestination(const FVector& Goal, FVector& OutDestination, FNavPathSharedPtr* OutPath, bool* OutFullPathFailed) const
{
	if (OutFullPathFailed) *OutFullPathFailed = false;
	if (OutPath) OutPath->Reset();
	if (Goal.ContainsNaN() || !CanMove())
	{
		return false;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ANavigationData* Data = Nav && Pawn ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	FNavLocation Point;
	if (!Data || !Nav->ProjectPointToNavigation(Goal, Point, FVector(150, 150, 300), Data))
	{
		return false;
	}
	// 이미 목적지에 있는 경우 한 점짜리 경로가 반환되어도 유효한 명령이다.
	if (FVector::DistSquared(Pawn->GetNavAgentLocation(), Point.Location) <= FMath::Square(ArrivalTolerance))
	{
		OutDestination = Point.Location;
		return true;
	}
	FPathFindingQuery Query(Pawn, *Data, Pawn->GetNavAgentLocation(), Point.Location);
	Query.SetAllowPartialPaths(false);
	const FNavPathSharedPtr Path = Nav->FindPathSync(Pawn->GetNavAgentPropertiesRef(), Query).Path;
	if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial())
	{
		if (OutFullPathFailed) *OutFullPathFailed = true;
		return false;
	}
	if (OutPath) *OutPath = Path;
	OutDestination = Point.Location;
	return true;
}

ALSBarricade* ULSOrderComponent::GetBlockingBarricade() const
{
	return BlockingBarricade.Get();
}

ALSBarricade* ULSOrderComponent::FindBlockingBarricade(const FVector& Goal) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* Data = Nav && Pawn ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	FNavLocation Destination;
	if (!Data || !Nav->ProjectPointToNavigation(Goal, Destination, FVector(150, 150, 300), Data)) return nullptr;
	FPathFindingQuery Query(Pawn, *Data, Pawn->GetNavAgentLocation(), Destination.Location);
	Query.SetAllowPartialPaths(true);
	const FNavPathSharedPtr Path = Nav->FindPathSync(Pawn->GetNavAgentPropertiesRef(), Query).Path;
	if (!Path.IsValid() || !Path->IsValid() || !Path->IsPartial() || Path->GetPathPoints().IsEmpty()) return nullptr;
	const FVector End = Path->GetPathPoints().Last().Location;
	float Radius, Height;
	Pawn->GetSimpleCollisionCylinder(Radius, Height);
	ALSBarricade* Nearest = nullptr;
	double Best = Radius + ArrivalTolerance;
	for (TActorIterator<ALSBarricade> It(GetWorld()); It; ++It)
	{
		if (!It->IsObstacleActive()) continue;
		const double Distance = LSObstacleGeometry::GetPlanarBodyDistance(It->GetObstacleBody(), End);
		if (Distance <= Best) { Best = Distance; Nearest = *It; }
	}
	return Nearest;
}

bool ULSOrderComponent::GetNavigationOriginHeight(double& OutHeight) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* Data = Nav && Pawn ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
	FNavLocation ProjectedHere;
	if (!Data || !Nav->ProjectPointToNavigation(Pawn->GetNavAgentLocation(), ProjectedHere,
	                                                 FVector(150, 150, 300), Data))
		return false;
	// 경로의 바닥 좌표에서 실제 원점 위치를 예측한다.
	OutHeight = Pawn->GetActorLocation().Z - ProjectedHere.Location.Z;
	return true;
}

bool ULSOrderComponent::CanExecuteOrderKind(ELSOrderKind Kind) const
{
	switch (Kind)
	{
	case ELSOrderKind::Disarm:
	{
		const ULSSelectableComponent* Selection = GetOwner()->FindComponentByClass<ULSSelectableComponent>();
		return Selection && Selection->Kind == ELSSelectionKind::Unit && Cast<ALSUnit>(GetOwner()) && CanMove();
	}
	case ELSOrderKind::Move:
		return CanMove();
	case ELSOrderKind::AttackMove:
		return CanMove() && CanAttack();
	case ELSOrderKind::AttackTarget:
		return CanAttack();
	case ELSOrderKind::Idle:
	case ELSOrderKind::Hold:
		return true;
	default:
		return false;
	}
}

bool ULSOrderComponent::PrepareOrder(const FLSOrder& Order, FLSOrder& OutOrder)
{
	BlockingBarricade.Reset();
	const ULSSelectableComponent* Selection = GetOwner()->FindComponentByClass<ULSSelectableComponent>();
	if (!Selection || !Selection->CanPlayerControl())
	{
		return false;
	}
	OutOrder = Order;
	switch (Order.Kind)
	{
	case ELSOrderKind::Idle:
	case ELSOrderKind::Hold:
		return true;
	case ELSOrderKind::Move:
	case ELSOrderKind::AttackMove:
	{
		if (Order.Kind == ELSOrderKind::AttackMove && !CanAttack()) return false;
		bool bFullPathFailed = false;
		const bool bResolved = ResolveDestination(Order.Destination, OutOrder.Destination, nullptr, &bFullPathFailed);
		if (!bResolved && bFullPathFailed) BlockingBarricade = FindBlockingBarricade(Order.Destination);
		return bResolved;
	}
	case ELSOrderKind::AttackTarget:
	{
		FVector TargetDestination;
		return CanAttack() && IsHostile(Order.Target.Get()) &&
		       ((IsInAttackRange(Order.Target.Get()) && Attack->HasClearAttackPathFromLocation(GetOwner()->GetActorLocation(), Order.Target.Get())) ||
		        ResolveAttackDestination(Order.Target.Get(), TargetDestination, false));
	}
	default:
		return false;
	}
}

bool ULSOrderComponent::SubmitOrder(const FLSOrder& Order, bool bQueue)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		LastFailure = ELSOrderFailure::Unavailable;
		return false;
	}
	LastFailure = ELSOrderFailure::None;
	BlockingBarricade.Reset();
	if (Order.Kind == ELSOrderKind::Disarm)
	{
		ALSMine* Mine = Cast<ALSMine>(Order.Target.Get());
		FVector Destination;
		double Length;
		if (bQueue || !PrepareDisarm(Mine, Destination, Length) || !Mine->TryClaimDisarm(GetOwner()))
		{
			LastFailure = ELSOrderFailure::InvalidTarget;
			return false;
		}
		CancelDisarm();
		return BeginClaimedDisarm(Mine, Destination);
	}
	FLSOrder PreparedOrder;
	if (!HasBegunPlay() || !PrepareOrder(Order, PreparedOrder))
	{
		const ULSSelectableComponent* Selection = GetOwner()->FindComponentByClass<ULSSelectableComponent>();
		LastFailure = !HasBegunPlay() || !Selection || !Selection->CanPlayerControl()
		                  ? ELSOrderFailure::Unavailable
		                  : (Order.Kind == ELSOrderKind::AttackTarget && !IsHostile(Order.Target.Get())
		                         ? ELSOrderFailure::InvalidTarget
		                         : ELSOrderFailure::Unreachable);
		if (LastFailure == ELSOrderFailure::Unreachable && BlockingBarricade.IsValid())
			LastFailure = ELSOrderFailure::BlockedByBarricade;
		return false;
	}
	if (ActiveOrder.Kind == ELSOrderKind::Disarm)
	{
		CancelDisarm();
		bQueue = false;
	}
	if (bQueue && ActiveOrder.Kind != ELSOrderKind::Idle && ActiveOrder.Kind != ELSOrderKind::Hold)
	{
		if (PendingOrders.Num() >= MaxQueuedOrders)
		{
			LastFailure = ELSOrderFailure::QueueFull;
			return false;
		}
		PendingOrders.Add(PreparedOrder);
		return true;
	}
	PendingOrders.Reset();
	BeginOrder(PreparedOrder);
	return true;
}

void ULSOrderComponent::BeginOrder(const FLSOrder& Order)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	CancelIdleYield();
	bMineBlocked = false;
	if (bKnockbackActive)
	{
		// 공중에서 새 명령을 받아도 강제 이동 자체는 취소하지 않는다.
		ActiveOrder = Order;
		SetEngagementTarget(nullptr);
		bHasEngagement = false;
		if (Attack)
			Attack->StopAttack();
		return;
	}
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->SetAvoidanceEnabled(Order.Kind != ELSOrderKind::AttackTarget);
	}
	if (Attack)
	{
		Attack->StopAttack();
	}
	SetEngagementTarget(nullptr);
	ActiveOrder = Order;
	bHasEngagement = false;
	bAttackApproaching = false;
	SetMineAvoidance(UsesMineAvoidance());
	NextRepathTime = 0;
	if (Order.Kind == ELSOrderKind::Move || Order.Kind == ELSOrderKind::AttackMove)
	{
		if (Order.Kind == ELSOrderKind::AttackMove)
		{
			if (AActor* Enemy = CanAttack() ? FindEnemy(GetDetectionRange()) : nullptr)
			{
				Engage(Enemy, true);
				return;
			}
		}
		if (!MoveTo(Order.Destination))
		{
			StopMovement();
			FinishOrder();
		}
	}
	else if (Order.Kind == ELSOrderKind::AttackTarget)
	{
		// 이동 중 지정 공격도 즉시 추적으로 교체하고, 사거리 안에서만 정지한다.
		Engage(Order.Target.Get(), true);
	}
	else
	{
		StopMovement();
	}
}

bool ULSOrderComponent::MoveTo(const FVector& Goal, bool bAttackApproach)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		// RVO의 예측 반경이 공격 진입을 막지 않도록 교전 접근에서는 사용하지 않는다.
		// 실제 충돌과 NavMesh 경로·공격 위치의 점유 검사는 그대로 유지한다.
		Character->GetCharacterMovement()->SetAvoidanceEnabled(!bAttackApproach);
	}
	bAttackApproaching = bAttackApproach;
	SetMineAvoidance(UsesMineAvoidance());
	APawn* Pawn = Cast<APawn>(GetOwner());
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	if (!Controller || !Controller->GetPathFollowingComponent())
	{
		LastFailure = ELSOrderFailure::Unreachable;
		return false;
	}
	UPathFollowingComponent* PathFollowing = Controller->GetPathFollowingComponent();
	PathFollowing->OnRequestFinished.RemoveAll(this);
	PathFollowing->OnRequestFinished.AddUObject(this, &ULSOrderComponent::OnMovementFinished);
	// 교체 과정의 Abort 알림은 새 요청의 결과로 취급하지 않는다.
	// 기존 이동을 먼저 중단하지 않는다. UE의 요청 교체가 현재 속도를 보존한다.
	MovementRequestId = FAIRequestID::InvalidRequest;
	bMovementCompleted = false;
	FAIMoveRequest Request(Goal);
	float AcceptanceRadius = ArrivalTolerance;
	const ALSCastle* Castle = Cast<ALSCastle>(Pawn);
	const auto* CastleMovement = Castle ? Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent()) : nullptr;
	if (!bAttackApproach && CastleMovement && CastleMovement->bTurnBeforeForward)
	{
		Request.SetReachTestIncludesAgentRadius(false);
	}
	if (bAttackApproach)
	{
		double OriginHeight;
		if (!Attack || !EngagedTarget.IsValid() || !GetNavigationOriginHeight(OriginHeight))
			return false;
		const double RemainingRange = Attack->GetAttackSettings().Range -
		    ULSCombatTargetLibrary::GetAttackDistanceAtLocation(GetOwner(), EngagedTarget.Get(),
		                                                     Goal + FVector(0, 0, OriginHeight)) - 1.0;
		if (RemainingRange <= 0)
			return false;
		AcceptanceRadius = FMath::Min(ArrivalTolerance, static_cast<float>(FMath::Max(0.0, RemainingRange - GetAttackApproachPadding())));
		Request.SetReachTestIncludesAgentRadius(false);
		Request.SetReachTestIncludesGoalRadius(false);
	}
	if (ActiveOrder.Kind == ELSOrderKind::Disarm)
	{
		AcceptanceRadius = 5;
		Request.SetReachTestIncludesAgentRadius(false);
		Request.SetReachTestIncludesGoalRadius(false);
	}
	Request.SetAcceptanceRadius(AcceptanceRadius);
	Request.SetUsePathfinding(true);
	Request.SetAllowPartialPath(false);
	Request.SetProjectGoalLocation(!bAttackApproach && ActiveOrder.Kind != ELSOrderKind::Disarm);
	FPathFollowingRequestResult Result;
	if (UsesMineAvoidance())
	{
		const FLSMineSafePath Safe = LSMineAvoidance::BuildSafePath(Pawn, Goal, LSMineAvoidance::Snapshot(GetWorld()));
		if (Safe.Result != ELSMinePathResult::SafePath)
		{
			if (Safe.Result == ELSMinePathResult::HazardBlocked || Safe.Result == ELSMinePathResult::SearchLimit)
			{
				StopForMine();
				MineBlockedGoal = Goal;
				bMineBlockedAttackApproach = bAttackApproach;
				return ActiveOrder.Kind != ELSOrderKind::Disarm;
			}
			return false;
		}
		bMineBlocked = false;
		MovementRequestId = Controller->RequestMove(Request, Safe.Path);
		return MovementRequestId != FAIRequestID::InvalidRequest;
	}
	Result = Controller->MoveTo(Request);
	MovementRequestId = Result.MoveId;
	if (Result.Code != EPathFollowingRequestResult::RequestSuccessful)
	{
		bMovementCompleted = true;
		bLastMovementSucceeded = Result.Code == EPathFollowingRequestResult::AlreadyAtGoal;
	}
	if (Result.Code == EPathFollowingRequestResult::Failed)
		LastFailure = ELSOrderFailure::Unreachable;
	return Result.Code != EPathFollowingRequestResult::Failed;
}

bool ULSOrderComponent::ApplyKnockback(const FVector& Velocity)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!HasBegunPlay() || !IsValid(Character) || Character->IsActorBeingDestroyed() || Velocity.ContainsNaN() ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Character) || !GetWorld() || GetWorld()->bIsTearingDown)
		return false;
	CancelDisarm();
	CancelIdleYield();
	SetMineAvoidance(false);
	StopMovement();
	if (Attack)
		Attack->StopAttack();
	SetEngagementTarget(nullptr);
	bHasEngagement = false;
	NextRepathTime = 0;
	bKnockbackActive = true;
	// LaunchCharacter의 보류된 속도가 다음 이동 틱에서 적용될 시간을 보장한다.
	KnockbackResumeAfter = GetWorld()->GetTimeSeconds() + 0.05;
	Character->GetCharacterMovement()->SetAvoidanceEnabled(false);
	// 종료 뒤에도 명중 반응은 유지한다. MOVE_None에서 보류된 Launch가 버려지지 않게 한다.
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Character->LaunchCharacter(Velocity, true, true);
	return true;
}

void ULSOrderComponent::OnMovementFinished(FAIRequestID RequestId, const FPathFollowingResult& Result)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (MovementRequestId == FAIRequestID::InvalidRequest || RequestId != MovementRequestId)
	{
		return;
	}
	if (bYieldingToCastle)
	{
		bMovementCompleted = true;
		MovementRequestId = FAIRequestID::InvalidRequest;
		RequestOrderUpdate();
		return;
	}
	if (!Result.IsSuccess() && UsesMineAvoidance())
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		const ULSMineAwareMovementComponent* Movement = Character ? Cast<ULSMineAwareMovementComponent>(Character->GetCharacterMovement()) : nullptr;
		if (Movement && Movement->GetMineConstraintDuration() >= RepathInterval)
		{
			if (ActiveOrder.Kind == ELSOrderKind::Disarm) CancelDisarm();
			else StopForMine();
			return;
		}
	}
	bMovementCompleted = true;
	bLastMovementSucceeded = Result.IsSuccess();
	if (!Result.IsSuccess())
		LastFailure = ELSOrderFailure::Unreachable;
	MovementRequestId = FAIRequestID::InvalidRequest;
	RequestOrderUpdate();
}

void ULSOrderComponent::StopMovement()
{
	bAttackApproaching = false;
	MovementRequestId = FAIRequestID::InvalidRequest;
	bMovementCompleted = false;
	if (ALSCastle* Castle = Cast<ALSCastle>(GetOwner()))
	{
		Castle->StopMoving();
		return;
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (AAIController* AI = Cast<AAIController>(Pawn->GetController()))
		{
			AI->StopMovement();
		}
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}
}

bool ULSOrderComponent::IsMoving() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	return AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
}

bool ULSOrderComponent::IsHostile(AActor* Target) const
{
	return ULSCombatTargetLibrary::CanAutoAttackTarget(GetOwner(), Target) &&
	       ULSVisionSubsystem::PassesFriendlySight(GetOwner(), Target);
}

bool ULSOrderComponent::IsInAttackRange(AActor* Target) const
{
	const ULSBasicAttackComponent* Component = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	return Target && Component &&
	       ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Target) <= Component->GetAttackSettings().Range;
}

bool ULSOrderComponent::IsWaitingFriendlyBody(const AActor* Candidate) const
{
	const ILSFactionInterface* SourceFaction = Cast<ILSFactionInterface>(GetOwner());
	const ILSFactionInterface* CandidateFaction = Cast<ILSFactionInterface>(Candidate);
	if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed() || !SourceFaction ||
	    !CandidateFaction || CandidateFaction->GetFaction() != SourceFaction->GetFaction() ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Candidate)) return false;
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Candidate->GetRootComponent());
	const ULSOrderComponent* Orders = Candidate->FindComponentByClass<ULSOrderComponent>();
	const ALSUnit* Unit = Cast<ALSUnit>(Candidate);
	const bool bGrounded = Cast<ALSCastle>(Candidate) || (Unit && Unit->GetCharacterMovement()->IsMovingOnGround());
	return Capsule && Capsule->IsQueryCollisionEnabled() && Capsule->GetUpVector().Equals(FVector::UpVector, 0.001) &&
	    bGrounded && Candidate->GetVelocity().IsNearlyZero() && Orders && !Orders->IsMoving() &&
	    !Orders->IsYieldingToCastle() && !Orders->IsKnockedBack();
}

bool ULSOrderComponent::ResolveAttackDestination(AActor* Target, FVector& OutDestination, bool bAvoidMines,
                                                bool* OutMineBlocked) const
{
	if (OutMineBlocked) *OutMineBlocked = false;
	if (!IsHostile(Target) || !Attack || !CanMove())
		return false;
	float Radius, Height, TargetRadius, TargetHeight;
	GetOwner()->GetSimpleCollisionCylinder(Radius, Height);
	Target->GetSimpleCollisionCylinder(TargetRadius, TargetHeight);
	const APawn* Pawn = CastChecked<APawn>(GetOwner());
	double OriginHeight;
	if (!GetNavigationOriginHeight(OriginHeight))
		return false;
	const FVector CenterOffset(0, 0, OriginHeight);
	struct FLSWaitingBody
	{
		FVector Center;
		float Radius, Height;
	};
	TArray<FLSWaitingBody> WaitingBodies;
	const UCapsuleComponent* OwnCapsule = Cast<UCapsuleComponent>(GetOwner()->GetRootComponent());
	if ((Cast<ALSOutpost>(Target) || Cast<ALSPathObstacle>(Target)) && OwnCapsule &&
	    OwnCapsule->GetUpVector().Equals(FVector::UpVector, 0.001))
	{
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
			if (IsWaitingFriendlyBody(*It))
			{
				const UCapsuleComponent* Capsule = It->GetCapsuleComponent();
				WaitingBodies.Add({Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()});
			}
		for (TActorIterator<ALSCastle> It(GetWorld()); It; ++It)
			if (IsWaitingFriendlyBody(*It))
			{
				const UCapsuleComponent* Capsule = It->GetCapsuleComponent();
				WaitingBodies.Add({Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()});
			}
	}
	FVector Direction = (GetOwner()->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
		Direction = FVector::ForwardVector;
	const float Distance = Radius + TargetRadius + Attack->GetAttackSettings().Range * 0.8f;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSAttackApproach), false, GetOwner());
	Query.AddIgnoredActor(Target);
	FVector OccupiedFallback = FVector::ZeroVector;
	bool bHasFallback = false;
	FVector VacantFallback = FVector::ZeroVector;
	bool bHasVacantFallback = false;
	const TArray<FLSMineSphere> Mines = bAvoidMines ? LSMineAvoidance::Snapshot(GetWorld()) : TArray<FLSMineSphere>();
	TArray<FVector> Candidates;
	if (const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(Target))
	{
		Candidates = LSObstacleGeometry::GetAttackApproachCandidates(GetOwner(), Obstacle, Attack->GetAttackSettings().Range);
	}
	else
	{
		for (int32 Index = 0; Index < 17; ++Index)
		{
			const float Angle = Index == 0 ? 0.0f : (Index % 2 ? 1.0f : -1.0f) * ((Index + 1) / 2) * 22.5f;
			Candidates.Add(Target->GetActorLocation() + Direction.RotateAngleAxis(Angle, FVector::UpVector) * Distance);
		}
	}
	for (FVector Goal : Candidates)
	{
		Goal.Z = Pawn->GetNavAgentLocation().Z;
		FVector Projected;
		FNavPathSharedPtr CandidatePath;
		bool bResolved = false;
		if (bAvoidMines)
		{
			const FLSMineSafePath Safe = LSMineAvoidance::BuildSafePath(const_cast<APawn*>(Pawn), Goal, Mines);
			if (OutMineBlocked) *OutMineBlocked |= Safe.Result == ELSMinePathResult::HazardBlocked || Safe.Result == ELSMinePathResult::SearchLimit;
			bResolved = Safe.Result == ELSMinePathResult::SafePath;
			Projected = Safe.Destination;
			CandidatePath = Safe.Path;
		}
		else bResolved = ResolveDestination(Goal, Projected, WaitingBodies.IsEmpty() ? nullptr : &CandidatePath);
		if (bResolved &&
		    ULSCombatTargetLibrary::GetAttackDistanceAtLocation(GetOwner(), Target, Projected + CenterOffset) <=
		        Attack->GetAttackSettings().Range &&
		    Attack->HasClearAttackPathFromLocation(Projected + CenterOffset, Target))
		{
			if (!bHasFallback)
			{
				OccupiedFallback = Projected;
				bHasFallback = true;
			}
			// 앞줄 병력이 차지한 지점 대신 옆의 빈 공격 위치부터 선택한다.
			if (GetWorld()->OverlapAnyTestByObjectType(
			        Projected + CenterOffset, FQuat::Identity, Objects,
			        FCollisionShape::MakeCapsule(Radius + 4, FMath::Max(Height, Radius + 4)), Query))
				continue;
			if (!bHasVacantFallback) { VacantFallback = Projected; bHasVacantFallback = true; }
			if (!WaitingBodies.IsEmpty())
			{
				TArray<FVector> BodyCenters{GetOwner()->GetActorLocation()};
				// 도착 허용 거리 안의 일반 이동은 경로 조회 없이 수락하므로 실제 몸체 사이의 짧은 구간을 검사한다.
				if (!CandidatePath.IsValid() && !bAvoidMines) BodyCenters.Add(Projected + CenterOffset);
				else
				{
					if (!CandidatePath.IsValid() || !CandidatePath->IsValid() || CandidatePath->IsPartial()) continue;
					for (const FNavPathPoint& Point : CandidatePath->GetPathPoints()) BodyCenters.Add(Point.Location + CenterOffset);
				}
				if (WaitingBodies.ContainsByPredicate([&](const FLSWaitingBody& Body)
				{
					return !IsCapsulePathClear(BodyCenters, Radius + 4, FMath::Max(Height, Radius + 4), Body.Center, Body.Radius, Body.Height);
				})) continue;
			}
			OutDestination = Projected;
			return true;
		}
	}
	// 대기 병력을 피할 후보가 없어도 종전의 첫 빈 도착점을 먼저 유지한다.
	if (bHasVacantFallback)
	{
		OutDestination = VacantFallback;
		return true;
	}
	// 포위가 꽉 차도 유효한 공격 명령을 버리지 않고 회피·재탐색으로 빈자리를 기다린다.
	if (bHasFallback)
	{
		OutDestination = OccupiedFallback;
		return true;
	}
	return false;
}

float ULSOrderComponent::GetDetectionRange() const
{
	FLSUnitStatRow Stats;
	if (const ALSUnit* Unit = Cast<ALSUnit>(GetOwner()); Unit && Unit->TryGetStats(Stats))
	{
		return FMath::Max(Stats.DetectionRange, Attack ? Attack->GetAttackSettings().Range : 0.0f);
	}
	return Attack ? Attack->GetAttackSettings().Range : 0.0f;
}

AActor* ULSOrderComponent::FindEnemy(float Range) const
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	TArray<FOverlapResult> Hits;
	float SourceRadius, SourceHeight;
	GetOwner()->GetSimpleCollisionCylinder(SourceRadius, SourceHeight);
	GetWorld()->OverlapMultiByObjectType(
	    Hits, GetOwner()->GetActorLocation(), FQuat::Identity, Objects,
	    FCollisionShape::MakeBox(FVector(Range + SourceRadius + 1, Range + SourceRadius + 1, Range + SourceHeight + 1)),
	    FCollisionQueryParams(SCENE_QUERY_STAT(LSOrders), false, GetOwner()));
	AActor* Best = nullptr;
	double BestDistance = Range;
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(Candidate);
		if (!IsHostile(Candidate) || (Obstacle && !Obstacle->CanBeAutomaticallyAcquired()))
		{
			continue;
		}
		const double Distance = ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Candidate);
		if (Distance < BestDistance ||
		    (Distance == BestDistance && (!Best || Candidate->GetUniqueID() < Best->GetUniqueID())))
		{
			Best = Candidate;
			BestDistance = Distance;
		}
	}
	return Best;
}

void ULSOrderComponent::Engage(AActor* Target, bool bAllowChase)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	CancelIdleYield();
	// 정지한 앞줄까지 예측 회피하면 뒤의 근접 병력이 사거리 밖에서 옆으로 떠밀린다.
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->SetAvoidanceEnabled(false);
	}
	if (EngagedTarget.Get() != Target)
	{
		NextRepathTime = 0;
	}
	SetEngagementTarget(Target);
	bHasEngagement = true;
	SetMineAvoidance(UsesMineAvoidance());
	const float Padding = bAttackApproaching && IsMoving() ? GetAttackApproachPadding() : 0.0f;
	if (IsInAttackRange(Target) && Attack->HasClearAttackPathFromLocation(GetOwner()->GetActorLocation(), Target) &&
	    ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Target) <= Attack->GetAttackSettings().Range - Padding)
	{
		StopMovement();
		if (Attack && Attack->GetAttackTarget() != Target)
		{
			Attack->StartAttack(Target);
		}
	}
	else if (bAllowChase)
	{
		if (Attack)
		{
			Attack->StopAttack();
		}
		const double Now = GetWorld()->GetTimeSeconds();
		const bool bTargetMoved = Now - LastAttackRepathTime >= MinAttackRepathInterval &&
		    FVector::DistSquared(Target->GetActorLocation(), LastAttackTargetLocation) >= FMath::Square(TargetRepathDistance);
		if (Now >= NextRepathTime || bTargetMoved)
		{
			LastAttackRepathTime = Now;
			LastAttackTargetLocation = Target->GetActorLocation();
			NextRepathTime = Now + RepathInterval;
			FVector Destination;
			bool bAttackPathMineBlocked = false;
			if (!ResolveAttackDestination(Target, Destination, UsesMineAvoidance(), &bAttackPathMineBlocked) || !MoveTo(Destination, true))
			{
				if (UsesMineAvoidance() && bAttackPathMineBlocked)
				{
					StopForMine();
					return;
				}
				LastFailure = ELSOrderFailure::Unreachable;
				SetEngagementTarget(nullptr);
				FinishOrder();
			}
		}
	}
	else
	{
		StopMovement();
		if (Attack) Attack->StopAttack();
	}
}

void ULSOrderComponent::FinishOrder()
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		Character->GetCharacterMovement()->SetAvoidanceEnabled(true);
	ActiveOrder = FLSOrder();
	bHasEngagement = false;
	SetEngagementTarget(nullptr);
	if (Attack)
	{
		Attack->StopAttack();
	}
	while (!PendingOrders.IsEmpty())
	{
		const FLSOrder Next = PendingOrders[0];
		PendingOrders.RemoveAt(0);
		FLSOrder PreparedOrder;
		if (PrepareOrder(Next, PreparedOrder))
		{
			BeginOrder(PreparedOrder);
			return;
		}
	}
}

bool ULSOrderComponent::GetPlannedMovementPath(TArray<FVector>& OutPoints, float MaxDistance) const
{
	OutPoints.Reset();
	if (!FMath::IsFinite(MaxDistance) || MaxDistance <= 0 || bMovementCompleted || bKnockbackActive ||
	    bMineBlocked || !CanMove() ||
	    (ActiveOrder.Kind != ELSOrderKind::Move && ActiveOrder.Kind != ELSOrderKind::AttackMove && !bAttackApproaching))
		return false;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
	if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial() || !Path->IsUpToDate() ||
	    Following->GetStatus() == EPathFollowingStatus::Idle)
		return false;
	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	const FVector Origin = Pawn->GetNavAgentLocation();
	OutPoints.Add(Origin);
	auto AppendPoint = [&OutPoints, &MaxDistance](const FVector& Point)
	{
		const FVector Start = OutPoints.Last();
		const double Length = FVector::Dist2D(Start, Point);
		if (Length <= KINDA_SMALL_NUMBER) return true;
		if (Length >= MaxDistance)
		{
			OutPoints.Add(FMath::Lerp(Start, Point, MaxDistance / Length));
			return false;
		}
		OutPoints.Add(Point);
		MaxDistance -= Length;
		return true;
	};
	const int32 NextIndex = static_cast<int32>(Following->GetNextPathIndex());
	const ULSCastleMovementComponent* Movement = Cast<ULSCastleMovementComponent>(Pawn->GetMovementComponent());
	if (Movement && Movement->bTurnBeforeForward &&
	    Movement->Velocity.SizeSquared2D() > 1.0)
	{
		const FVector HorizontalVelocity(Movement->Velocity.X, Movement->Velocity.Y, 0);
		FVector NextDirection = FVector::ZeroVector;
		for (int32 Index = NextIndex; Index < Points.Num(); ++Index)
		{
			NextDirection = (Points[Index].Location - Origin).GetSafeNormal2D();
			if (!NextDirection.IsNearlyZero()) break;
		}
		const double StoppingDistance = HorizontalVelocity.SizeSquared() /
		    (2.0 * FMath::Max(1.0f, Movement->ForwardBrakingDeceleration));
		const bool bDirectionChanged = !NextDirection.IsNearlyZero() &&
		    FVector::DotProduct(HorizontalVelocity.GetSafeNormal(), NextDirection) < FMath::Cos(FMath::DegreesToRadians(1.0f));
		const auto* CastleFollowing = Cast<ULSCastlePathFollowingComponent>(Following);
		float DistanceToStop;
		const bool bCannotStopBeforeBoundary = CastleFollowing && CastleFollowing->GetDistanceToNextStop(DistanceToStop) &&
		    StoppingDistance > DistanceToStop;
		if (bDirectionChanged || bCannotStopBeforeBoundary)
		{
			// 새 경로를 받은 첫 틱 전에는 이동 요청 속도가 이전 방향일 수 있으므로 실제 경로를 비교한다.
			// 충돌로 제동 거리가 짧아져도 생산·양보는 충돌이 없을 때의 통과 구간까지 비워 둔다.
			if (!AppendPoint(Origin + HorizontalVelocity.GetSafeNormal() * StoppingDistance)) return true;
		}
	}
	for (int32 Index = NextIndex; Index < Points.Num(); ++Index)
		if (!AppendPoint(Points[Index].Location)) break;
	if (OutPoints.Num() >= 2) return true;
	OutPoints.Reset();
	return false;
}

void ULSOrderComponent::CancelIdleYield()
{
	if (!bYieldingToCastle) return;
	bYieldingToCastle = false;
	StopMovement();
	SetMineAvoidance(false);
}

bool ULSOrderComponent::IsIdleYieldPathClear(const TArray<FVector>& BodyCenters) const
{
	if (BodyCenters.Num() < 2) return false;
	float Radius, Height;
	GetOwner()->GetSimpleCollisionCylinder(Radius, Height);
	const float SweepRadius = Radius + 4;
	const float SweepHeight = FMath::Max(Height, SweepRadius);
	const FCollisionShape Body = FCollisionShape::MakeCapsule(SweepRadius, SweepHeight);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSIdleYield), false, GetOwner());
	TArray<FOverlapResult> InitialContacts;
	GetWorld()->OverlapMultiByObjectType(InitialContacts, BodyCenters[0], FQuat::Identity, Objects, Body, Query);
	for (const FOverlapResult& Contact : InitialContacts)
	{
		const AActor* Ally = Contact.GetActor();
		const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Ally);
		const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Contact.GetComponent());
		if ((!Cast<ALSUnit>(Ally) && !Cast<ALSCastle>(Ally)) || !Faction || Faction->GetFaction() != ELSFaction::Ally ||
		    !Capsule || Capsule != Ally->GetRootComponent() || !Capsule->GetUpVector().Equals(FVector::UpVector, 0.001)) continue;
		const FVector Center = Capsule->GetComponentLocation();
		if (!IsCapsulePathClear(BodyCenters, SweepRadius, SweepHeight, Center,
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight())) return false;
		Query.AddIgnoredComponent(Capsule);
	}
	for (int32 Index = 1; Index < BodyCenters.Num(); ++Index)
		if (GetWorld()->SweepTestByObjectType(BodyCenters[Index - 1], BodyCenters[Index], FQuat::Identity, Objects, Body, Query))
			return false;
	return true;
}

float ULSOrderComponent::GetIdleYieldAlongOffset(const FVector& Closest, const FVector& Direction, const FVector& Side,
	float CorridorRadius, bool& OutNearbyYielding) const
{
	float Radius, Height;
	GetOwner()->GetSimpleCollisionCylinder(Radius, Height);
	float AlongOffset = Radius * 2 + YieldClearance;
	OutNearbyYielding = false;
	for (TActorIterator<ALSTroopUnit> It(GetWorld()); It; ++It)
	{
		if (*It == GetOwner() || It->GetFaction() != ELSFaction::Ally || !ULSCombatTargetLibrary::IsCombatAlive(*It)) continue;
		float OtherRadius, OtherHeight;
		It->GetSimpleCollisionCylinder(OtherRadius, OtherHeight);
		const FVector Relative = It->GetActorLocation() - Closest;
		const double SideDistance = FVector::DotProduct(Relative, Side);
		if (FMath::Abs(FVector::DotProduct(Relative, Direction)) > CorridorRadius ||
		    SideDistance < -(Radius + OtherRadius) || SideDistance > CorridorRadius + YieldClearance + OtherRadius) continue;
		AlongOffset = FMath::Max(AlongOffset,
			static_cast<float>(FMath::Abs(FVector::DotProduct(Relative, Direction))) + Radius + OtherRadius + YieldClearance);
		const ULSOrderComponent* OtherOrders = It->Orders;
		if (OtherOrders && OtherOrders->bYieldingToCastle)
		{
			OutNearbyYielding = true;
			AlongOffset = FMath::Max(AlongOffset,
				static_cast<float>(FMath::Abs(FVector::DotProduct(OtherOrders->YieldDestination - Closest, Direction))) +
				Radius + OtherRadius + YieldClearance);
		}
	}
	return AlongOffset;
}

void ULSOrderComponent::UpdateIdleYield()
{
	ALSTroopUnit* Troop = Cast<ALSTroopUnit>(GetOwner());
	const ULSSelectableComponent* Selection = Troop ? Troop->Selection.Get() : nullptr;
	if (!Troop || Troop->GetFaction() != ELSFaction::Ally || !Selection || !Selection->CanPlayerControl() ||
	    ActiveOrder.Kind != ELSOrderKind::Idle || !PendingOrders.IsEmpty() || bHasEngagement ||
	    EngagedTarget.IsValid() || (Attack && Attack->GetAttackTarget()) || DisarmMine.IsValid() ||
	    bKnockbackActive || (!bYieldingToCastle && IsMoving()) || !CanMove() ||
	    !IsComponentTickEnabled() || GetWorld()->bIsTearingDown)
	{
		CancelIdleYield();
		return;
	}
	const ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	FLSPlayerCastleState CastleState;
	ALSCastle* Castle = Battlefield && Battlefield->IsSetupValid() && Battlefield->TryGetPlayerCastleState(CastleState)
	    ? Cast<ALSCastle>(CastleState.Castle.Get()) : nullptr;
	TArray<FVector> Corridor;
	if (!IsValid(Castle) || CastleState.bIsDead || Castle->IsActorBeingDestroyed() ||
	    Castle->GetFaction() != ELSFaction::Ally || !Castle->Orders ||
	    !Castle->Orders->GetPlannedMovementPath(Corridor, YieldLookAhead))
	{
		CancelIdleYield();
		YieldAttempts = 0;
		bYieldSpaceChanging = false;
		return;
	}
	float Radius, Height, CastleRadius, CastleHeight;
	Troop->GetSimpleCollisionCylinder(Radius, Height);
	Castle->GetSimpleCollisionCylinder(CastleRadius, CastleHeight);
	const float CorridorRadius = CastleRadius + Radius + YieldClearance;
	auto FindNearestSegment = [&Corridor](const FVector& Location, FVector& Closest, FVector& Direction)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Corridor.Num(); ++Index)
		{
			const FVector Segment = Corridor[Index] - Corridor[Index - 1];
			const double Length = Segment.Size2D();
			if (Length <= KINDA_SMALL_NUMBER) continue;
			const FVector Tangent = Segment.GetSafeNormal2D();
			const double Along = FVector::DotProduct(Location - Corridor[Index - 1], Tangent);
			if (Index == 1 && Along < 0) continue;
			const FVector Point = Corridor[Index - 1] + Tangent * FMath::Clamp(Along, 0.0, Length);
			const double Distance = FVector::DistSquared2D(Location, Point);
			if (Distance < Best) { Best = Distance; Closest = Point; Direction = Tangent; }
		}
		return Best;
	};
	FVector Closest, Direction;
	const double DistanceSquared = FindNearestSegment(Troop->GetNavAgentLocation(), Closest, Direction);
	if (bYieldingToCastle)
	{
		FVector UnusedPoint, UnusedDirection;
		if (bMovementCompleted || FindNearestSegment(YieldDestination, UnusedPoint, UnusedDirection) < FMath::Square(CorridorRadius))
			CancelIdleYield();
		return;
	}
	if (DistanceSquared > FMath::Square(CorridorRadius)) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextYieldTime) return;
	if (FVector::DistSquared2D(Castle->GetNavAgentLocation(), YieldCastleOrigin) > FMath::Square(CastleRadius * 0.5f) ||
	    !Castle->Orders->GetOrderDestination().Equals(YieldCastleGoal, YieldClearance))
	{
		YieldAttempts = 0;
		YieldCastleOrigin = Castle->GetNavAgentLocation();
		YieldCastleGoal = Castle->Orders->GetOrderDestination();
	}
	FVector Side = FVector::CrossProduct(FVector::UpVector, Direction);
	if (FVector::DotProduct(Troop->GetActorLocation() - Closest, Side) < 0) Side *= -1;
	bool bSideYielding = false, bOtherSideYielding = false;
	const float AlongOffsets[] = {
		GetIdleYieldAlongOffset(Closest, Direction, Side, CorridorRadius, bSideYielding),
		GetIdleYieldAlongOffset(Closest, Direction, -Side, CorridorRadius, bOtherSideYielding)};
	const float BaseSpacing = Radius * 2 + YieldClearance;
	bool bOuterSideYielding = false, bOtherOuterSideYielding = false;
	GetIdleYieldAlongOffset(Closest, Direction, Side, CorridorRadius + BaseSpacing, bOuterSideYielding);
	GetIdleYieldAlongOffset(Closest, Direction, -Side, CorridorRadius + BaseSpacing, bOtherOuterSideYielding);
	const bool bNearbyYielding = bSideYielding || bOtherSideYielding || bOuterSideYielding || bOtherOuterSideYielding;
	if (bYieldSpaceChanging && !bNearbyYielding) YieldAttempts = 0;
	bYieldSpaceChanging = bNearbyYielding;
	if (!bNearbyYielding && YieldAttempts >= MaxYieldAttempts) return;
	if (!bNearbyYielding) ++YieldAttempts;
	NextYieldTime = Now + YieldRetryInterval;
	double OriginHeight;
	if (!GetNavigationOriginHeight(OriginHeight)) return;
	const FVector CenterOffset(0, 0, OriginHeight);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSIdleYield), false, Troop);
	const FCollisionShape Body = FCollisionShape::MakeCapsule(Radius + 4, FMath::Max(Height, Radius + 4));
	const TArray<FLSMineSphere> Mines = LSMineAvoidance::Snapshot(GetWorld());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Troop->GetNavAgentPropertiesRef(), Troop->GetNavAgentLocation()) : nullptr;
	if (!Data) return;
	// 후방 후보보다 먼저 성의 진로 앞쪽으로 더 멀리 비킬 자리도 검사한다.
	// 바깥 행은 안쪽 이웃이 늘린 앞뒤 간격을 쓰지 않고 실제 경로·몸체 검사로 안전을 확인한다.
	constexpr int32 CandidateOrder[] = {0, 1, 2, 3, 12, 13, 4, 5, 6, 7, 8, 9, 10, 11};
	for (const int32 Index : CandidateOrder)
	{
		const bool bFarForward = Index >= 12;
		const int32 Row = bFarForward ? 0 : Index / 6;
		const int32 Slot = Index % 6;
		const float AlongSpacing = Row == 0 ? AlongOffsets[Slot % 2] : BaseSpacing;
		const float Along = bFarForward ? FMath::Max(AlongOffsets[Slot % 2], CorridorRadius + BaseSpacing)
			: (Slot / 2 == 0 ? 0.0f : (Slot / 2 == 1 ? 1.0f : -1.0f)) * AlongSpacing;
		const float SideDistance = CorridorRadius + YieldClearance + Row * BaseSpacing;
		FVector Candidate = Closest + Side * (Slot % 2 == 0 ? 1 : -1) * SideDistance + Direction * Along;
		Candidate.Z = Troop->GetNavAgentLocation().Z;
		auto DiagnoseCandidate = [&](const TCHAR* Stage, const FVector& Point)
		{
			const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("LS.RTS.InputDebug"));
			if (Debug && Debug->GetInt() != 0)
				UE_LOG(LogTemp, Display, TEXT("RTS_YIELD actor=%s at=%s attempt=%d changing=%d candidate=%d along=%.1f requested=%s checked=%s stage=%s"),
					*Troop->GetName(), *Troop->GetActorLocation().ToString(), YieldAttempts, bNearbyYielding, Index,
					Along, *Candidate.ToString(), *Point.ToString(), Stage);
		};
		FNavLocation Point;
		if (!Nav->ProjectPointToNavigation(Candidate, Point, FVector(150, 150, 300), Data) ||
		    FVector::DistSquared2D(Candidate, Point.Location) > FMath::Square(YieldClearance))
		{ DiagnoseCandidate(TEXT("projection"), Candidate); continue; }
		const FVector Projected = Point.Location;
		FVector UnusedPoint, UnusedDirection;
		if (FindNearestSegment(Projected, UnusedPoint, UnusedDirection) < FMath::Square(CorridorRadius))
		{ DiagnoseCandidate(TEXT("inside-corridor"), Projected); continue; }
		if (GetWorld()->OverlapAnyTestByObjectType(Projected + CenterOffset, FQuat::Identity, Objects, Body, Query))
		{ DiagnoseCandidate(TEXT("endpoint-occupied"), Projected); continue; }
		const FLSMineSafePath Safe = LSMineAvoidance::BuildSafePath(Troop, Projected, Mines);
		if (Safe.Result != ELSMinePathResult::SafePath || !Safe.Path.IsValid() || !Safe.Path->IsValid() || Safe.Path->IsPartial())
		{ DiagnoseCandidate(TEXT("no-full-safe-path"), Projected); continue; }
		TArray<FVector> BodyCenters{Troop->GetActorLocation()};
		for (const FNavPathPoint& YieldPoint : Safe.Path->GetPathPoints())
			BodyCenters.Add(YieldPoint.Location + CenterOffset);
		if (!IsIdleYieldPathClear(BodyCenters))
		{ DiagnoseCandidate(TEXT("body-path-blocked"), Projected); continue; }
		DiagnoseCandidate(TEXT("accepted"), Projected);
		AAIController* AI = CastChecked<AAIController>(Troop->GetController());
		UPathFollowingComponent* Following = AI->GetPathFollowingComponent();
		if (!Following) return;
		Following->OnRequestFinished.RemoveAll(this);
		Following->OnRequestFinished.AddUObject(this, &ULSOrderComponent::OnMovementFinished);
		bYieldingToCastle = true;
		YieldDestination = Safe.Destination;
		bMovementCompleted = false;
		MovementRequestId = FAIRequestID::InvalidRequest;
		SetMineAvoidance(true);
		FAIMoveRequest Request(Safe.Destination);
		Request.SetAcceptanceRadius(5);
		Request.SetReachTestIncludesAgentRadius(false);
		Request.SetReachTestIncludesGoalRadius(false);
		Request.SetAllowPartialPath(false);
		Request.SetProjectGoalLocation(false);
		MovementRequestId = AI->RequestMove(Request, Safe.Path);
		if (MovementRequestId == FAIRequestID::InvalidRequest) CancelIdleYield();
		return;
	}
}

void ULSOrderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(DeltaTime, TickType, Function);
	UpdateOrders(DeltaTime);
}

void ULSOrderComponent::UpdateOrders(float DeltaTime)
{
	if (bUpdatingOrders) return;
	TGuardValue<bool> UpdatingGuard(bUpdatingOrders, true);
	if (bBattleActionsEnded) return;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		EndBattleActions();
		return;
	}
	if (!IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed())
	{
		return;
	}
	if (!ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
	{
		bKnockbackActive = false;
		PendingOrders.Reset();
		BeginOrder(FLSOrder());
		return;
	}
	if (bKnockbackActive)
	{
		if (!TryCompleteKnockback()) return;
		const FLSOrder ResumeOrder = ActiveOrder;
		BeginOrder(ResumeOrder);
	}
	if (ActiveOrder.Kind == ELSOrderKind::Disarm)
	{
		TickDisarm(DeltaTime);
		return;
	}
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const ULSMineAwareMovementComponent* Movement = Cast<ULSMineAwareMovementComponent>(Character->GetCharacterMovement());
		if (UsesMineAvoidance() && Movement && Movement->GetMineConstraintDuration() >= RepathInterval && !bMineBlocked)
		{
			if (bYieldingToCastle) { CancelIdleYield(); return; }
			StopForMine();
		}
	}
	if (bMineBlocked && bMineBlockedAttackApproach)
	{
		AActor* BlockedTarget = EngagedTarget.Get();
		if (!IsHostile(BlockedTarget) ||
		    ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), BlockedTarget) > GetDetectionRange())
		{
			bMineBlocked = false;
			StopMovement();
			SetEngagementTarget(nullptr);
			bHasEngagement = false;
			if (Attack) Attack->StopAttack();
		}
		else
		{
			AActor* StationaryTarget = IsInAttackRange(BlockedTarget) ? BlockedTarget :
			    (CanAttack() ? FindEnemy(Attack->GetAttackSettings().Range) : nullptr);
			if (StationaryTarget && Attack->HasClearAttackPathFromLocation(GetOwner()->GetActorLocation(), StationaryTarget))
			{
				if (Attack && Attack->GetAttackTarget() != StationaryTarget) Attack->StartAttack(StationaryTarget);
				return;
			}
			if (Attack) Attack->StopAttack();
		}
	}
	if (bMineBlocked)
	{
		if (GetWorld()->GetTimeSeconds() >= NextRepathTime)
		{
			NextRepathTime = GetWorld()->GetTimeSeconds() + RepathInterval;
			if (bMineBlockedAttackApproach)
			{
				if (!IsHostile(EngagedTarget.Get()))
				{
					bMineBlocked = false;
				}
				else
				{
					FVector Destination;
					bool bAttackPathMineBlocked = false;
					if (ResolveAttackDestination(EngagedTarget.Get(), Destination, true, &bAttackPathMineBlocked)) MoveTo(Destination, true);
					else if (!bAttackPathMineBlocked) { bMineBlocked = false; FinishOrder(); }
				}
			}
			else if (!MoveTo(MineBlockedGoal, false)) { bMineBlocked = false; FinishOrder(); }
		}
		if (bMineBlocked) return;
	}
	if (ActiveOrder.Kind == ELSOrderKind::Move)
	{
		if (bMovementCompleted)
		{
			FinishOrder();
		}
		return;
	}
	if (ActiveOrder.Kind == ELSOrderKind::AttackTarget)
	{
		if (!IsHostile(ActiveOrder.Target.Get()))
		{
			StopMovement();
			FinishOrder();
		}
		else
		{
			Engage(ActiveOrder.Target.Get(), true);
		}
		return;
	}
	const bool bHold = ActiveOrder.Kind == ELSOrderKind::Hold;
	const float Detection = bHold ? (Attack ? Attack->GetAttackSettings().Range : 0.0f) : GetDetectionRange();
	AActor* Enemy = EngagedTarget.Get();
	if (!IsHostile(Enemy) || ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Enemy) > Detection)
	{
		Enemy = CanAttack() ? FindEnemy(Detection) : nullptr;
	}
	if (Enemy)
	{
		Engage(Enemy, !bHold);
		return;
	}
	if (bHasEngagement)
	{
		StopMovement();
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
			Character->GetCharacterMovement()->SetAvoidanceEnabled(true);
		if (Attack)
		{
			Attack->StopAttack();
		}
		SetEngagementTarget(nullptr);
		bHasEngagement = false;
	}
	if (ActiveOrder.Kind == ELSOrderKind::AttackMove && !IsMoving())
	{
		if (bMovementCompleted)
		{
			FinishOrder();
		}
		else if (!MoveTo(ActiveOrder.Destination))
		{
			FinishOrder();
		}
	}
	if (ActiveOrder.Kind == ELSOrderKind::Idle)
		UpdateIdleYield();
}

void ULSOrderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelIdleYield();
	GetWorld()->GetTimerManager().ClearTimer(OrderUpdateTimer);
	SetEngagementTarget(nullptr);
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		Character->OnCharacterMovementUpdated.RemoveDynamic(this, &ULSOrderComponent::OnOwnerMovementUpdated);
	CancelDisarm();
	if (ULSHealthComponent* Health = GetOwner()->FindComponentByClass<ULSHealthComponent>())
	{
		Health->OnDamagedEvent.RemoveDynamic(this, &ULSOrderComponent::OnOwnerDamaged);
		Health->OnDiedEvent.RemoveDynamic(this, &ULSOrderComponent::OnOwnerDamaged);
	}
	PendingOrders.Reset();
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const AAIController* Controller = Cast<AAIController>(Pawn->GetController()))
		{
			if (UPathFollowingComponent* PathFollowing = Controller->GetPathFollowingComponent())
			{
				PathFollowing->OnRequestFinished.RemoveAll(this);
			}
		}
	}
	StopMovement();
	if (Attack)
	{
		Attack->StopAttack();
	}
	Super::EndPlay(Reason);
}

TArray<FVector> ULSOrderComponent::MakeFormation(const FVector& Center, int32 Count, float Spacing)
{
	TArray<FVector> Points;
	if (Count <= 0 || Count > 1024 || Center.ContainsNaN() || !FMath::IsFinite(Spacing) || Spacing <= 0)
	{
		return Points;
	}
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	FVector Mean = FVector::ZeroVector;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Offset((Index / Columns) * Spacing, (Index % Columns) * Spacing, 0);
		Points.Add(Offset);
		Mean += Offset;
	}
	Mean /= Count;
	for (FVector& Point : Points)
	{
		Point += Center - Mean;
	}
	return Points;
}


bool ULSOrderComponent::IsDisarmTargetValid(ALSMine* Mine) const
{
	return IsValid(Mine) && Mine->IsArmed() && !Mine->IsActorBeingDestroyed() &&
	       ULSCombatTargetLibrary::IsCombatAlive(Mine) && ULSVisionSubsystem::PassesFriendlySight(GetOwner(), Mine);
}

bool ULSOrderComponent::PrepareDisarm(ALSMine* Mine, FVector& Destination, double& Length) const
{
	const ULSSelectableComponent* Selection = GetOwner()->FindComponentByClass<ULSSelectableComponent>();
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()) ||
	    !HasBegunPlay() || !Selection || !Selection->CanPlayerControl() || Selection->Kind != ELSSelectionKind::Unit || !Cast<ALSUnit>(GetOwner()) ||
	    !CanMove() || bKnockbackActive || !IsDisarmTargetValid(Mine) || Mine->HasDisarmWorker())
		return false;
	const FLSMineSafePath Path = LSMineAvoidance::FindDisarmPath(Cast<APawn>(GetOwner()), Mine);
	if (Path.Result != ELSMinePathResult::SafePath) return false;
	Destination = Path.Destination;
	Length = Path.Length;
	return true;
}

bool ULSOrderComponent::BeginClaimedDisarm(ALSMine* Mine, const FVector& Destination)
{
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()) ||
	    !IsDisarmTargetValid(Mine) || !CanMove())
	{
		if (IsValid(Mine)) Mine->ReleaseDisarm(GetOwner());
		return false;
	}
	PendingOrders.Reset();
	DisarmMine = Mine;
	DisarmElapsed = 0;
	bDisarmWorking = false;
	FLSOrder Order;
	Order.Kind = ELSOrderKind::Disarm;
	Order.Target = Mine;
	Order.Destination = Destination;
	BeginOrder(Order);
	if (!MoveTo(Destination))
	{
		CancelDisarm();
		return false;
	}
	return true;
}

void ULSOrderComponent::CancelDisarm()
{
	if (ALSMine* Mine = DisarmMine.Get()) Mine->ReleaseDisarm(GetOwner());
	DisarmMine.Reset();
	DisarmElapsed = 0;
	bDisarmWorking = false;
	if (ActiveOrder.Kind == ELSOrderKind::Disarm)
	{
		PendingOrders.Reset();
		FLSOrder Hold;
		Hold.Kind = ELSOrderKind::Hold;
		BeginOrder(Hold);
	}
}

void ULSOrderComponent::OnOwnerDamaged(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage > 0) CancelDisarm();
}

void ULSOrderComponent::TickDisarm(float DeltaTime)
{
	ALSMine* Mine = DisarmMine.Get();
	if (!IsDisarmTargetValid(Mine)) { CancelDisarm(); return; }
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		if (const ULSMineAwareMovementComponent* Movement = Cast<ULSMineAwareMovementComponent>(Character->GetCharacterMovement()))
			if (Movement->GetMineConstraintDuration() >= RepathInterval) { CancelDisarm(); return; }
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const double Distance = LSMineAvoidance::GetDisarmDistance(Pawn, Mine);
	const bool bInRange = Distance > Mine->GetTriggerRadius() && Distance <= Mine->GetDisarmDistance();
	if (bDisarmWorking && !bInRange) { CancelDisarm(); return; }
	if (!bDisarmWorking)
	{
		if (!bMovementCompleted) return;
		if (!bLastMovementSucceeded || !bInRange) { CancelDisarm(); return; }
		StopMovement();
		bDisarmWorking = true;
		return;
	}
	if (!Pawn || !Pawn->GetVelocity().IsNearlyZero()) { CancelDisarm(); return; }
	if (!LSMineAvoidance::HasDisarmLine(Pawn, Mine)) { CancelDisarm(); return; }
	DisarmElapsed += DeltaTime;
	Mine->SetDisarmProgress(GetOwner(), DisarmElapsed / Mine->GetDisarmDuration());
	if (DisarmElapsed >= Mine->GetDisarmDuration())
	{
		Mine->TryCompleteDisarm(GetOwner());
		CancelDisarm();
	}
}

bool ULSOrderComponent::UsesMineAvoidance() const
{
	return bYieldingToCastle || ActiveOrder.Kind == ELSOrderKind::Disarm ||
	       (bHasEngagement && ActiveOrder.Kind != ELSOrderKind::AttackTarget);
}

void ULSOrderComponent::SetMineAvoidance(bool bEnabled)
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		if (ULSMineAwareMovementComponent* Movement = Cast<ULSMineAwareMovementComponent>(Character->GetCharacterMovement()))
			Movement->SetMineAvoidanceEnabled(bEnabled);
}

void ULSOrderComponent::StopForMine()
{
	if (!bMineBlocked)
	{
		MineBlockedGoal = bHasEngagement ? (EngagedTarget.IsValid() ? EngagedTarget->GetActorLocation() : ActiveOrder.Destination) : ActiveOrder.Destination;
		bMineBlockedAttackApproach = bHasEngagement;
	}
	bMineBlocked = true;
	NextRepathTime = GetWorld()->GetTimeSeconds() + RepathInterval;
	StopMovement();
}

float ULSOrderComponent::GetAttackApproachPadding() const
{
	return Attack ? FMath::Clamp(AttackApproachPadding, 0.0f, Attack->GetAttackSettings().Range * 0.1f) : 0.0f;
}

void ULSOrderComponent::RequestOrderUpdate()
{
	if (bBattleActionsEnded || !IsComponentTickEnabled() || GetWorld()->GetTimerManager().TimerExists(OrderUpdateTimer))
		return;
	// 경로·사망 콜백 안에서 새 이동 요청을 만들지 않고 다음 틱에 한 번만 판단한다.
	const uint64 LifeSerial = OrderLifeSerial;
	OrderUpdateTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this, LifeSerial]()
		{
			if (OrderLifeSerial != LifeSerial) return;
			OrderUpdateTimer.Invalidate();
			if (IsComponentTickEnabled()) UpdateOrders(0);
		}));
}

void ULSOrderComponent::SetEngagementTarget(AActor* Target)
{
	if (EngagedTarget.Get() == Target) return;
	if (AActor* Previous = EngagedTarget.Get())
		if (ULSHealthComponent* Health = Previous->FindComponentByClass<ULSHealthComponent>())
			Health->OnDiedEvent.RemoveDynamic(this, &ULSOrderComponent::OnEngagedTargetDied);
	EngagedTarget = Target;
	if (Target)
		if (ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>())
			Health->OnDiedEvent.AddUniqueDynamic(this, &ULSOrderComponent::OnEngagedTargetDied);
}

void ULSOrderComponent::OnEngagedTargetDied(const FLSDamageResult& Damage)
{
	RequestOrderUpdate();
}

bool ULSOrderComponent::TryCompleteKnockback()
{
	if (!bKnockbackActive) return true;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!GetWorld() || (Character && Character->GetCharacterMovement()->IsFalling()) ||
	    GetWorld()->GetTimeSeconds() < KnockbackResumeAfter) return false;
	bKnockbackActive = false;
	return true;
}

void ULSOrderComponent::OnOwnerMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (IsComponentTickEnabled() && bAttackApproaching && !bKnockbackActive && !bMineBlocked &&
	    IsHostile(EngagedTarget.Get()) && IsInAttackRange(EngagedTarget.Get()))
		UpdateOrders(0);
}
