#include "Navigation/LSCastleMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "AIController.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "Components/CapsuleComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSFactionInterface.h"
#include "Controls/LSOrderComponent.h"
#include "Troops/LSUnit.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

void ULSCastleMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	if (!bTurnBeforeForward)
	{
		Super::RequestDirectMove(MoveVelocity, bForceMaxSpeed);
		return;
	}
	// 기본 구현은 속도를 즉시 덮어쓰므로 요청만 보관하고 이동 틱에서 가속·제동한다.
	RequestedVelocity = MoveVelocity;
	bHasRequestedVelocity = true;
	bRequestedMoveWithMaxSpeed = bForceMaxSpeed;
}

void ULSCastleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	                                           FActorComponentTickFunction* ThisTickFunction)
{
	if (ShouldSkipUpdate(DeltaTime) || !PawnOwner || !UpdatedComponent)
		return;
	// 경로를 따르는 AI는 ApplyControlInputToVelocity를 건너뛸 수 있다. 스윕보다 먼저 계산한다.
	if (bPlayerDrive && DeltaTime > SMALL_NUMBER)
		CalcPlayerDriveVelocity(DeltaTime);
	else if (bTurnBeforeForward && DeltaTime > SMALL_NUMBER)
		CalcForwardVelocity(DeltaTime);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ClearMoveIntent();
	if (bTurnBeforeForward || bPlayerDrive)
		ConsumeInputVector();
	if (IsActive() && UpdatedComponent && PawnOwner)
	{
		if (UAvoidanceManager* Avoidance = GetWorld()->GetAvoidanceManager())
		{
			// 성은 회피 속도를 받지 않고 몸체와 실제 속도만 병력의 RVO에 알린다.
			if (AvoidanceUID == INDEX_NONE)
				Avoidance->RegisterMovementComponent(this, 1.0f);
			else
				Avoidance->UpdateRVO(this);
		}
	}
}

void ULSCastleMovementComponent::ApplyControlInputToVelocity(float DeltaTime)
{
	if (bTurnBeforeForward || bPlayerDrive)
		ConsumeInputVector();
	else
		Super::ApplyControlInputToVelocity(DeltaTime);
}

void ULSCastleMovementComponent::CalcForwardVelocity(float DeltaTime)
{
	const FVector Input = GetPendingInputVector().GetClampedToMaxSize(1);
	const FVector Move = bHasRequestedVelocity ? RequestedVelocity.GetSafeNormal2D() : Input.GetSafeNormal2D();
	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const float CurrentSpeed = FMath::Max(0.0f, static_cast<float>(FVector::DotProduct(Velocity, Forward)));
	float RequestedSpeed = 0;
	if (!Move.IsNearlyZero())
	{
		RequestedSpeed = bHasRequestedVelocity
			? (bRequestedMoveWithMaxSpeed ? GetMaxSpeed() : FMath::Min(GetMaxSpeed(), static_cast<float>(RequestedVelocity.Size2D())))
			: GetMaxSpeed() * static_cast<float>(Input.Size2D());
		const AAIController* Controller = Cast<AAIController>(PawnOwner->GetController());
		const auto* PathFollowing = Controller ? Cast<ULSCastlePathFollowingComponent>(Controller->GetPathFollowingComponent()) : nullptr;
		float DistanceToStop;
		if (PathFollowing && PathFollowing->GetDistanceToNextStop(DistanceToStop))
		{
			const float SafeSpeed = DistanceToStop > 0
				? FMath::Max(1.0f, FMath::Sqrt(2 * GetMaxBrakingDeceleration() * DistanceToStop)) : 0;
			RequestedSpeed = FMath::Min(RequestedSpeed, SafeSpeed);
		}
		FRotator Rotation = UpdatedComponent->GetComponentRotation();
		const float DesiredYaw = Move.Rotation().Yaw;
		if (CurrentSpeed > 1 && FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, DesiredYaw)) > 1.0f)
		{
			// 새 방향을 받으면 현재 전진 방향으로 멈춘 뒤 선회한다.
			RequestedSpeed = 0;
		}
		else
		{
			Rotation.Yaw = FMath::FixedTurn(Rotation.Yaw, DesiredYaw, FMath::Max(1.0f, TurnRate) * DeltaTime);
			MoveUpdatedComponent(FVector::ZeroVector, Rotation, true);
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, DesiredYaw)) > 1.0f)
			{
				Velocity = FVector::ZeroVector;
				return;
			}
		}
	}
	const FVector TravelDirection = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const float Rate = RequestedSpeed > CurrentSpeed ? ForwardAcceleration : GetMaxBrakingDeceleration();
	const float Speed = FMath::FInterpConstantTo(CurrentSpeed, RequestedSpeed, DeltaTime, FMath::Max(1.0f, Rate));
	Velocity = TravelDirection * Speed;
}

void ULSCastleMovementComponent::SetPlayerDriveInput(float Throttle, float Steering)
{
	if (!FMath::IsFinite(Throttle) || !FMath::IsFinite(Steering))
		return;
	bPlayerDrive = true;
	PlayerThrottle = FMath::Clamp(Throttle, -1.0f, 1.0f);
	PlayerSteering = FMath::Clamp(Steering, -1.0f, 1.0f);
}

