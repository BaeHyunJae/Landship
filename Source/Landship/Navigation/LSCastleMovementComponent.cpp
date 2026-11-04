#include "Navigation/LSCastleMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "AIController.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "Components/CapsuleComponent.h"
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
	if (bTurnBeforeForward && DeltaTime > SMALL_NUMBER)
		CalcForwardVelocity(DeltaTime);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ClearMoveIntent();
	if (bTurnBeforeForward)
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
	if (bTurnBeforeForward)
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

float ULSCastleMovementComponent::GetMaxBrakingDeceleration() const
{
	return FMath::Max(1.0f, bTurnBeforeForward ? ForwardBrakingDeceleration : Deceleration);
}

float ULSCastleMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal,
	                                               FHitResult& Hit, bool bHandleImpact)
{
	// 성의 전진 스윕이 막히면 멈춘다. 표면을 따라 옆으로 미끄러지지 않는다.
	return bTurnBeforeForward ? 0 : Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}

void ULSCastleMovementComponent::ClearMoveIntent()
{
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
