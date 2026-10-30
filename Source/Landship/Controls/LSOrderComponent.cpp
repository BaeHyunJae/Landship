#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicAttackParticipant.h"
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

ULSOrderComponent::ULSOrderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void ULSOrderComponent::BeginPlay()
{
	Super::BeginPlay();
	Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	if (Attack) { Attack->StopAttack(); }
}

bool ULSOrderComponent::CanAttack() const
{
	const ULSBasicAttackComponent* Component = GetOwner() ? GetOwner()->FindComponentByClass<ULSBasicAttackComponent>() : nullptr;
	return Component && Component->IsConfigured() && GetOwner()->Implements<ULSBasicAttackParticipant>();
}

bool ULSOrderComponent::CanMove() const
{
	FLSUnitStatRow UnitStats;
	if (const ALSUnit* Unit = Cast<ALSUnit>(GetOwner()); Unit && (!Unit->TryGetStats(UnitStats) || UnitStats.MovementSpeed <= 0)) { return false; }
	FLSCastleStatRow CastleStats;
	if (const ALSCastle* Castle = Cast<ALSCastle>(GetOwner()); Castle && (!Castle->TryGetStats(CastleStats) || CastleStats.MovementSpeed <= 0)) { return false; }
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->GetMovementComponent() && Cast<AAIController>(Pawn->GetController());
}

bool ULSOrderComponent::CanReach(const FVector& Goal) const
{
	if (Goal.ContainsNaN() || !CanMove()) { return false; }
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Point;
	if (!Nav || !Nav->ProjectPointToNavigation(Goal, Point, FVector(150, 150, 300))) { return false; }
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Pawn->GetNavAgentLocation(), Point.Location, GetOwner());
	return Path && Path->IsValid() && !Path->IsPartial();
}

bool ULSOrderComponent::ValidateOrder(const FLSOrder& Order) const
{
	const ULSSelectableComponent* Selection = GetOwner()->FindComponentByClass<ULSSelectableComponent>();
	if (!Selection || !Selection->CanPlayerControl()) { return false; }
	switch (Order.Kind)
	{
	case ELSOrderKind::Idle:
	case ELSOrderKind::Hold: return true;
	case ELSOrderKind::Move: return CanReach(Order.Destination);
	case ELSOrderKind::AttackMove: return CanAttack() && CanReach(Order.Destination);
	case ELSOrderKind::AttackTarget:
		return CanAttack() && IsHostile(Order.Target.Get())
			&& (IsInAttackRange(Order.Target.Get()) || CanReach(Order.Target->GetActorLocation()));
	default: return false;
	}
}

bool ULSOrderComponent::SubmitOrder(const FLSOrder& Order, bool bQueue)
{
	if (!HasBegunPlay() || !ValidateOrder(Order)) { return false; }
	if (bQueue && ActiveOrder.Kind != ELSOrderKind::Idle && ActiveOrder.Kind != ELSOrderKind::Hold)
	{
		if (PendingOrders.Num() >= MaxQueuedOrders) { return false; }
		PendingOrders.Add(Order);
		return true;
	}
	PendingOrders.Reset();
	BeginOrder(Order);
	return true;
}

void ULSOrderComponent::BeginOrder(const FLSOrder& Order)
{
	StopMovement();
	if (Attack) { Attack->StopAttack(); }
	EngagedTarget.Reset();
	ActiveOrder = Order;
	bHasEngagement = false;
	NextRepathTime = 0;
	if (Order.Kind == ELSOrderKind::Move || Order.Kind == ELSOrderKind::AttackMove)
	{
		if (!MoveTo(Order.Destination)) { FinishOrder(); }
	}
}

bool ULSOrderComponent::MoveTo(const FVector& Goal)
{
	if (ALSCastle* Castle = Cast<ALSCastle>(GetOwner())) { return Castle->RequestMove(Goal); }
	APawn* Pawn = Cast<APawn>(GetOwner());
	AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	return AI && AI->MoveToLocation(Goal, ArrivalTolerance, true, true, true, false, nullptr, false)
		!= EPathFollowingRequestResult::Failed;
}

void ULSOrderComponent::StopMovement()
{
	if (ALSCastle* Castle = Cast<ALSCastle>(GetOwner())) { Castle->StopMoving(); return; }
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (AAIController* AI = Cast<AAIController>(Pawn->GetController())) { AI->StopMovement(); }
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent()) { Movement->StopMovementImmediately(); }
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
	return IsValid(Target) && !Target->IsActorBeingDestroyed() && Target != GetOwner()
		&& Target->Implements<ULSBasicAttackParticipant>()
		&& ILSBasicAttackParticipant::Execute_IsCombatAlive(Target)
		&& ILSBasicAttackParticipant::Execute_CanReceiveBasicAttack(Target, GetOwner());
}

