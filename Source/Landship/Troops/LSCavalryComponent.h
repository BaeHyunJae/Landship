#pragma once
#include "Troops/LSTroopCombatComponent.h"
#include "LSCavalryComponent.generated.h"

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCavalryChargeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float StartDistance = 600;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float RunUpDistance = 350;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float SpeedMultiplier = 1.6f;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float ImpactReach = 70;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float DamageMultiplier = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float StaggerSeconds = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float KnockbackSpeed = 650;
	UPROPERTY(EditDefaultsOnly, Category = "Charge") float TimeoutSeconds = 6;
	bool IsValid() const;
};

/** 기병의 돌격 상태·도움닫기·접촉 피해·속도 복원을 전담한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSCavalryComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()
  public:
	virtual bool IsConfigurationValid() const override;
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents) override;
	bool TryStartCharge(AActor* Target);
	bool IsCharging() const
	{
		return bCharging;
	}
	bool HasSpentCharge() const
	{
		return bChargeSpent;
	}
	int32 GetImpactCount() const
	{
		return ImpactCount;
	}
	bool WasLastImpactGuarded() const
	{
		return bLastImpactGuarded;
	}
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Charge")
	FLSCavalryChargeSettings Settings;

  protected:
	virtual void CancelSpecialAction() override;
	virtual void TickSpecialAction(float DeltaTime) override;
	virtual bool HasSpecialAction() const override
	{
		return bCharging;
	}

  private:
	void StopCharge();
	void Impact(AActor* Target);
	TWeakObjectPtr<AActor> ChargeTarget;
	FVector PreviousLocation = FVector::ZeroVector;
	FVector ChargeDirection = FVector::ZeroVector;
	double ChargeStarted = 0;
	float OriginalSpeed = 0;
	float RunUp = 0;
	int32 ImpactCount = 0;
	bool bCharging = false;
	bool bChargeSpent = false;
	bool bLastImpactGuarded = false;
};
