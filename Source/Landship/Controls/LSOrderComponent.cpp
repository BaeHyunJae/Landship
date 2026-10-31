#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Controls/LSSelectableComponent.h"
#include "Troops/LSUnit.h"
#include "Castle/LSCastle.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

ULSOrderComponent::ULSOrderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void ULSOrderComponent::BeginPlay()
{
	Super::BeginPlay();
	Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	if (Attack)
	{
		Attack->StopAttack();
	}
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
	if (!ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
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

bool ULSOrderComponent::ResolveDestination(const FVector& Goal, FVector& OutDestination) const
{
	if (Goal.ContainsNaN() || !CanMove())
	{
		return false;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Point;
	if (!Nav || !Nav->ProjectPointToNavigation(Goal, Point, FVector(150, 150, 300)))
	{
		return false;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	// 이미 목적지에 있는 경우 한 점짜리 경로가 반환되어도 유효한 명령이다.
	if (FVector::DistSquared(Pawn->GetNavAgentLocation(), Point.Location) <= FMath::Square(ArrivalTolerance))
	{
		OutDestination = Point.Location;
		return true;
	}
	UNavigationPath* Path =
	    Nav->FindPathToLocationSynchronously(GetWorld(), Pawn->GetNavAgentLocation(), Point.Location, GetOwner());
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return false;
	}
	OutDestination = Point.Location;
	return true;
}

bool ULSOrderComponent::CanExecuteOrderKind(ELSOrderKind Kind) const
{
	switch (Kind)
	{
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

bool ULSOrderComponent::PrepareOrder(const FLSOrder& Order, FLSOrder& OutOrder) const
{
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
		return ResolveDestination(Order.Destination, OutOrder.Destination);
	case ELSOrderKind::AttackMove:
		return CanAttack() && ResolveDestination(Order.Destination, OutOrder.Destination);
	case ELSOrderKind::AttackTarget:
	{
		FVector TargetDestination;
		return CanAttack() && IsHostile(Order.Target.Get()) &&
		       (IsInAttackRange(Order.Target.Get()) ||
		        ResolveDestination(Order.Target->GetActorLocation(), TargetDestination));
	}
	default:
		return false;
	}
}

bool ULSOrderComponent::SubmitOrder(const FLSOrder& Order, bool bQueue)
{
	FLSOrder PreparedOrder;
	if (!HasBegunPlay() || !PrepareOrder(Order, PreparedOrder))
	{
		return false;
	}
	if (bQueue && ActiveOrder.Kind != ELSOrderKind::Idle && ActiveOrder.Kind != ELSOrderKind::Hold)
	{
		if (PendingOrders.Num() >= MaxQueuedOrders)
		{
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
	if (bKnockbackActive)
	{
		// 공중에서 새 명령을 받아도 강제 이동 자체는 취소하지 않는다.
		ActiveOrder = Order;
		EngagedTarget.Reset();
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
	EngagedTarget.Reset();
	ActiveOrder = Order;
	bHasEngagement = false;
	NextRepathTime = 0;
	if (Order.Kind == ELSOrderKind::Move || Order.Kind == ELSOrderKind::AttackMove)
	{
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

bool ULSOrderComponent::MoveTo(const FVector& Goal)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->SetAvoidanceEnabled(
		    !bHasEngagement &&
		    (ActiveOrder.Kind == ELSOrderKind::Move || ActiveOrder.Kind == ELSOrderKind::AttackMove));
	}
	APawn* Pawn = Cast<APawn>(GetOwner());
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	if (!Controller || !Controller->GetPathFollowingComponent())
	{
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
	Request.SetAcceptanceRadius(ArrivalTolerance);
	Request.SetUsePathfinding(true);
	Request.SetAllowPartialPath(false);
	Request.SetProjectGoalLocation(true);
	const FPathFollowingRequestResult Result = Controller->MoveTo(Request);
	MovementRequestId = Result.MoveId;
	if (Result.Code != EPathFollowingRequestResult::RequestSuccessful)
	{
		bMovementCompleted = true;
		bLastMovementSucceeded = Result.Code == EPathFollowingRequestResult::AlreadyAtGoal;
	}
	return Result.Code != EPathFollowingRequestResult::Failed;
}

bool ULSOrderComponent::ApplyKnockback(const FVector& Velocity)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!HasBegunPlay() || !IsValid(Character) || Character->IsActorBeingDestroyed() || Velocity.ContainsNaN() ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Character) || !GetWorld() || GetWorld()->bIsTearingDown)
		return false;
	StopMovement();
	if (Attack)
		Attack->StopAttack();
	EngagedTarget.Reset();
	bHasEngagement = false;
	NextRepathTime = 0;
	bKnockbackActive = true;
	// LaunchCharacter의 보류된 속도가 다음 이동 틱에서 적용될 시간을 보장한다.
	KnockbackResumeAfter = GetWorld()->GetTimeSeconds() + 0.05;
	Character->GetCharacterMovement()->SetAvoidanceEnabled(false);
	Character->LaunchCharacter(Velocity, true, true);
	return true;
}

void ULSOrderComponent::OnMovementFinished(FAIRequestID RequestId, const FPathFollowingResult& Result)
{
	if (MovementRequestId == FAIRequestID::InvalidRequest || RequestId != MovementRequestId)
	{
		return;
	}
	bMovementCompleted = true;
	bLastMovementSucceeded = Result.IsSuccess();
	MovementRequestId = FAIRequestID::InvalidRequest;
}

void ULSOrderComponent::StopMovement()
{
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
	return ULSCombatTargetLibrary::CanAutoAttackTarget(GetOwner(), Target);
}

bool ULSOrderComponent::IsInAttackRange(AActor* Target) const
{
	const ULSBasicAttackComponent* Component = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	return Target && Component &&
	       FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()) <=
	           FMath::Square(static_cast<double>(Component->GetAttackSettings().Range));
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
	GetWorld()->OverlapMultiByObjectType(Hits, GetOwner()->GetActorLocation(), FQuat::Identity, Objects,
	                                     FCollisionShape::MakeSphere(FMath::Max(0.01f, Range)),
	                                     FCollisionQueryParams(SCENE_QUERY_STAT(LSOrders), false, GetOwner()));
	AActor* Best = nullptr;
	double BestDistance = FMath::Square(static_cast<double>(Range));
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!IsHostile(Candidate))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), Candidate->GetActorLocation());
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
	// 공격 대상을 회피 장애물로 취급해 사거리 밖에서 멈추지 않게 한다.
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->SetAvoidanceEnabled(false);
	}
	if (EngagedTarget.Get() != Target)
	{
		NextRepathTime = 0;
	}
	EngagedTarget = Target;
	bHasEngagement = true;
	if (IsInAttackRange(Target))
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
		if (GetWorld()->GetTimeSeconds() >= NextRepathTime)
		{
			NextRepathTime = GetWorld()->GetTimeSeconds() + RepathInterval;
			if (!MoveTo(Target->GetActorLocation()))
			{
				EngagedTarget.Reset();
				FinishOrder();
			}
		}
	}
}

void ULSOrderComponent::FinishOrder()
{
	ActiveOrder = FLSOrder();
	bHasEngagement = false;
	EngagedTarget.Reset();
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

void ULSOrderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(DeltaTime, TickType, Function);
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
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		if (Character &&
		    (Character->GetCharacterMovement()->IsFalling() || GetWorld()->GetTimeSeconds() < KnockbackResumeAfter))
			return;
		bKnockbackActive = false;
		const FLSOrder ResumeOrder = ActiveOrder;
		BeginOrder(ResumeOrder);
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
	if (!IsHostile(Enemy) || FVector::DistSquared(GetOwner()->GetActorLocation(), Enemy->GetActorLocation()) >
	                             FMath::Square(static_cast<double>(Detection)))
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
		if (Attack)
		{
			Attack->StopAttack();
		}
		EngagedTarget.Reset();
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
}

void ULSOrderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
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
