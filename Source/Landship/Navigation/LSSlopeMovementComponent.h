#pragma once

#include "Curves/RichCurve.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LSSlopeMovementComponent.generated.h"

/** 걷는 동안 바닥 법선과 이동 요청 방향으로 경사 속도 배율을 계산한다. */
UCLASS()
class LANDSHIP_API ULSSlopeMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** 각도는 양수가 오르막이고 음수가 내리막이다. 빈 커브는 효과를 끈다. */
	void SetSlopeSpeedCurve(const FRichCurve& Curve);
	void ClearSlopeSpeedCurve();
	virtual float GetMaxSpeed() const override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

private:
	FRichCurve SlopeSpeedCurve;
	float SlopeSpeedMultiplier = 1.0f;
};
