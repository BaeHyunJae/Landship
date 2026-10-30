#pragma once

#include "Troops/LSUnit.h"
#include "Combat/LSHealthTypes.h"
#include "LSHMHRTSUnit.generated.h"

/** HMH 표시용 병력. 전투 수치·진영·체력·사망은 공통 ALSUnit 구현을 그대로 사용한다. */
UCLASS(NotBlueprintable)
class ALSHMHRTSUnit : public ALSUnit
{
	GENERATED_BODY()

  public:
	ALSHMHRTSUnit();

  protected:
	virtual void BeginPlay() override;

  private:
	UFUNCTION()
	void ObserveAttack(AActor* Target, float AppliedDamage);
	UFUNCTION()
	void ObserveDeath(const FLSDamageResult& Damage);
};
