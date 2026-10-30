#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSBasicAttackComponent.generated.h"

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSBasicAttackSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0"))
	float Damage = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0.001", Units = "s"))
	float Interval = 1.0f;
	/** 액터 원점 사이의 거리다. 메시 크기에 따라 달라지는 경계 거리는 사용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0", Units = "cm"))
	float Range = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSBasicAttackPerformed, AActor*, Target, float, AppliedDamage);

/** 사거리 안의 가장 가까운 적에게 즉시 피해를 적용한다. 추적 이동·애니메이션은 별도다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSBasicAttackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSBasicAttackComponent();
	virtual void Deactivate() override;

	/** 유효한 설정만 반영한다. 재설정으로 진행 중인 쿨타임을 초기화하지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool ConfigureAttack(const FLSBasicAttackSettings& NewSettings);

	/** 사거리 밖 대상도 지정할 수 있으며 사거리 안에 들어오면 공격한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool StartAttack(AActor* Target);

	/** 사거리 안에서 가장 가까운 유효한 적을 계속 다시 선택한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool StartAutoAttack();

	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	void StopAttack();

	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	AActor* GetAttackTarget() const { return AttackTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	float GetCooldownRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	FLSBasicAttackSettings GetAttackSettings() const { return Settings; }
	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	bool IsConfigured() const { return bConfigured; }

	UPROPERTY(BlueprintAssignable, Category = "Landship|Attack")
	FLSBasicAttackPerformed OnAttackPerformed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool IsValidCombatPair(AActor* Target) const;
	void UpdateAttack();
	void StartAttackTimer();
	AActor* FindNearestTarget() const;

	UPROPERTY(EditDefaultsOnly, Category = "Landship|Attack")
	bool bAutoAttackOnBeginPlay = true;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Attack")
	FLSBasicAttackSettings Settings;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> AttackTarget;

	/** 사거리 진입·사망 검사 주기다. 프레임이 느려져도 밀린 공격을 몰아서 적용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Attack", meta = (ClampMin = "0.01", Units = "s"))
	float CheckInterval = 0.05f;

	FTimerHandle AttackTimer;
	double NextAttackTime = 0.0;
	bool bConfigured = false;
	bool bResolvingAttack = false;
	bool bAutoSearching = false;
};
