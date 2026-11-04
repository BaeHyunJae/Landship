#include "Navigation/LSCastlePathFollowingComponent.h"
#include "Navigation/LSCastlePathGeometry.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

bool ULSCastlePathFollowingComponent::HandlePathUpdateEvent()
{
	const AAIController* Controller = Cast<AAIController>(GetOwner());
	// Recast 재탐색은 기존 점·포털을 지운 뒤 알린다. 새 점에 적용한 후 추종 구간을 다시 계산한다.
	LSCastlePathGeometry::OffsetCorners(Controller ? Controller->GetPawn() : nullptr, Path);
	return Super::HandlePathUpdateEvent();
}

ULSCastleMovementComponent* ULSCastlePathFollowingComponent::GetCastleMovement() const
{
	const AAIController* Controller = Cast<AAIController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	auto* Movement = Pawn ? Cast<ULSCastleMovementComponent>(Pawn->GetMovementComponent()) : nullptr;
	return Movement && Movement->bTurnBeforeForward ? Movement : nullptr;
}

bool ULSCastlePathFollowingComponent::NeedsStopAtCurrentTarget() const
{
	if (!Path.IsValid() || MoveSegmentEndIndex >= Path->GetPathPoints().Num() - 1) return true;
	const FVector NextDirection = (Path->GetPathPoints()[MoveSegmentEndIndex + 1].Location -
		GetCurrentTargetLocation()).GetSafeNormal2D();
	return FVector::DotProduct(GetCurrentDirection().GetSafeNormal2D(), NextDirection) < FMath::Cos(FMath::DegreesToRadians(1.0f));
}

bool ULSCastlePathFollowingComponent::HasReachedDestination(const FVector& CurrentLocation) const
{
	const auto* Movement = GetCastleMovement();
	return Super::HasReachedDestination(CurrentLocation) &&
		(!Movement || Movement->Velocity.SizeSquared2D() <= 1.0);
}

bool ULSCastlePathFollowingComponent::HasReachedCurrentTarget(const FVector& CurrentLocation) const
{
	const auto* Movement = GetCastleMovement();
	return Super::HasReachedCurrentTarget(CurrentLocation) &&
		(!Movement || !NeedsStopAtCurrentTarget() || Movement->Velocity.SizeSquared2D() <= 1.0);
}

bool ULSCastlePathFollowingComponent::HasReachedStopGeometry(const FVector& Location) const
{
	return MoveSegmentEndIndex >= Path->GetPathPoints().Num() - 1
		? Super::HasReachedDestination(Location) : Super::HasReachedCurrentTarget(Location);
}

bool ULSCastlePathFollowingComponent::GetDistanceToNextStop(float& OutDistance) const
{
	const auto* Movement = GetCastleMovement();
	if (!Movement || GetStatus() != EPathFollowingStatus::Moving || !HasValidPath() || !NeedsStopAtCurrentTarget()) return false;
	const FVector Start = Movement->GetActorFeetLocation();
	if (HasReachedStopGeometry(Start))
	{
		OutDistance = 0;
		return true;
	}
	const FVector Target = GetCurrentTargetLocation();
	if (!HasReachedStopGeometry(Target)) return false;
	float Outside = 0;
	float Inside = 1;
	// UE가 계산하는 실제 도착 경계를 그대로 질의한다. 캡슐 가산·명령별 허용 반경을 복제하지 않는다.
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float Middle = (Outside + Inside) * .5f;
		if (HasReachedStopGeometry(FMath::Lerp(Start, Target, Middle))) Inside = Middle;
		else Outside = Middle;
	}
	OutDistance = static_cast<float>(FVector::Dist2D(Start, Target)) * Inside;
	return true;
}
