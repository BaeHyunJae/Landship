#pragma once

#include "GameFramework/Actor.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthTypes.h"
#include "LSBasicAttackTestActor.generated.h"

class ULSBasicAttackComponent;
class ULSHealthComponent;

/** 공통 체력·피해 코드를 사용하는 자동 검사 액터. 별도 체력·진영 판정은 구현하지 않는다. */
UCLASS(NotPlaceable, Transient)
class ALSBasicAttackTestActor : public AActor, public ILSFactionInterface
{
	GENERATED_BODY()

  public:
	ALSBasicAttackTestActor();
	virtual ELSFaction GetFaction() const override
	{
		return Faction;
	}
	void SetInitialFaction(ELSFaction InFaction);
	float GetCurrentHealth() const;
	FLSApplyDamageResult ApplyTestDamage(float Amount, AActor* Source);
	UFUNCTION()
	void ObserveAttack(AActor* Target, float AppliedDamage);
	UFUNCTION()
	void ObserveDamage(const FLSDamageResult& Damage);
	UPROPERTY()
	TObjectPtr<ULSBasicAttackComponent> Attack;
	UPROPERTY()
	TObjectPtr<ULSHealthComponent> HealthComponent;
	int32 HitsReceived = 0;
	int32 Notifications = 0;
	bool bReenterOnNotification = false;
	bool bReentrantAccepted = false;

  protected:
	virtual void BeginPlay() override;

  private:
	void PrepareForDeath();
	ELSFaction Faction = ELSFaction::Ally;
	bool bFactionLocked = false;
};