bool ULSOrderComponent::IsInAttackRange(AActor* Target) const
{
	const ULSBasicAttackComponent* Component = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	return Target && Component && FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation())
		<= FMath::Square(static_cast<double>(Component->GetAttackSettings().Range));
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
	GetWorld()->OverlapMultiByObjectType(Hits, GetOwner()->GetActorLocation(), FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(FMath::Max(0.01f, Range)), FCollisionQueryParams(SCENE_QUERY_STAT(LSOrders), false, GetOwner()));
	AActor* Best = nullptr;
	double BestDistance = FMath::Square(static_cast<double>(Range));
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!IsHostile(Candidate)) { continue; }
		const double Distance = FVector::DistSquared(GetOwner()->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance < BestDistance || (Distance == BestDistance && (!Best || Candidate->GetUniqueID() < Best->GetUniqueID())))
		{
			Best = Candidate;
			BestDistance = Distance;
		}
	}
	return Best;
}

void ULSOrderComponent::Engage(AActor* Target, bool bAllowChase)
{
	if (EngagedTarget.Get() != Target) { NextRepathTime = 0; }
	EngagedTarget = Target;
	bHasEngagement = true;
	if (IsInAttackRange(Target))
	{
		StopMovement();
		if (Attack && Attack->GetAttackTarget() != Target) { Attack->StartAttack(Target); }
	}
	else if (bAllowChase)
	{
		if (Attack) { Attack->StopAttack(); }
		if (GetWorld()->GetTimeSeconds() >= NextRepathTime)
		{
			NextRepathTime = GetWorld()->GetTimeSeconds() + RepathInterval;
			if (!MoveTo(Target->GetActorLocation())) { EngagedTarget.Reset(); FinishOrder(); }
		}
	}
}

void ULSOrderComponent::FinishOrder()
{
	ActiveOrder = FLSOrder();
	bHasEngagement = false;
	EngagedTarget.Reset();
	if (Attack) { Attack->StopAttack(); }
	while (!PendingOrders.IsEmpty())
	{
		const FLSOrder Next = PendingOrders[0];
		PendingOrders.RemoveAt(0);
		if (ValidateOrder(Next)) { BeginOrder(Next); return; }
	}
}

void ULSOrderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(DeltaTime, TickType, Function);
	if (!IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) { return; }
	if (GetOwner()->Implements<ULSBasicAttackParticipant>() && !ILSBasicAttackParticipant::Execute_IsCombatAlive(GetOwner()))
	{
		PendingOrders.Reset();
		BeginOrder(FLSOrder());
		return;
	}
	if (ActiveOrder.Kind == ELSOrderKind::Move)
	{
		if (!IsMoving()) { FinishOrder(); }
		return;
	}
	if (ActiveOrder.Kind == ELSOrderKind::AttackTarget)
	{
		if (!IsHostile(ActiveOrder.Target.Get())) { StopMovement(); FinishOrder(); }
		else { Engage(ActiveOrder.Target.Get(), true); }
		return;
	}
	const bool bHold = ActiveOrder.Kind == ELSOrderKind::Hold;
	const float Detection = bHold ? (Attack ? Attack->GetAttackSettings().Range : 0.0f) : GetDetectionRange();
	AActor* Enemy = EngagedTarget.Get();
	if (!IsHostile(Enemy) || FVector::DistSquared(GetOwner()->GetActorLocation(), Enemy->GetActorLocation()) > FMath::Square(static_cast<double>(Detection)))
	{
		Enemy = CanAttack() ? FindEnemy(Detection) : nullptr;
	}
	if (Enemy) { Engage(Enemy, !bHold); return; }
	if (bHasEngagement)
	{
		StopMovement();
		if (Attack) { Attack->StopAttack(); }
		EngagedTarget.Reset();
		bHasEngagement = false;
	}
	if (ActiveOrder.Kind == ELSOrderKind::AttackMove && !IsMoving())
	{
		float Radius, HalfHeight;
		GetOwner()->GetSimpleCollisionCylinder(Radius, HalfHeight);
		if (FVector::Dist2D(GetOwner()->GetActorLocation(), ActiveOrder.Destination) <= Radius + ArrivalTolerance + 15.0f) { FinishOrder(); }
		else if (!MoveTo(ActiveOrder.Destination)) { FinishOrder(); }
	}
}

void ULSOrderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	PendingOrders.Reset();
	StopMovement();
	if (Attack) { Attack->StopAttack(); }
	Super::EndPlay(Reason);
}

TArray<FVector> ULSOrderComponent::MakeFormation(const FVector& Center, int32 Count, float Spacing)
{
	TArray<FVector> Points;
	if (Count <= 0 || Count > 1024 || Center.ContainsNaN() || !FMath::IsFinite(Spacing) || Spacing <= 0) { return Points; }
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	FVector Mean = FVector::ZeroVector;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Offset((Index / Columns) * Spacing, (Index % Columns) * Spacing, 0);
		Points.Add(Offset);
		Mean += Offset;
	}
	Mean /= Count;
	for (FVector& Point : Points) { Point += Center - Mean; }
	return Points;
}
