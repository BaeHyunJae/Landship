#include "Navigation/LSSlopeMovementComponent.h"

void ULSSlopeMovementComponent::SetSlopeSpeedCurve(const FRichCurve& Curve)
{
	SlopeSpeedCurve = Curve;
	SlopeSpeedMultiplier = 1.0f;
}

void ULSSlopeMovementComponent::ClearSlopeSpeedCurve()
{
	SlopeSpeedCurve.Reset();
	SlopeSpeedMultiplier = 1.0f;
}

float ULSSlopeMovementComponent::GetMaxSpeed() const
{
	return Super::GetMaxSpeed() * (IsMovingOnGround() ? SlopeSpeedMultiplier : 1.0f);
}

void ULSSlopeMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (SlopeSpeedCurve.GetNumKeys() != 0 && IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
	{
		// AI는 경로 요청 벡터를 사용하고 직접 조종은 현재 입력 가속도를 사용한다.
		const FVector MoveDirection =
		    (bHasRequestedVelocity ? RequestedVelocity : Acceleration).GetSafeNormal2D();
		float TargetMultiplier = 1.0f;
		const FVector FloorNormal = CurrentFloor.HitResult.ImpactNormal;
		if (!MoveDirection.IsNearlyZero() && FloorNormal.Z > UE_SMALL_NUMBER)
		{
			const double Grade = -FVector::DotProduct(FloorNormal, MoveDirection) / FloorNormal.Z;
			const float Angle = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(Grade)));
			const float Value = SlopeSpeedCurve.Eval(Angle, 1.0f);
			if (FMath::IsFinite(Value))
				TargetMultiplier = FMath::Clamp(Value, 0.25f, 1.5f);
		}
		SlopeSpeedMultiplier = FMath::FInterpTo(SlopeSpeedMultiplier, TargetMultiplier, DeltaTime, 10.0f);
	}
	else
	{
		SlopeSpeedMultiplier = 1.0f;
	}

	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}
