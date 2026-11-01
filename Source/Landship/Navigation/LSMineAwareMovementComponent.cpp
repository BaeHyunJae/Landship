#include "Navigation/LSMineAwareMovementComponent.h"
#include "Navigation/LSMineAvoidance.h"
#include "GameFramework/Character.h"

void ULSMineAwareMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
												  FActorComponentTickFunction* ThisTickFunction)
{
	bExternalForceThisTick = !PendingImpulseToApply.IsNearlyZero() || !PendingForceToApply.IsNearlyZero() ||
							 !PendingLaunchVelocity.IsNearlyZero();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void ULSMineAwareMovementComponent::SetMineAvoidanceEnabled(bool bEnabled)
{
	bMineAvoidanceEnabled = bEnabled;
	if (!bEnabled)
	{
		bMineConstrained = false;
		MineConstraintDuration = 0;
	}
}

void ULSMineAwareMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid,
												 float BrakingDeceleration)
{
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	bMineConstrained = false;
	if (bMineAvoidanceEnabled && !bExternalForceThisTick && IsMovingOnGround() && CharacterOwner &&
		!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !Velocity.IsNearlyZero())
	{
		const TArray<FLSMineSphere> Mines = LSMineAvoidance::Snapshot(GetWorld());
		const FVector Start = CharacterOwner->GetNavAgentLocation();
		if (!LSMineAvoidance::IsSegmentSafe(CharacterOwner, Start, Start + Velocity * DeltaTime, Mines))
		{
			Velocity = FVector::ZeroVector;
			bMineConstrained = true;
		}
	}
	MineConstraintDuration = bMineConstrained ? MineConstraintDuration + DeltaTime : 0;
}
