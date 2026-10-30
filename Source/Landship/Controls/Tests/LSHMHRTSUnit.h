#pragma once

#include "Troops/LSUnit.h"
#include "Combat/LSBasicAttackParticipant.h"
#include "LSHMHRTSUnit.generated.h"

/** HMH 조작 실험 전용. 진영·체력은 팀원 코드 통합 전까지 사용하는 테스트 대역이다. */
UCLASS(NotBlueprintable)
class ALSHMHRTSUnit : public ALSUnit, public ILSBasicAttackParticipant
{
	GENERATED_BODY()
public:
	ALSHMHRTSUnit();
	virtual bool IsCombatAlive_Implementation() const override;
	virtual bool CanReceiveBasicAttack_Implementation(AActor* Attacker) const override;
	virtual float ReceiveBasicAttack_Implementation(AActor* Attacker, float Damage) override;
	virtual bool GetCombatHealth_Implementation(float& Current, float& Maximum) const override;
	UFUNCTION()
	void ObserveAttack(AActor* Target, float AppliedDamage);
	UPROPERTY(EditAnywhere, Category = "HMH Test Only")
	int32 TestTeam = 0;
	UPROPERTY(EditAnywhere, Category = "HMH Test Only")
	float TestHealth = 200.0f;
protected:
	virtual void BeginPlay() override;
};
