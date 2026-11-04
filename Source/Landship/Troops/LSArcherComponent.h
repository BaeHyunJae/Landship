#pragma once

#include "Troops/LSTroopCombatComponent.h"
#include "LSArcherComponent.generated.h"

struct FLSDamageResult;

/** 실제 피격 시 일시적으로 사격 간격을 늘린다. 피해의 종류·공격자 병종과 관계없이 적용한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSArcherComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()
  public:
	virtual bool IsConfigurationValid() const override;
	virtual void SetBattleEnabled(bool bEnabled) override;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual FString GetRestingLabel() const override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Suppression", meta = (ClampMin = "0.01", Units = "s"))
	float SuppressionDuration = 2.0f;
	/** 공격속도 비율이 아니라 공격 간격의 배율이다. 기본 1초는 1.5초가 된다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Suppression", meta = (ClampMin = "1.01"))
	float SuppressedIntervalMultiplier = 1.5f;

  private:
	UFUNCTION()
	void OnDamaged(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnDied(const FLSDamageResult& Damage);
	void ClearSuppression();
	FTimerHandle SuppressionTimer;
	bool bSuppressed = false;
};
