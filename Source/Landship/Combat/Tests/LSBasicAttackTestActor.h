#pragma once

#include "GameFramework/Actor.h"
#include "Combat/LSBasicAttackParticipant.h"
#include "LSBasicAttackTestActor.generated.h"

class ULSBasicAttackComponent;

/** 자동 검사에서만 진영·체력 연결부를 대체한다. 실제 전투 체력 구현이 아니다. */
UCLASS(NotPlaceable, Transient)
class ALSBasicAttackTestActor : public AActor, public ILSBasicAttackParticipant
{
	GENERATED_BODY()
public:
	ALSBasicAttackTestActor();
	virtual bool IsCombatAlive_Implementation() const override;
	virtual bool CanReceiveBasicAttack_Implementation(AActor* Attacker) const override;
	virtual float ReceiveBasicAttack_Implementation(AActor* Attacker, float Damage) override;

	UFUNCTION()
	void ObserveAttack(AActor* Target, float AppliedDamage);

	UPROPERTY()
	TObjectPtr<ULSBasicAttackComponent> Attack;
	float Health = 100.0f;
	int32 Team = 0;
	int32 HitsReceived = 0;
	int32 Notifications = 0;
	bool bReenterOnNotification = false;
	bool bReentrantAccepted = false;
	bool bImmune = false;
};
