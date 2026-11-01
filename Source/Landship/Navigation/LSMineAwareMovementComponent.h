#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "LSMineAwareMovementComponent.generated.h"

/** 자발적인 지상 이동만 제한하며 발사·강제 이동의 접촉은 면제하지 않는다. */
UCLASS()
class LANDSHIP_API ULSMineAwareMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
  public:
	void SetMineAvoidanceEnabled(bool bEnabled);
	bool IsMineAvoidanceEnabled() const { return bMineAvoidanceEnabled; }
	bool IsMineConstrained() const { return bMineConstrained; }
	float GetMineConstraintDuration() const { return MineConstraintDuration; }
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

  private:
	bool bMineAvoidanceEnabled = false;
	bool bMineConstrained = false;
	float MineConstraintDuration = 0;
	bool bExternalForceThisTick = false;
};
