#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LSBasicAttackParticipant.generated.h"

UINTERFACE(BlueprintType)
class LANDSHIP_API ULSBasicAttackParticipant : public UInterface
{
	GENERATED_BODY()
};

/** 진영·체력 구현과 기본 공격 사이의 연결 지점이다. 기본 구현은 공통 체력·진영 규칙에 위임한다. */
class LANDSHIP_API ILSBasicAttackParticipant
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Landship|Combat")
	bool IsCombatAlive() const;
	virtual bool IsCombatAlive_Implementation() const;

	/** 진영과 무적 등 대상 측 규칙을 확인한다. 부작용 없이 결과만 반환해야 한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Landship|Combat")
	bool CanReceiveBasicAttack(AActor* Attacker) const;
	virtual bool CanReceiveBasicAttack_Implementation(AActor* Attacker) const;

	/** 체력 기능에 피해를 전달하고 실제 적용한 피해량을 반환한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Landship|Combat")
	float ReceiveBasicAttack(AActor* Attacker, float Damage);
	virtual float ReceiveBasicAttack_Implementation(AActor* Attacker, float Damage);

	/** UI는 체력 구현을 소유하지 않는다. 미연결 시 false를 반환한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Landship|Combat")
	bool GetCombatHealth(float& Current, float& Maximum) const;
	virtual bool GetCombatHealth_Implementation(float& Current, float& Maximum) const;
};