void ULSCastleMovementComponent::CalcPlayerDriveVelocity(float DeltaTime)
{
	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const float Speed = static_cast<float>(FVector::DotProduct(Velocity, Forward));
	const float Goal = PlayerThrottle * GetMaxSpeed() * (PlayerThrottle < 0 ? ReverseSpeedRatio : 1.0f);
	const bool bBraking = FMath::IsNearlyZero(Goal) || Speed * Goal < 0;
	const float TargetSpeed = bBraking ? 0.0f : Goal;
	const float NewSpeed = FMath::FInterpConstantTo(Speed, TargetSpeed, DeltaTime,
	    FMath::Max(1.0f, bBraking ? PlayerBrakingDeceleration : ForwardAcceleration));
	FRotator Rotation = UpdatedComponent->GetComponentRotation();
	Rotation.Yaw += PlayerSteering * TurnRate * DeltaTime;
	MoveUpdatedComponent(FVector::ZeroVector, Rotation, true);
	Velocity = Rotation.Vector().GetSafeNormal2D() * NewSpeed;
}

float ULSCastleMovementComponent::GetMaxBrakingDeceleration() const
{
	return FMath::Max(1.0f, bTurnBeforeForward ? ForwardBrakingDeceleration : Deceleration);
}

float ULSCastleMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal,
	                                               FHitResult& Hit, bool bHandleImpact)
{
	const bool bPushed = TryPushFriendlyTroop(Hit, Delta);
	if (!IsValid(PawnOwner) || !IsValid(UpdatedComponent))
		return 0;
	if (bPushed)
	{
		FHitResult Retry;
		SafeMoveUpdatedComponent(Delta * Time, UpdatedComponent->GetComponentQuat(), true, Retry);
		Hit = Retry;
		return Retry.Time * Time;
	}
	// 성의 전진 스윕이 막히면 멈춘다. 표면을 따라 옆으로 미끄러지지 않는다.
	return bTurnBeforeForward ? 0 : Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}

bool ULSCastleMovementComponent::TryPushFriendlyTroop(const FHitResult& Hit, const FVector& Delta)
{
	ALSUnit* Troop = Cast<ALSUnit>(Hit.GetActor());
	const auto* Faction = Cast<ILSFactionInterface>(PawnOwner);
	if (!IsValid(Troop) || !Faction || Troop->GetFaction() != Faction->GetFaction() ||
	    !ULSCombatTargetLibrary::IsCombatAlive(PawnOwner) || !ULSCombatTargetLibrary::IsCombatAlive(Troop) ||
	    Delta.IsNearlyZero() || !FMath::IsFinite(FriendlyPushSpeed) || FriendlyPushSpeed <= 0)
		return false;
	auto* Movement = Troop->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround() || !IsValid(Movement->UpdatedComponent))
		return false;
	const FVector Forward = Delta.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
		return false;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const float Side = FVector::DotProduct(Troop->GetActorLocation() - PawnOwner->GetActorLocation(), Right) < 0 ? -1 : 1;
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const auto* Data = Nav ? Nav->GetNavDataForProps(Troop->GetNavAgentPropertiesRef(), Troop->GetNavAgentLocation()) : nullptr;
	if (!Data)
		return false;
	const float Distance = FriendlyPushSpeed * FMath::Clamp(GetWorld()->GetDeltaSeconds(), 0.0f, 0.05f);
	for (const float Sign : {Side, -Side})
	{
		const FVector Push = (Right * Sign + Forward * 0.25f).GetSafeNormal() * Distance;
		FNavLocation Ground;
		const FVector Feet = Troop->GetNavAgentLocation();
		if (!Nav->ProjectPointToNavigation(Feet + Push, Ground, FVector(10, 10, 100), Data) ||
		    FVector::DistSquared2D(Feet + Push, Ground.Location) > FMath::Square(10.0) ||
		    FMath::Abs(Ground.Location.Z - Feet.Z) > Movement->MaxStepHeight)
			continue;
		const FVector Before = Troop->GetActorLocation();
		FHitResult PushHit;
		Movement->SafeMoveUpdatedComponent(Push, Movement->UpdatedComponent->GetComponentQuat(), true, PushHit);
		if (!IsValid(Troop) || !IsValid(Movement) || !IsValid(PawnOwner) || !IsValid(UpdatedComponent))
			return false;
		if (FVector::DistSquared2D(Before, Troop->GetActorLocation()) > 0.01)
		{
			if (auto* Orders = Troop->FindComponentByClass<ULSOrderComponent>())
				Orders->PauseAfterCastlePush(FriendlyPushRecovery);
			return true;
		}
	}
	return false;
}

void ULSCastleMovementComponent::ClearMoveIntent()
{
	PlayerThrottle = 0;
	PlayerSteering = 0;
	RequestedVelocity = FVector::ZeroVector;
	bHasRequestedVelocity = false;
	bRequestedMoveWithMaxSpeed = false;
}

void ULSCastleMovementComponent::StopMovementImmediately()
{
	ClearMoveIntent();
	ConsumeInputVector();
	Super::StopMovementImmediately();
}

void ULSCastleMovementComponent::StopActiveMovement()
{
	ClearMoveIntent();
	ConsumeInputVector();
	Super::StopActiveMovement();
}

float ULSCastleMovementComponent::GetRVOAvoidanceRadius()
{
	const auto* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	return Capsule ? Capsule->GetScaledCapsuleRadius() : 0;
}

float ULSCastleMovementComponent::GetRVOAvoidanceHeight()
{
	const auto* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0;
}

void ULSCastleMovementComponent::RemoveAvoidanceObstacle()
{
	if (AvoidanceUID != INDEX_NONE)
	{
		if (UWorld* World = GetWorld())
			if (UAvoidanceManager* Avoidance = World->GetAvoidanceManager())
				Avoidance->RemoveAvoidanceObject(AvoidanceUID);
		AvoidanceUID = INDEX_NONE;
	}
}

void ULSCastleMovementComponent::Deactivate()
{
	ClearMoveIntent();
	RemoveAvoidanceObstacle();
	Super::Deactivate();
}

void ULSCastleMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveAvoidanceObstacle();
	Super::EndPlay(EndPlayReason);
}
